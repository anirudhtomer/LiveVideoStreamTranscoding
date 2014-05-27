/*
 * Controller.cpp
 *
 *  Created on: 04-Jan-2011
 *      Author: anirudh
 */

#include<iostream>
#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>
#include "structures.h"
#include<string.h>
#include<sys/shm.h>
#include<sys/stat.h>
#include<sys/sem.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<pthread.h>
#include<fcntl.h>
#include<errno.h>
#include<netdb.h>
#include<signal.h>
#include<errno.h>
#include<mysql.h>
#include<sys/stat.h>
#include<time.h>

using namespace std;

void* transcodeControlPipeListener(void *);
void* serverHandler(void *);
void* httpRequestHandler(void *);
void* terminalCommandReader(void *);

int checkPort(char *);
void printHelp();

class ServerSocket{

protected:
	int serversocketfd;
	bool serverrunning;
	char ipaddress[16];
	int portnumber;
	
public:
	ServerSocket(){

	}

	bool createServerSocket(Controller *);
};


class Controller:public ServerSocket{

	int front,rear;
	bool transcodetableempty;

	struct sembuf waitop,signalop;

	pid_t transcodehandlerprocessid;
	struct TranscodeEntryStructure *transcodetable;

	int transcodetablesemid,dbsemid,queuesemid;

	int *transcodetablequeue;

	int transcodehandlerdatapipefd,transcodehandlercontrolpipefd;

	pthread_t threadtranscodecontrolpipehandler;
	pthread_t threadserver;
	pthread_t threadterminalcommandreader;

	struct TranscodeHandlerControlPipeDataType *transcodecontrolpipedatatype;

public:

	Controller(){
		
	}

	void initializeController(char ***);
	void initializeTranscodeHandler();
	void createPipes();
	void createThreads();
	void createSemaphore();
	void copyTranscodeEntryStructure(struct TranscodeEntryStructure*,struct TranscodeEntryStructure *);
	void Qinsert(int);
	int Qdelete();
	void exitClean(int);

	friend void* transcodeControlPipeListener(void *);
	friend void* serverHandler(void *);
	friend void* httpRequestHandler(void *);
	friend void* terminalCommandReader(void *);
};

int checkPort(char *service){

	if(strcmp(service,"http")==0)
		return HTTP;

	//similarly for all other
}

void printHelp(){

	cout<<"\nMedia Magic's DCMTOL, version 1.0\nHelp Menu\n\n";
	cout<<"'help': To get the list of all commands and their options\n";
	cout<<"'quit': To close DCMTOL\n\n";
}

//terminal command reader
void* terminalCommandReader(void *param){
	
	Controller *controller = (Controller *)param;
	char *command = new char[10];

	printHelp();	

	while(1){	

		fscanf(stdin,"%s",command);	

		if(!strcasecmp(command,"quit")){
			break;
		}
		
		if(!strcasecmp(command,"help")){
			printHelp();
		}
	}
	
	delete []command;
	return (void *)0;
}

//to handle the http requests
void* httpRequestHandler(void* param){

	Controller *controller = ((struct ClientInfo*) param)->controller;
	ClientInfo *clientinfo = (ClientInfo *)param;

	bool mimecheck = false;

	MYSQL *dbconnection;
	MYSQL_RES *result;
	MYSQL_ROW row;
	
	char query[350];
	char tempstr[200];
	char *tempstrbig = new char[1500];
	char filepath1[100],filepath2[100];

	char *tempptr;
	int tempnum;

	struct stat filestatbuff;	

	struct TranscodeHandlerDataPipeDataType *transcodedatapipedatatype = new TranscodeHandlerDataPipeDataType();	
	int transcodeindex;


	cout<<"## Inside HTTP Request Handler , Job: To Handle request sent by IPAddress = "<<clientinfo->clientipaddress<<" ##\n\n\n";


/*********   STEP 1: Make connection to the database ****************/
	dbconnection = mysql_init(NULL);

	if (mysql_real_connect(dbconnection,DB_SERVERHOSTNAME,DB_USERNAME,DB_PASSWORD,DB_SCHEMANAME,MYSQL_PORT,NULL,0) == 0 ){
		cout<<"Error--> function: httpRequestHandler, DCMTOL_stat: creating connection to the database, Reason: "<<mysql_error(dbconnection)<<"\n";
		close(clientinfo->clientsockfd);
		delete clientinfo;
		delete transcodedatapipedatatype;
		delete []tempstrbig;
		return (void*) -1;
	}
/*********** Connection made to the database ************/


/********  STEP 2: Read the request from the socket into buffer ***********/
	int clientrequestlen;
	clientrequestlen = 0;
	do{
		clientrequestlen += read(clientinfo->clientsockfd,clientinfo->clientrequest + clientrequestlen,GETREQUEST_LENGTH-clientrequestlen-1);
		clientinfo->clientrequest[clientrequestlen] = '\0';

		tempptr = strstr(clientinfo->clientrequest,"\r\n\r\n");
	}while(tempptr == NULL);

	tempptr = strstr(clientinfo->clientrequest,"GET");
		
	if(tempptr==NULL){
		cout<<"Request Ignored\n";
		close(clientinfo->clientsockfd);
		delete clientinfo;
		delete transcodedatapipedatatype;
		delete []tempstrbig;
		return (void *) -1;
	}

  //reading everything after the Host: ... line into a buffer
	tempptr = strstr(clientinfo->clientrequest,"Host:");
	while(*tempptr != '\n')
		tempptr++;
	tempptr++;

	strcpy(tempstrbig,tempptr);

/********  Request loaded into the buffer **********/


/******** STEP 3: Save the user agent field *******/

	tempptr = strcasestr(clientinfo->clientrequest,"User-Agent: ") + strlen("User-Agent: ");
	tempnum = 0;
	while(*tempptr!='\r'){
		clientinfo->clientuseragent[tempnum++] = *(tempptr++);
	}
	clientinfo->clientuseragent[tempnum] = '\0';

/******* user agent field saved *********/

	//here is the new request
	cout<<clientinfo->clientrequest<<"\n\n";

/******* STEP 4: Detecting the type of the request and parsing it********/

	//STEP 4.1(a) If the request contains the MIME types supported by the client 
	if(	(tempptr = strstr(clientinfo->clientrequest,"mimecheck") )!=NULL){
		clientinfo->hostport = controller->portnumber;
	
		mimecheck = true;	

		strcpy(clientinfo->hostname,"dcmtol");	
		cout<<"Request type MIMECHECK detected\n\n";	
	}
	//STEP 4.1 (b) Else the request is normal HTTP request
	else{
 
		tempptr	= strstr(clientinfo->clientrequest,"GET ") + strlen("GET ");
		tempnum = 0;
		while(*tempptr != ':')
			tempstr[tempnum++] = *(tempptr++);
		tempstr[tempnum] = '\0';

		clientinfo->hostport = checkPort(tempstr);
		tempptr += strlen("://");  
		//parsed http:// or ftp:// till now

		tempnum = 0;	
		while(*tempptr != '/' && *tempptr!=':')
			clientinfo->hostname[tempnum++] = *(tempptr++);
		clientinfo->hostname[tempnum] = '\0';

		tempnum = 0;
		if(*tempptr == ':'){ //port number is exclusively specified
			tempptr++;
			while(*tempptr!='/')
				tempstr[tempnum++] = *(tempptr++);
			tempstr[tempnum] = '\0';
			clientinfo->hostport = atoi(tempstr);    
		}	//else use default port number for that service that was found earlier

		//" GET http://x.y.z.p:m" is parsed till now, so tempptr points to the actual page now

		//if the host name is the current ip address of my machine then 	
		if( !strcmp(clientinfo->hostname,controller->ipaddress) && clientinfo->hostport == controller->portnumber){
			strcpy(clientinfo->hostname,"dcmtol");		
		}

		//STEP 4.1(b).1 Changing the GET request so that it can be forwarded to actual server

		clientinfo->clientrequest[0] = 'G';
		clientinfo->clientrequest[1] = 'E';
		clientinfo->clientrequest[2] = 'T';
		clientinfo->clientrequest[3] = ' ';
			
		tempnum = strlen("GET ");

		while(*tempptr != '\n')
			clientinfo->clientrequest[tempnum++] = *(tempptr++);
		clientinfo->clientrequest[tempnum] = '\0';

		strcat(clientinfo->clientrequest,"\nHost: ");
		strcat(clientinfo->clientrequest,clientinfo->hostname);
		strcat(clientinfo->clientrequest,"\r\n");
		strcat(clientinfo->clientrequest,tempstrbig);
	}

/******* REQUEST PARSED and New request generated to be forwarded to the actual server ********/


/******* STEP 5: Send back the reply to the client *********/

	//STEP 5(a): If mime types were sent in the request
	if(mimecheck==true){ 

		//parse the request 
		FILE *fprawmime,*fpparsed;		
		char *ptr1,*ptr2,*ptr3;
		bool versionover = false,mimeover = false; 

		sprintf(filepath1,"%s/%s%d",TEMP_FILES_PATH,"rawmime",clientinfo->clientsockfd);					
		fprawmime = fopen(filepath1,"w");
	
		sprintf(filepath2,"%s/%s%d",TEMP_FILES_PATH,"parsedmime",clientinfo->clientsockfd);

		fputs(clientinfo->clientrequest,fprawmime);	
		fclose(fprawmime);
	
		sprintf(tempstrbig,"sed 's/%%../ /g' %s | sed 's/ \\+/ /g' > %s",filepath1,filepath2);
		system(tempstrbig);

		//STEP 5(a).1: Insert the mime types into the database
		fpparsed = fopen(filepath2,"r");
		semop(controller->dbsemid,&controller->waitop,1);

			sprintf(query,"delete from VERSION_COMPATIBILITY where clientip = '%s' and useragent = '%s'",clientinfo->clientipaddress,clientinfo->clientuseragent);
			mysql_query(dbconnection,query); 

			sprintf(query,"delete from MIME_CAPABILITY where clientip = '%s' and useragent = '%s'",clientinfo->clientipaddress,clientinfo->clientuseragent);
			mysql_query(dbconnection,query);

			fgets(clientinfo->clientrequest,GETREQUEST_LENGTH - 1,fpparsed);
			ptr1 = strstr(clientinfo->clientrequest,"versions=") + strlen("versions=");
			ptr2 = strchr(ptr1,'|');

			tempnum = 0;
						
			while(ptr2!=NULL){
				while(*ptr1==' ')
					ptr1++;

				while(ptr1!=ptr2){
					tempstr[tempnum] = *ptr1;
					ptr1++;
					tempnum++;
				}
	
				tempstr[tempnum] = '\0';		
				tempnum = 0;
				ptr1++;
				ptr2 = strchr(ptr1,'|');

				if(strcmp(tempstr,"&MIME=")==0){
					versionover = true;
					continue;	
				}
/*				else if(strcmp(tempstr,"&")==0){
					mimeover = true;
					break;
				}*/
				else{
					if(versionover==false){
						ptr3 = strchr(tempstr,'.');
						if(ptr3!=NULL){
							if(*(ptr3-1)>=48 && *(ptr3-1)<=57){
								sprintf(query,"insert into VERSION_COMPATIBILITY values ('%s','%s','%s')",clientinfo->clientipaddress,tempstr,clientinfo->clientuseragent);
								mysql_query(dbconnection,query);
							}
						}
					}
						
					if(versionover==true && mimeover == false){
						if(strchr(tempstr,'/')!=NULL && strcasestr(tempstr,"java")==NULL){
							sprintf(query,"insert into MIME_CAPABILITY values ('%s','%s','%s')",clientinfo->clientipaddress,tempstr,clientinfo->clientuseragent);								
							mysql_query(dbconnection,query);
						}	
					}

					if(versionover && mimeover){
						//put the URL string in database
					}
				}
			}				
			
		semop(controller->dbsemid,&controller->signalop,1);
		fclose(fpparsed);

		remove(filepath1);
		remove(filepath2);
		cout<<"Updated database\n";

		//Inserting mime types into the database is over

		//STEP 5(a).2: find the mimecheck_number to find which page's link to be send and send the Javascript2 in reply 

		tempptr = strstr(clientinfo->clientrequest,"mimecheck_") + strlen("mimecheck_");
		tempnum = 0;
		
		while(*tempptr!='/')
			tempstr[tempnum++] = *(tempptr++);
		tempstr[tempnum] = '\0';

		sprintf(tempstrbig,"sed 's/IPADDRESS/%s:%d/g' %s/jsreply2.js | sed 's/FILENAME/%s%s.html/g' > %s/js2_%d.js",controller->ipaddress,controller->portnumber,JSREPLY2_PATH,"pagesave",tempstr,JSREPLY2_PATH,clientinfo->clientsockfd);
			
		system(tempstrbig);

		sprintf(filepath1,"%s/js2_%d.js",JSREPLY2_PATH,clientinfo->clientsockfd);
		FILE *fpjs2 = fopen(filepath1,"r");

		stat(filepath1,&filestatbuff);
		int contentlength = filestatbuff.st_size;

	 sprintf(clientinfo->clientrequest,"HTTP/1.1 200 OK\r\nDate: Tue, 01 Mar 2011 06:14:58 GMT\r\nAccept-Ranges: bytes\r\nContent-Type: text/javascript\r\nContent-Length: %d\r\nServer: DCMTOL\r\n\r\n",contentlength);
									
		tempnum = strlen(clientinfo->clientrequest);
						
		//sending the changed file to the client
		fread(clientinfo->clientrequest + tempnum,contentlength,1,fpjs2);									
		tempnum = write(clientinfo->clientsockfd,clientinfo->clientrequest,tempnum + contentlength);
		cout<<"sent the reply to the client's 1st Javascript, written "<<tempnum<<" bytes : "<<strerror(errno)<<"\n";
		
		fclose(fpjs2);
		remove(filepath1);
		// the javascript is sent in the reply
	}
	
	//STEP 5(b): if we have to send a normal reply, no mime types were sent
	else{ 
		bool pagenotavailable = false;

		//STEP 5(b).1: if the data is on the local server
		if(strcmp(clientinfo->hostname,"dcmtol")==0){ 
			tempptr = strstr(clientinfo->clientrequest,"GET") + strlen("GET ");
			
			strcpy(filepath1,HTMLPAGE_PATH);			
		
			tempnum = strlen(filepath1);
			while(*tempptr!=' ')
				filepath1[tempnum++] = *(tempptr++);
			filepath1[tempnum] = '\0';
	
			int fd = open(filepath1,O_RDONLY);		
			
			stat(filepath1,&filestatbuff);	
			int totalcnt = filestatbuff.st_size;

			do{
				tempnum = read(fd,clientinfo->clientrequest,GETREQUEST_LENGTH-1);	
				tempnum = write(clientinfo->clientsockfd,clientinfo->clientrequest,tempnum);

				totalcnt = totalcnt - tempnum;
			}while(totalcnt > 0);
		}

		//STEP 5(b).2: if the data is on some other server
		else{  
		
			struct hostent *hostentry;
			struct sockaddr_in sockhost;
		
			hostentry = gethostbyname(clientinfo->hostname);

			if(hostentry==NULL)
				pagenotavailable = true;
			else{
				inet_ntop(AF_INET,(void *) hostentry->h_addr_list[0],clientinfo->hostipaddress,15);
			
				cout<<"IP Address of the host "<<clientinfo->hostname<<" : "<<clientinfo->hostipaddress<<" and port number = "<<clientinfo->hostport<<"\n";
		
				//fill the socket structure of the sockhost
				bzero(&sockhost,sizeof(sockhost));

				sockhost.sin_family = AF_INET;
				inet_pton(AF_INET,clientinfo->hostipaddress,(void *) &(sockhost.sin_addr.s_addr));	
				sockhost.sin_port = htons(clientinfo->hostport);

				clientinfo->hostsockfd = socket(AF_INET,SOCK_STREAM,0);

				if(clientinfo->hostsockfd == -1)
					pagenotavailable = true;			
				else{
					//connect the socket/bind the socket to the remote site
					if(connect(clientinfo->hostsockfd,(struct sockaddr*)&sockhost,sizeof(struct sockaddr)) < 0)
						pagenotavailable = true;
					else{
						cout<<"Connected to the host IP address "<<clientinfo->hostipaddress<<" and port "<<clientinfo->hostport<<"\n";
						char *url	= new char[500];

						tempnum = 0;				
						sprintf(url,"http://%s:%d",clientinfo->hostipaddress,clientinfo->hostport);
						tempnum = strlen(url);
	
						tempptr = strstr(clientinfo->clientrequest,"GET") + strlen("GET ");		
				
						while(*tempptr!=' ')
							url[tempnum++] = *(tempptr++);
						url[tempnum] = '\0';
	
						//if the host server is down
						if(write(clientinfo->hostsockfd,clientinfo->clientrequest,strlen(clientinfo->clientrequest)) == -1){
							pagenotavailable = true;
							delete []url;
						}
						else{ 
							cout<<"Request forwarded to the server at the IP address "<<clientinfo->hostipaddress<<" and port number = "<<clientinfo->hostport<<"\n\n"; 
							cout<<clientinfo->clientrequest<<"\n\n";

							int i;
							int pagesavenum;
							bool mimefound = false;
							char contenttype[50];
							FILE *fpsendhtml = NULL;
							int contentlength;
							int totalcnt = 0;
							bool pagesave = false;
							
							//read the HTTP packet from the server	
							tempnum = read(clientinfo->hostsockfd,clientinfo->clientrequest,GETREQUEST_LENGTH-1);
	
							//Find the content length
							tempptr = strcasestr(clientinfo->clientrequest,"content-length: ");
							tempptr += strlen("content-length: ");		
			
							i = 0;	
							while(*tempptr != '\r')
								contenttype[i++] = *(tempptr++);
							
							contenttype[i] = '\0';
			
							contentlength = atoi(contenttype);						
							
							cout<<"Found the content length to be "<<contentlength<<"\n";
						
							//Find the content length
							tempptr = strstr(clientinfo->clientrequest,"Content-Type");
							tempptr += strlen("Content-Type: ");
						
							i = 0;					
							while(*tempptr != ';' && *tempptr != '\r')
								contenttype[i++] = *(tempptr++);
							
							contenttype[i] = '\0';
				
							cout<<"Found the content-type to be "<<contenttype<<"\n";

							if(strstr(contenttype,"text/")!=NULL || !strcmp(contenttype,"application/octet-stream") || strstr(contenttype,"image/")!=NULL){
								mimefound = true;
	
								if(!strcmp(contenttype,"text/html")){
									pagesave = true;
							
									//check the database ka time stamp...if u find it is not over then pagesave = false
									//find the current time	
									time_t curtime;
									time(&curtime);
									semop(controller->dbsemid,&controller->waitop,1);
																	
										sprintf(query,"select timeupdate from TIMEOUT where clientip = '%s' and useragent = '%s'",clientinfo->clientipaddress,clientinfo->clientuseragent);
										mysql_query(dbconnection,query);
										result = mysql_store_result(dbconnection);									
								
										row = mysql_fetch_row(result);
										if(row != NULL){		
											if((curtime - atol(row[0])) >  1800){
												pagesave = true;
	
												sprintf(query,"delete from TIMEOUT where clientip = '%s' and useragent = '%s'",clientinfo->clientipaddress,clientinfo->clientuseragent);	
												mysql_query(dbconnection,query);

												sprintf(query,"insert into TIMEOUT values ('%s','%ld','%s')",clientinfo->clientipaddress,curtime,clientinfo->clientuseragent);
												mysql_query(dbconnection,query);
											}				
											else
												pagesave = false;
										}
										else{
											pagesave = true;
											sprintf(query,"insert into TIMEOUT values ('%s','%ld','%s')",clientinfo->clientipaddress,curtime,clientinfo->clientuseragent);
											mysql_query(dbconnection,query);
										}

										mysql_free_result(result);
									semop(controller->dbsemid,&controller->signalop,1);	

									cout<<"Check the database for timeout value: its "<<(pagesave==true?"required":"not required")<<"\n";

								}
							}
							else{
								//compare the content type with those in the database
								sprintf(query,"select mime from MIME_CAPABILITY where clientip = '%s' and useragent = '%s'",clientinfo->clientipaddress,clientinfo->clientuseragent);
			
								semop(controller->dbsemid,&controller->waitop,1);
									mysql_query(dbconnection,query);
									result = mysql_store_result(dbconnection);	
							
									while((row = mysql_fetch_row(result))){
										if(strcmp(contenttype,row[0]) == 0){
											mimefound = true;
											break;
										}
									}		
								semop(controller->dbsemid,&controller->signalop,1);

								mysql_free_result(result);
								if(mimefound)
									cout<<"The content type "<<contenttype<<" is supported by the client\n";
								else
									cout<<"The content type "<<contenttype<<" is not supported by the client\n";
	
							}

							if(pagesave){
								pagesavenum = rand();
							
								sprintf(filepath1,"%s/pagesave%d.html",HTMLPAGE_PATH,pagesavenum);
								fpsendhtml = fopen(filepath1,"w");														
	
								cout<<"page is to be saved and java scripts are to be sent\n";
							}

							tempptr = strstr(clientinfo->clientrequest,"\r\n\r\n") + strlen("\r\n\r\n");
							totalcnt = tempnum - (int) (tempptr - clientinfo->clientrequest);							
					
							if(mimefound){			

								while(1){
	
									cout<<clientinfo->clientrequest<<endl;
	
									if(pagesave==true)
										fwrite(clientinfo->clientrequest,tempnum,1,fpsendhtml);
									else{
										cout<<"had read "<<tempnum<<" bytes : about to write them on the socket\n";
										tempnum = write(clientinfo->clientsockfd,clientinfo->clientrequest,tempnum);
										cout<<"Successfully written "<<tempnum<<" bytes to the client socket totalcnt = "<<totalcnt<<"\n";
									}							
						
									if(totalcnt >= contentlength){
										if(pagesave)
											fclose(fpsendhtml);
										break;
									}
									
									tempnum = read(clientinfo->hostsockfd,clientinfo->clientrequest,GETREQUEST_LENGTH-1);
									totalcnt += tempnum;
								}
						
								if(pagesave == true){
	
									//make changes in the file written by toshish's and find its size and make a good header and forward it to the client
									sprintf(tempstrbig,"sed 's/IPADDRESS/%s:%d/g' %s/jsreply1.html | sed 's/PAGESAVE_NUMBER/%d/g' > %s/js1_%d.html",controller->ipaddress,controller->portnumber,JSREPLY1_PATH,pagesavenum,JSREPLY1_PATH,clientinfo->clientsockfd);	
									system(tempstrbig);
	
									sprintf(filepath1,"%s/js1_%d.html",JSREPLY1_PATH,clientinfo->clientsockfd);
									fpsendhtml = fopen(filepath1,"r");															
		
									stat(filepath1,&filestatbuff);
									contentlength = filestatbuff.st_size;

									sprintf(clientinfo->clientrequest,"HTTP/1.1 200 OK\r\nDate: Tue, 01 Mar 2011 06:14:58 GMT\r\nAccept-Ranges: bytes\r\nContent-Type: text/html\r\nContent-Length: %d\r\nServer: DCMTOL\r\n\r\n",contentlength);
										
									tempnum = strlen(clientinfo->clientrequest);
						
									//sending the changed file to the client
									fread(clientinfo->clientrequest + tempnum,contentlength,1,fpsendhtml);							
																		
									write(clientinfo->clientsockfd,clientinfo->clientrequest,tempnum + contentlength);
									fclose(fpsendhtml);
									cout<<"\n\nWritten all the data\n";
								}						
							}
							else{	//if the mime type is not found
		
								int streamfilefd = -1;
					
								char transcodeformat[40];
								//webm, ogg, mp4, 
								strcpy(transcodeformat,"video/ogg");	
	
								cout<<"Demand for video of the type "<<contenttype<<endl;
								//check if a transcoded file already exists in the system
								sprintf(query,"select * from FILES where url = '%s'",url);		
	
								mysql_query(dbconnection,query);					
								result = mysql_store_result(dbconnection);	
									
								while((row = mysql_fetch_row(result))){
									if(!strcmp(row[2],transcodeformat)){
										mimefound = true;	
										break;	
									}
								}
								
								if(mimefound)
									streamfilefd = open(row[0],O_RDONLY);									

								mysql_free_result(result);
					
								if(!mimefound){
									//start transcoding after selecting a particular format
									transcodeindex = controller->Qdelete();
									transcodedatapipedatatype->transcodeindex = transcodeindex;		
									transcodedatapipedatatype->msgcode = START_TRANSCODING;
			
									//what we will do is have some top 5-10 mime types which we will serially try to find out	
									strcpy((controller->transcodetable + transcodeindex)->url,url);
									strcpy((controller->transcodetable + transcodeindex)->inputmime,contenttype);
									strcpy((controller->transcodetable + transcodeindex)->outputmime,transcodeformat);
									controller->copyTranscodeEntryStructure(&(transcodedatapipedatatype->transcodeentry),(controller->transcodetable + transcodeindex));

									write(controller->transcodehandlerdatapipefd,transcodedatapipedatatype,TRANSCODEHANDLER_DATAPIPE_DATASIZE);
	
									semop(controller->transcodetablesemid,&(controller->transcodetable + transcodeindex)->waitop,1);
									
									cout<<"reply code Got the signal that transcoding has started---about to send\n";						
									if((controller->transcodetable + transcodeindex)->transcodingstarted == true){
										
										sprintf(tempstrbig,"%s/%s",TRANSCODED_VIDEO_PATH,(controller->transcodetable + transcodeindex)->transcodedfilename);
										sleep(2);
										streamfilefd = open(tempstrbig,O_RDONLY);
							
										sprintf(query,"insert into FILES values ('%s/%s','%s','%s',0,0,0)",TRANSCODED_VIDEO_PATH,(controller->transcodetable + transcodeindex)->transcodedfilename,url,transcodeformat);

										mysql_query(dbconnection,query);			
									}
								}
			
								if(streamfilefd!=-1){
									
									int readcnt,writecnt,cnt;
	
									//stream from file											
									sprintf(tempstrbig,"HTTP/1.0 200 OK\r\nDate: Tue, 01 Mar 2011 06:14:58 GMT\r\nConnection: close\r\nCache-control: private\r\nContent-type: %s\r\nServer: lighttpd/1.4.26\r\n\r\n",transcodeformat);

									cnt = send(clientinfo->clientsockfd,tempstrbig,strlen(tempstrbig),0);
									printf("Sent this reply : \n%s \nwith size = %d : %s\n",tempstrbig,cnt,strerror(errno));

									cnt = 0;	
									while(1){
										readcnt = read(streamfilefd,clientinfo->clientrequest,2000);
										printf("Read %d bytes from the file : %s\n",readcnt,strerror(errno));		

										if(cnt==7)
											break;
										else if(cnt > 0 && readcnt != 0)
											cnt = 0;
										else if(cnt >=0 && readcnt == 0){
											sleep(1);
											cnt++;
											continue;
										}
		
										writecnt = send(clientinfo->clientsockfd,clientinfo->clientrequest,readcnt,0);
										printf("Data written = %d bytes: %s\n",writecnt,strerror(errno));
	
										if(writecnt <= 0)
											break;										
									}
								}
								else{
									//forward the actual stream...its not gonna work as well, not gonna be played...so no point in forwarding the stream
								}								
							}								
						}
						delete []url;
					}
				}
			}			
		}

		if(pagenotavailable == true){
				sprintf(filepath1,"%s/%s",HTMLPAGE_PATH,NOTFOUND_FILENAME);
				int filefd = open(filepath1,O_RDONLY);		
				
				while((tempnum = read(filefd,clientinfo->clientrequest,GETREQUEST_LENGTH-1)) > 0 )
					write(clientinfo->clientsockfd,tempstrbig,tempnum);	

				close(filefd);
		}
	}	

	close(clientinfo->clientsockfd);	
	close(clientinfo->hostsockfd);
	
	cout<<"Request by the client IP: "<<clientinfo->clientipaddress<<" for Host: "<<clientinfo->hostname<<" at the Host-Port: "<<clientinfo->hostport<<" replied back: \n";

	delete clientinfo;
	delete transcodedatapipedatatype;
	delete []tempstrbig;
	return (void *)0;
}

/*************
GENERIC: many such server Handler threads would be running together
This function is to handle HTTP requests on port 80
*************/
void* serverHandler(void* param){

	Controller *controller = (Controller *) param;
	struct sockaddr_in sockclient;
	unsigned int sockclientlen = sizeof(sockclient);
	int clientsocketfd;
	int temptranscodeindex;
	ClientInfo *clientinfo;

	cout<<"********** DCMTOL SERVER STARTED, Job: To accept requests from clients **********\n";

	//code to continuously listen to all  the requests
	while(controller->serverrunning){

		clientsocketfd = accept(controller->serversocketfd,(struct sockaddr*)&sockclient,&sockclientlen);

		if(clientsocketfd == -1){
			cout<<"Error--> function: serverHandler, DCMTOL_stat: clientsocketfd == -1, Reason: "<<strerror(errno)<<"\n";
		}
		else{
			clientinfo = new struct ClientInfo();
			clientinfo->controller = controller;
				
			clientinfo->clientsockfd = clientsocketfd;
			inet_ntop(AF_INET,(void*) &sockclient.sin_addr.s_addr,clientinfo->clientipaddress,15);
			clientinfo->clientport = ntohs(sockclient.sin_port);
			
			cout<<"Request received from "<<clientinfo->clientipaddress<<"\n";			
	
			if(pthread_create(new pthread_t,NULL,httpRequestHandler,(void*)clientinfo)!=0){
				cout<<"Error--> function: serverHandler, DCMTOL_stat: while creating thread for httprequesthandler, Reason: "<<strerror(errno)<<"\n";
			
				close(clientsocketfd);
				delete clientinfo;
			}				
		}		
	}
	
	return (void *)0;
}

/***************
function to read the replies from Transcode Handler 
***************/
void* transcodeControlPipeListener(void* param){

	Controller *controller = (Controller *) param;
	char sedstr[200];
	int tempnum,clientsockfd;
	bool transcodecontrolpipelistenershutdown = false;

	//open the pipes
	controller->transcodehandlerdatapipefd = open(TRANSCODEHANDLER_DATAPIPE_PATH,O_WRONLY);
	controller->transcodehandlercontrolpipefd = open(TRANSCODEHANDLER_CONTROLPIPE_PATH,O_RDONLY);

	//now listen to the control pipe
	while(!transcodecontrolpipelistenershutdown){
		
		read(controller->transcodehandlercontrolpipefd,controller->transcodecontrolpipedatatype,TRANSCODEHANDLER_CONTROLPIPE_DATASIZE);

		switch(controller->transcodecontrolpipedatatype->msgcode){
			
			case TRANSCODING_STARTED:	
										(controller->transcodetable + controller->transcodecontrolpipedatatype->transcodeindex)->transcodingstarted = true;
										strcpy((controller->transcodetable + controller->transcodecontrolpipedatatype->transcodeindex)->transcodedfilename,controller->transcodecontrolpipedatatype->transcodedfilename);		
										strcpy((controller->transcodetable + controller->transcodecontrolpipedatatype->transcodeindex)->outputmime,controller->transcodecontrolpipedatatype->outputmime);	
										semop(controller->transcodetablesemid,&(controller->transcodetable + controller->transcodecontrolpipedatatype->transcodeindex)->signalop,1);
				
										cout<<"Got the signal that transcoding has started\n";						
										break;

			case TRANSCODING_KILLED:	
										(controller->transcodetable + controller->transcodecontrolpipedatatype->transcodeindex)->transcodingstarted = false;
										controller->Qinsert(controller->transcodecontrolpipedatatype->transcodeindex);	
										break;

			case TRANSCODING_OVER:	
										(controller->transcodetable + controller->transcodecontrolpipedatatype->transcodeindex)->transcodingstarted = false;
										controller->Qinsert(controller->transcodecontrolpipedatatype->transcodeindex);	
										break;

			case TRANSCODING_FAILED:
										(controller->transcodetable + controller->transcodecontrolpipedatatype->transcodeindex)->transcodingstarted = false;
										controller->Qinsert(controller->transcodecontrolpipedatatype->transcodeindex);	
										break;

			case CLEAR_FOR_SHUTDOWN:	transcodecontrolpipelistenershutdown = true;	
										break;

			default: cout<<"Controller Recieved unknown msg from TranscodeHandler\n";
		}	
	}

	cout<<"Controller got the signalop that Transcode Handler is shutting down\n";
	return (void *) 0;
}

/****************
DCMTOL's main function : heart of the project
****************/
int main(int argc,char *argv[]){

	Controller *c1 = new Controller();
	
	srand((unsigned)time(NULL));

	c1->initializeController(&argv);
	c1->createPipes();
	c1->initializeTranscodeHandler();
	
	if(c1->createServerSocket(c1) == false){
		c1->exitClean(SERVER_START_FAIL);
		return SERVER_START_FAIL;
	}
	
	c1->createThreads();
	c1->exitClean(NORMAL_EXIT);

	return NORMAL_EXIT;
}

void Controller::exitClean(int code){

	struct TranscodeHandlerDataPipeDataType temptranscodedatapipedatatype;
	struct TranscodeHandlerControlPipeDataType temptranscodecontrolpipedatatype; 

	cout<<"DCMTOL shutting down:";

	switch(code){

		case SERVER_START_FAIL: cout<<"Server Start Fail\n";
														break;
		case NORMAL_EXIT:	cout<<"Normal Exit\n";
											break;

		default: cout<<"Unknown Signal Recieved\n";

	}

	//terminate the server if its running
	if(serverrunning){

		cout<<"Controller sending the shutdown signalop\n";	
		serverrunning = false;
		temptranscodedatapipedatatype.msgcode = SHUTDOWN;
		write(transcodehandlerdatapipefd,(void *)&temptranscodedatapipedatatype,TRANSCODEHANDLER_DATAPIPE_DATASIZE);	

		cout<<"Controller waitoping for TranscodeHandler to shutdown\n";
		pthread_join(threadtranscodecontrolpipehandler,NULL);
		cout<<strerror(errno)<<"\n";			
		cout<<"Terminated the server\n";
		
	}
	else{
		
		cout<<"Controller sending the shutdown signalop\n";	

		//open the pipes
		transcodehandlerdatapipefd = open(TRANSCODEHANDLER_DATAPIPE_PATH,O_WRONLY);
		transcodehandlercontrolpipefd = open(TRANSCODEHANDLER_CONTROLPIPE_PATH,O_RDONLY);
		
		cout<<"Controller waitoping for TranscodeHandler to shutdown\n";
		temptranscodedatapipedatatype.msgcode = SHUTDOWN;
		write(transcodehandlerdatapipefd,(void *)&temptranscodedatapipedatatype,TRANSCODEHANDLER_DATAPIPE_DATASIZE);	
		read(transcodehandlercontrolpipefd,(void *)&temptranscodecontrolpipedatatype,TRANSCODEHANDLER_CONTROLPIPE_DATASIZE);

	}

	sleep(1);	

	delete []transcodetable;
	delete []transcodetablequeue;

	delete transcodecontrolpipedatatype;

	close(transcodehandlerdatapipefd);
	close(transcodehandlercontrolpipefd);
	close(serversocketfd);

	cout<<"Controller shutting down\n";	
	//remove all the temporary files
}

void Controller::copyTranscodeEntryStructure(struct TranscodeEntryStructure *dest,struct TranscodeEntryStructure *src){

	dest->transcodeindex = src->transcodeindex;
	
	strcpy(dest->url,src->url);
	strcpy(dest->inputmime,src->inputmime);
	strcpy(dest->outputmime,src->outputmime);

	dest->transcodingstarted = src->transcodingstarted;
 	strcpy(dest->transcodedfilename,src->transcodedfilename);

}

void Controller::Qinsert(int transcodeindex){

	semop(queuesemid,&waitop,1);
		rear = (rear + 1) % SERVER_MAXREQUESTHANDLE_COUNT;

    //after incrementing the front if the following holds true then the queue has become empty
    if(transcodetableempty == true){
    	transcodetableempty = false;
    }

	transcodetablequeue[rear] = transcodeindex;
 	semop(queuesemid,&signalop,1);

	cout<<"rear = "<<rear<<"\tfront = "<<front<<"\n";

}

int Controller::Qdelete(){
	
	int retval;

	semop(queuesemid,&waitop,1);
		
		retval = transcodetablequeue[front];
		front = (front + 1) % SERVER_MAXREQUESTHANDLE_COUNT;

		if(front == (rear + 1) % SERVER_MAXREQUESTHANDLE_COUNT){
			transcodetableempty = true;																							
		}

	semop(queuesemid,&signalop,1);
	cout<<"rear = "<<rear<<"\tfront = "<<front<<"\n";	
	return retval;
}

/****************** 
 To create a connection to the Database and to create the Transcode Handler process 
******************/
void Controller::initializeTranscodeHandler(){

	//start the child process TransodeHandler
	transcodehandlerprocessid = fork();

	if(transcodehandlerprocessid==0){
		execl(TRANSCODEHANDLER_PATH,TRANSCODEHANDLER_PATH,NULL);
	}
}

void Controller::createSemaphore(){

	queuesemid = semget(QUEUESEMKEY,1,IPC_CREAT); 
	if(queuesemid == -1)
		cout<<"Error--> function: createSemaphore, DCMTOL_stat: queuesemid == -1, Reason: "<<strerror(errno)<<"\n";

	dbsemid = semget(DBSEMKEY,1,IPC_CREAT);
	if(dbsemid == -1)
		cout<<"Error--> function: createSemaphore, DCMTOL_stat: dbsemid == -1, Reason: "<<strerror(errno)<<"\n";

	transcodetablesemid = semget(TRANSCODESEMKEY,TRANSCODE_MAXREQUESTHANDLE_COUNT,IPC_CREAT);
	if(transcodetablesemid == -1)
		cout<<"Error--> function: createSemaphore, DCMTOL_stat: transcodetablesemid == -1, Reason: "<<strerror(errno)<<"\n";

	//initialise the semaphore values
	semctl(queuesemid,0,SETVAL,1);  
	semctl(dbsemid,0,SETVAL,1);

	//define the operations
	waitop.sem_num = 0;
	waitop.sem_op = -1;
	waitop.sem_flg = SEM_UNDO;

	signalop.sem_num = 0;
	signalop.sem_op = 1;
	signalop.sem_flg = SEM_UNDO;
}

bool ServerSocket::createServerSocket(Controller *controller){

	struct sockaddr_in sockserver;
	char serverip[16];

	serverrunning = false;

	bzero(&sockserver,sizeof(struct sockaddr_in));
	serversocketfd = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);  //for rtp I think we have to use the SOCK_DGRAM type of socket

	if(serversocketfd==-1){
		cout<<"Error--> function: createServerSocket, DCMTOL_stat: serversocketfd == -1, Reason: "<<strerror(errno)<<"\n";
		return serverrunning;
	}

	sockserver.sin_family = AF_INET;
	sockserver.sin_port = htons(portnumber);
	inet_pton(AF_INET,ipaddress,(void *)&sockserver.sin_addr.s_addr);
	//sockserver.sin_addr.s_addr = htonl(INADDR_ANY);  //pseudo interface which listens requests coming on every interface available

	//bind to the socket now
	if(bind(serversocketfd,(struct sockaddr*)&sockserver,sizeof(sockserver)) == -1){
		cout<<"Error--> function: createServerSocket, DCMTOL_stat: binding socket with IP: "<<inet_ntop(AF_INET,(void*) &sockserver.sin_addr.s_addr,serverip,15)<<" and Port: "<<sockserver.sin_port<<" ; Reason: "<<strerror(errno)<<"\n";
		return serverrunning;
	}

	if(listen(serversocketfd,SOCKET_QUEUESIZE)==-1){
		cout<<"Error--> function: createServerSocket, DCMTOL_stat: calling listen, Reason: "<<strerror(errno)<<"\n";
		return serverrunning;
	}
	
	serverrunning = true;
	return serverrunning;
}

void Controller::createThreads(){

	if(pthread_create(&threadterminalcommandreader,NULL,terminalCommandReader,(void*)this)!=0)
		cout<<"Error--> function: createThreads, DCMTOL_stat: creating Terminal Command Reader, Reason: "<<strerror(errno)<<"\n";
	
	if(pthread_create(&threadtranscodecontrolpipehandler,NULL,transcodeControlPipeListener,(void *)this)!=0)
		cout<<"Error--> function: createThreads, DCMTOL_stat: creating Transcode-ControlPipe Handler, Reason: "<<strerror(errno)<<"\n";
	
	if(pthread_create(&threadserver,NULL,serverHandler,(void*)this)!=0)
		cout<<"Error--> function: createThreads, DCMTOL_stat: Server Handler, Reason: "<<strerror(errno)<<"\n";
	
	pthread_join(threadterminalcommandreader,NULL);
}

void Controller::createPipes(){
	if(access(TRANSCODEHANDLER_DATAPIPE_PATH,F_OK) == -1 ){
		if(mkfifo(TRANSCODEHANDLER_DATAPIPE_PATH,0666) == -1){
			cout<<"Error--> function: createPipes, DCMTOL_stat: creating Transcode Handler Data Pipe, Reason: "<<strerror(errno)<<"\n";
		}
	}

	if(access(TRANSCODEHANDLER_CONTROLPIPE_PATH,F_OK) == -1 ){
		if(mkfifo(TRANSCODEHANDLER_CONTROLPIPE_PATH,0666) == -1){
			cout<<"Error--> function: createPipes, DCMTOL_stat: creating Transcode Handler Control Pipe, Reason: "<<strerror(errno)<<"\n";
		}
	}
}

void Controller::initializeController(char ***argv){

	int i;
	MYSQL *dbconnection;
	MYSQL_RES *result;
	MYSQL_ROW row;

	char tempstr[1000];

	signal(SIGPIPE,SIG_IGN);
	printf("Overriding signalop handler for SIGPIPE : %s\n",strerror(errno));

	cout<<"Process group id of Controller: "<<getpgid(getpid())<<"\n";
	
	transcodecontrolpipedatatype = new struct TranscodeHandlerControlPipeDataType();

	transcodetable = new struct TranscodeEntryStructure[TRANSCODE_MAXREQUESTHANDLE_COUNT];
	transcodetablequeue = new int[TRANSCODE_MAXREQUESTHANDLE_COUNT];

	for(i=0;i<TRANSCODE_MAXREQUESTHANDLE_COUNT;i++){
		transcodetablequeue[i] = i;
	}

	front = 0;
	rear = TRANSCODE_MAXREQUESTHANDLE_COUNT-1;

	cout<<"rear = "<<rear<<" front = "<<front<<"\n";
	transcodetableempty = false;

	createSemaphore();

	for(i=0;i<TRANSCODE_MAXREQUESTHANDLE_COUNT;i++){
		(transcodetable + i)->transcodeindex = i;
	
		(transcodetable + i)->waitop.sem_num = i;
		(transcodetable + i)->waitop.sem_op = -1;
		(transcodetable + i)->waitop.sem_flg = SEM_UNDO;
		
		(transcodetable + i)->signalop.sem_num = i;
		(transcodetable + i)->signalop.sem_op = 1;
		(transcodetable + i)->signalop.sem_flg = SEM_UNDO;

		semctl(transcodetablesemid,i,SETVAL,0);
	}

	//Maintain database consistency by checking the files present on the hard disk
	dbconnection = mysql_init(NULL);

	if (mysql_real_connect(dbconnection,DB_SERVERHOSTNAME,DB_USERNAME,DB_PASSWORD,DB_SCHEMANAME,MYSQL_PORT,NULL,0) == 0 ) 
		cout<<"Error--> function: initializeController, DCMTOL_stat: creating connection to the database, Reason: "<<mysql_error(dbconnection)<<"\n";

	mysql_query(dbconnection,"select * from FILES");
	result = mysql_store_result(dbconnection);	
										
	while((row = mysql_fetch_row(result))){
		
		if((i = open(row[0],O_RDONLY)) == -1){
			sprintf(tempstr,"delete from FILES where filename = '%s'",row[0]);
			mysql_query(dbconnection,tempstr);
		}
		else
			close(i);
	}

	mysql_free_result(result);	
	/* set the port number and the ip address */
	portnumber = atoi((*argv)[2]);  
/*	char *device;
	strcpy(device,(*argv)[1]);*/
	strcpy(ipaddress,(*argv)[1]);

}	
