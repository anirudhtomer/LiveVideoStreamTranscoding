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

	struct sembuf wait,signal;

	pid_t transcodehandlerprocessid;
	struct TranscodeEntryStructure *transcodetable;

	int transcodetablesemid,dbsemid,queuesemid;

	int *transcodetablequeue;
	int addjssize,jsreplysize;

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

	int clientrequestlen;
	bool GETrequest = false;
	bool mimecheck = false,localrequest = false;

	char tempstr[20];
	char *tempstrbig = new char[1500];

	char *tempptr;
	char tempchar;
	int tempnum;
	int webpagefd;

	struct TranscodeHandlerDataPipeDataType *transcodedatapipedatatype = new TranscodeHandlerDataPipeDataType();	
	int transcodeindex;

	MYSQL *dbconnection;

	//Make connection to the database
	dbconnection = mysql_init(NULL);

	if (mysql_real_connect(dbconnection,DB_SERVERHOSTNAME,DB_USERNAME,DB_PASSWORD,DB_SCHEMANAME,MYSQL_PORT,NULL,0) == 0 ) 
		cout<<"Error--> function: httpRequestHandler, DCMTOL_stat: creating connection to the database, Reason: "<<mysql_error(dbconnection)<<"\n";

	cout<<"## Inside HTTP Request Handler , Job: To Handle request sent by IPAddress = "<<clientinfo->clientipaddress<<" ##\n";

	//read the request from the socket into the buffer
	clientrequestlen = 0;
	do{
		clientrequestlen += read(clientinfo->clientsockfd,clientinfo->clientrequest + clientrequestlen,GETREQUEST_LENGTH-clientrequestlen-1);
		cout<<"clientrequest length = "<<clientrequestlen<<endl;
		clientinfo->clientrequest[clientrequestlen] = '\0';

		tempptr = strstr(clientinfo->clientrequest,"GET");
		if(tempptr != NULL)
			GETrequest = true;					
		else{
			cout<<"Request Ignored\n";
			close(clientinfo->clientsockfd);
			delete clientinfo;
			delete transcodedatapipedatatype;
			delete []tempstrbig;
			return (void *) -1;
		}

		write(1,clientinfo->clientrequest,clientrequestlen);
	
		tempptr = strstr(clientinfo->clientrequest,"Host");
	}while(tempptr == NULL);

	while(*tempptr != '\n')
		tempptr++;
	tempptr++;

	strcpy(tempstrbig,tempptr);

	//parse the buffer which contains the request					
	if(GETrequest==true)
		tempptr = strstr(clientinfo->clientrequest,"GET");
	else
		tempptr = strstr(clientinfo->clientrequest,"POST");
	
	//***************  EVERYTHING AHEAD IS DONE ASSUMING IT A GET REQUEST ***********
	tempptr = tempptr + strlen("GET ");

	//if looking for a page on the proxy server
	if(	(tempptr = strstr(tempptr,"mimecheck") )!=NULL){
		clientinfo->hostport = controller->portnumber;
	
		mimecheck = true;	

		strcpy(clientinfo->hostname,"dcmtol");		
	}
	else{ // page on some other server
		tempptr	= strstr(clientinfo->clientrequest,"GET ") + strlen("GET ");
		tempnum = 0;
		while(*tempptr != ':')
			tempstr[tempnum++] = *(tempptr++);
		tempstr[tempnum] = '\0';

		clientinfo->hostport = checkPort(tempstr);
		tempptr += strlen("://");  //parsed http:// or ftp:// till now

		tempnum = 0;	
		while(*tempptr != '/' && *tempptr!=':')
			clientinfo->hostname[tempnum++] = *(tempptr++);
		clientinfo->hostname[tempnum] = '\0';

		//if the host name is the current ip address of my machine then 	
		if(!strcmp(clientinfo->hostname,controller->ipaddress)){
			strcpy(clientinfo->hostname,"dcmtol");		
		}

		tempnum = 0;
		if(*tempptr == ':'){ //port number is exclusively specified
			tempptr++;
			while(*tempptr!='/')
				tempstr[tempnum++] = *(tempptr++);
			tempstr[tempnum] = '\0';
			clientinfo->hostport = atoi(tempstr);    
		}	//else use default port number for that service that was found earlier

		clientinfo->clientrequest[0] = 'G';
		clientinfo->clientrequest[1] = 'E';
		clientinfo->clientrequest[2] = 'T';
		clientinfo->clientrequest[3] = ' ';
			
		tempnum = strlen("GET ");

		while(*tempptr != '\n')
			clientinfo->clientrequest[tempnum++] = *(tempptr++);
		clientinfo->clientrequest[tempnum] = '\0';

/*		//code to replace HTTP/1.1
		tempptr = strstr(clientinfo->clientrequest,"HTTP/1.") + strlen("HTTP/1.");
		*tempptr = '0';*/

		cout<<clientinfo->clientrequest<<"\n";
	
		strcat(clientinfo->clientrequest,"\nHost: ");
		strcat(clientinfo->clientrequest,clientinfo->hostname);
		strcat(clientinfo->clientrequest,"\r\n");
		strcat(clientinfo->clientrequest,tempstrbig);
	}

	/************* CODE TO SEND THE REPLY TO THE CLIENT ****************/
	if(mimecheck==true){ 

		//parse the request 
		char requestfilename[100],dumpfilename[100];
		FILE *fprequest,*fpdump;		
		char *ptr1,*ptr2,*ptr3;
		char halfstr[200];
		int halfstrcnt;
		bool versionover = false,mimeover = false; 
		MYSQL_RES *result;
		MYSQL_ROW row;
		char query[150];
	
		cout<<"Entered mimecheck\n";
	
		sprintf(requestfilename,"%s/%s%d",TEMP_FILES_PATH,"request",clientinfo->clientsockfd);					
		sprintf(dumpfilename,"%s/%s%d",TEMP_FILES_PATH,"dump",clientinfo->clientsockfd);

		cout<<requestfilename<<"\t\t"<<dumpfilename<<"\n\n";
		fprequest = fopen(requestfilename,"w");
		fputs(clientinfo->clientrequest,fprequest);	
		fclose(fprequest);
	
		sprintf(tempstrbig,"sed 's/%%../ /g' %s | sed 's/ \\+/ /g' > %s",requestfilename,dumpfilename);
		system(tempstrbig);

		fpdump = fopen(dumpfilename,"r");
		/* create entry in database for the URL's and clientid */

		semop(controller->dbsemid,&controller->wait,1);

			sprintf(query,"delete from VERSION_COMPATIBILITY where clientip = '%s'",clientinfo->clientipaddress);
			mysql_query(dbconnection,query);

			sprintf(query,"delete from MIME_CAPABILITY where clientip = '%s'",clientinfo->clientipaddress);
			mysql_query(dbconnection,query);

			fgets(clientinfo->clientrequest,GETREQUEST_LENGTH - 1,fpdump);
			ptr1 = strstr(clientinfo->clientrequest,"versions=") + strlen("versions=");
			ptr2 = strchr(ptr1,'|');

			halfstrcnt = 0;
						
			while(ptr2!=NULL){
				while(*ptr1==' ')
					ptr1++;

				while(ptr1!=ptr2){
					halfstr[halfstrcnt] = *ptr1;
					ptr1++;
					halfstrcnt++;
				}
	
				halfstr[halfstrcnt] = '\0';		
				halfstrcnt = 0;
				ptr1++;
				ptr2 = strchr(ptr1,'|');

				if(strcmp(halfstr,"&MIME=")==0){
					versionover = true;
					continue;	
				}
/*				else if(strcmp(halfstr,"&")==0){
					mimeover = true;
					break;
				}*/
				else{
					if(versionover==false){
						ptr3 = strchr(halfstr,'.');
						if(ptr3!=NULL){
							if(*(ptr3-1)>=48 && *(ptr3-1)<=57){
								sprintf(query,"insert into VERSION_COMPATIBILITY values ('%s','%s')",clientinfo->clientipaddress,halfstr);
								mysql_query(dbconnection,query);
							}
						}
					}
						
					if(versionover==true && mimeover == false){
						if(strchr(halfstr,'/')!=NULL && strcasestr(halfstr,"java")==NULL){
							sprintf(query,"insert into MIME_CAPABILITY values ('%s','%s')",clientinfo->clientipaddress,halfstr);								
							mysql_query(dbconnection,query);
						}	
					}

					if(versionover && mimeover){
						//put the URL string in database
					}
				}
			}				
			
		semop(controller->dbsemid,&controller->signal,1);
		fclose(fpdump);

		cout<<"Updated database\n";
		sleep(2);  //easiest way to test the code.
	
		char jsfilename[50];
			
		sprintf(jsfilename,"%s/tempjs.js",HOME_DIR_PATH);
		int jsfd = open(jsfilename,O_RDONLY);
	
		if(jsfd != -1){
			int jscnt = read(jsfd,tempstrbig,1500);
			write(clientinfo->clientsockfd,tempstrbig,jscnt);	
		}
			
		//delete the temporary files
		remove(requestfilename);
		remove(dumpfilename);
	}
	else{ 
		//its a normal request	
		
		char filepath[200];
		bool pagenotavailable = false;
		int filefd;
		int cnt;

		//if the data is on the local server
		if(strcmp(clientinfo->hostname,"dcmtol")==0){ 
			tempptr = strstr(clientinfo->clientrequest,"GET") + strlen("GET ");
			
			strcpy(filepath,HOME_DIR_PATH);			
		
			tempnum = strlen(filepath);
			while(*tempptr!=' ')
				filepath[tempnum++] = *(tempptr++);
			filepath[tempnum] = '\0';
			
			if((strlen(filepath) - strlen(HOME_DIR_PATH))==1)
				strcat(filepath,DEFAULT_DCMTOL_FILEPATH);				
			
			filefd = open(filepath,O_RDONLY);
	
			if(filefd == -1)
				pagenotavailable = true;
			else{	
				while((cnt = read(filefd,tempstrbig,1499))>0)
					write(clientinfo->clientsockfd,tempstrbig,cnt);	
			}
		}
		else{  //else the data is on some other server
		
			struct hostent *hostentry;
			struct sockaddr_in sockhost;
		
			hostentry = gethostbyname(clientinfo->hostname);

			if(hostentry==NULL)
				pagenotavailable = true;
			else{
				inet_ntop(AF_INET,(void *) hostentry->h_addr_list[0],clientinfo->hostipaddress,15);
			
				cout<<"IP Address of "<<clientinfo->hostname<<" : "<<clientinfo->hostipaddress<<" and port number = "<<clientinfo->hostport<<"\n";
		
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
						//check the table of the database to get the already existing files					
						MYSQL_RES *result;
						MYSQL_ROW row;
						char query[600];
						int urlcnt = 0;
						char *ptr;
						char *url	= new char[500];

						ptr = strstr(clientinfo->clientrequest,"GET") + strlen("GET ");		

						while(*ptr!='\r'){
							*(url + urlcnt) = *ptr;
							urlcnt++;		
							ptr++;	
						}
						url[urlcnt] = '\0';
	
						cout<<"Sending the request to host:\n\n"<<clientinfo->clientrequest<<"\n";

						//if the host server is down
						if(write(clientinfo->hostsockfd,clientinfo->clientrequest,strlen(clientinfo->clientrequest)) == -1)
							pagenotavailable = true;
						else{ 
							cout<<"Request forwarded to the server at the IP address "<<clientinfo->hostipaddress<<"\n"; 

							bool mimefound = false;
							bool contenttypefound = false;
							char *contenttypeptr;
							char contenttype[50];
							FILE *fpaddjs,*fpsendhtml;
						
							bool scriptsend = false;
							bool scriptadded = false;
							bool contentlengthmodified = false;
							bool bodytagupdated = false;
							bool addjsread = false;

							char filenametemp[50];
							//wait for the reply from the host server
							//while((cnt = read(clientinfo->hostsockfd,tempstrbig,1499))>0){
							while(1){
										
								cnt = read(clientinfo->hostsockfd,clientinfo->clientrequest,BUFSIZ);
								//fputs(clientinfo->clientrequest,stdout);						
								cout<<"Successfully recieved "<<cnt<<" bytes from the server\n";
								if(cnt <=0)
									break;
														
								//Find the content-type from the reply
								clientinfo->clientrequest[cnt] = '\0';

								if(contenttypefound == false){
									contenttypeptr = strstr(clientinfo->clientrequest,"Content-Type");
									if(contenttypeptr!=NULL){
										contenttypeptr += strlen("Content-Type: ");
									}									
								}
						
								if(contenttypefound == false && contenttypeptr!=NULL){
									int i;

									contenttypefound = true;
									while(*contenttypeptr != ';' && *contenttypeptr != '\r')
										contenttype[i++] = *(contenttypeptr++);
							
									contenttype[i] = '\0';
				
									cout<<"Found the content-type to be "<<contenttype<<"\n";
									if(strstr(contenttype,"text/")!=NULL || strcmp(contenttype,"application/octet-stream") || strstr(contenttype,"image/")!=NULL){
										
										mimefound = true;
									}
									else{
										//compare the content type with those in the database
										
										sprintf(query,"select mime from MIME_CAPABILITY where clientip = '%s'",clientinfo->clientipaddress);
			
										semop(controller->dbsemid,&controller->wait,1);
											mysql_query(dbconnection,query);
											result = mysql_store_result(dbconnection);	
							
											if(result!=NULL){
												while((row = mysql_fetch_row(result))){
													if(strcmp(contenttype,row[0]) == 0){
														mimefound = true;
														break;
													}
												}		
											}
											else
												mimefound = true;
										semop(controller->dbsemid,&controller->signal,1);
									}
								}
		
								if(mimefound){
						
									if(scriptsend == false && !strcmp(contenttype,"text/html")){
										
										if(fpaddjs == NULL && fpsendhtml == NULL){
											sprintf(filenametemp,"%s/addjs.js",HOME_DIR_PATH);
											fpaddjs = fopen(filenametemp,"r");																			

											sprintf(filenametemp,"%s/tempsend.html",HOME_DIR_PATH);
											fpsendhtml = fopen(filenametemp,"w");															
										}
	
										if(contentlengthmodified == false){
											
											cout<<clientinfo->clientrequest<<endl;
											tempptr = strcasestr(clientinfo->clientrequest,"content-length: ");
													
											tempptr += strlen("content-length: ");		
											contentlengthmodified = true;			
		
											contenttypeptr = clientinfo->clientrequest;
										
											while(contenttypeptr != tempptr){
												fputc(*contenttypeptr,fpsendhtml);	
												contenttypeptr += 1;
											}
									
											int cnt = 0;
											while(*tempptr!='\r')
												tempstr[cnt++] = *(tempptr++);
											tempstr[cnt] ='\0';

											tempptr+=2;

											int cursize = atoi(tempstr);
											sprintf(tempstr,"%d\r\n",cursize + controller->addjssize + strlen(" onload=\"sendRequest()\"")/*controller->addjssize*/);
				
											fputs(tempstr,fpsendhtml);

											contenttypeptr = strstr(clientinfo->clientrequest,"\r\n\r\n") + strlen("\r\n\r\n");

											while(tempptr != contenttypeptr){
												fputc(*tempptr,fpsendhtml);
												tempptr++;
											}
											
										}
										
										if(scriptadded == false){																
										
											contenttypeptr = strstr(tempptr,"<head>");

											if(contenttypeptr==NULL){
												contenttypeptr = strstr(tempptr,"<html>") + strlen("<html>");
											}
											else
												contenttypeptr += strlen("<head>");	

											while(tempptr != contenttypeptr){
												fputc(*tempptr,fpsendhtml);
												tempptr++;
											}

											while(fgets(tempstrbig,1500,fpaddjs)!=NULL)
												fputs(tempstrbig,fpsendhtml);																		
	
											scriptadded = true;									
										}						
		
										if(bodytagupdated == false){
											contenttypeptr = strstr(tempptr,"<body");
							
											if(contenttypeptr == NULL){
												fputs(tempptr,fpsendhtml);																				
											}
											else{
												char *ptr1 = strchr(contenttypeptr,'>');			
												char *ptr2 = strstr(contenttypeptr,"onload=");

												contenttypeptr += strlen("<body");  
						
												while(tempptr != contenttypeptr){
													fputc(*tempptr,fpsendhtml);															
													tempptr++;
												}	
												if(ptr2==NULL || ptr1 < ptr2){
													fputs(" onload=\"sendRequest()\"",fpsendhtml);
													fputs(tempptr,fpsendhtml);
												}
												else{
													ptr2 += strlen("onload=\"");
													ptr2 = strchr(ptr2,'"');
													
													while(tempptr != ptr2){
														fputc(*tempptr,fpsendhtml);
													}
													fputs(";sendrequest()",fpsendhtml);
													fputs(tempptr,fpsendhtml);
												}
												bodytagupdated == true;
												scriptsend = true;
	
												fclose(fpsendhtml);	
												fclose(fpaddjs);
												
											}																																						
										}
									}	
											
									if(!strcmp(contenttype,"text/html")){
										if(scriptsend==true && addjsread==false){			
												
											sprintf(filenametemp,"%s/tempsend.html",HOME_DIR_PATH);
											fpsendhtml = fopen(filenametemp,"r");															
									
											addjsread=true;
										
											while(!feof(fpsendhtml)){
												fgets(clientinfo->clientrequest,BUFSIZ,fpsendhtml);
												cnt = write(clientinfo->clientsockfd,clientinfo->clientrequest,strlen(clientinfo->clientrequest));
											}
											fclose(fpsendhtml);
											cout<<"\n\nWritten all the data\n";
											break;
										}
									}
									else{
										cnt = write(clientinfo->clientsockfd,clientinfo->clientrequest,cnt);
										cout<<"Successfully written "<<cnt<<" bytes to the client socket\n";
									}								
								
								}
								else{	
									cout<<"demand for video of the type "<<contenttype<<endl;
									sleep(40);
									//check if a transcoded file already exists in the system
									sprintf(query,"select * from FILES where url = '%s'",url);		
	
									mysql_query(dbconnection,query);					
									result = mysql_store_result(dbconnection);	
									
									if(result!=NULL){
										while((row = mysql_fetch_row(result))){
											if(strcmp(row[2],contenttype)==0){
												mimefound = true;	
												break;	
											}
										}

										if(mimefound){
											//stream the data from the existing file.. row[0]
										}
									}

									//start transcoding
									transcodeindex = controller->Qdelete();
									transcodedatapipedatatype->transcodeindex = transcodeindex;		
									transcodedatapipedatatype->msgcode = START_TRANSCODING;
			
									//what we will do is have some top 5-10 mime types which we will serially try to find out	
									//strcpy((controller->transcodetable + transcodeindex)->url,);
									//strcpy((controller->transcodetable + transcodeindex)->inputmime,);
									//strcpy((controller->transcodetable + transcodeindex)->outputmime,);
									controller->copyTranscodeEntryStructure(&(transcodedatapipedatatype->transcodeentry),(controller->transcodetable + transcodeindex));

									write(controller->transcodehandlerdatapipefd,transcodedatapipedatatype,TRANSCODEHANDLER_DATAPIPE_DATASIZE);
	
									semop(controller->transcodetablesemid,&(controller->transcodetable + transcodeindex)->wait,1);
									
									if((controller->transcodetable + transcodeindex)->transcodingstarted == true){
										char command[200];
										sprintf(command,"sed 's/IPADDRESS/%s/g' %s/jsreply.js | sed 's/FILENAME/%s/g' > %s/tempjs.js",controller->ipaddress,HOME_DIR_PATH,(controller->transcodetable + transcodeindex)->transcodedfilename,HOME_DIR_PATH);
										break;	
									}
									else{
										//stream the current data as it is
									}								
								}	
							}
						}
					}
				}
			}			
		}

		if(pagenotavailable == true){
				sprintf(filepath,"%s/%s",HOME_DIR_PATH,NOTFOUND_FILEPATH);
				filefd = open(filepath,O_RDONLY);		
				
				while((cnt = read(filefd,tempstrbig,1499))>0)
					write(clientinfo->clientsockfd,tempstrbig,cnt);	

				close(filefd);
		}
	}	

	close(clientinfo->clientsockfd);	
	cout<<"Request by the client IP: "<<clientinfo->clientipaddress<<" for Host: "<<clientinfo->hostname<<" at the Host-Port: "<<clientinfo->hostport<<" replied back: "<<strerror(errno)<<"\n";

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
	int webpagefd,tempnum,clientsockfd;
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
										semop(controller->transcodetablesemid,&(controller->transcodetable + controller->transcodecontrolpipedatatype->transcodeindex)->signal,1);
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

	cout<<"Controller got the signal that Transcode Handler is shutting down\n";
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

		cout<<"Controller sending the shutdown signal\n";	
		serverrunning = false;
		temptranscodedatapipedatatype.msgcode = SHUTDOWN;
		write(transcodehandlerdatapipefd,(void *)&temptranscodedatapipedatatype,TRANSCODEHANDLER_DATAPIPE_DATASIZE);	

		cout<<"Controller waiting for TranscodeHandler to shutdown\n";
		pthread_join(threadtranscodecontrolpipehandler,NULL);
		cout<<strerror(errno)<<"\n";			
		cout<<"Terminated the server\n";
		
	}
	else{
		
		cout<<"Controller sending the shutdown signal\n";	

		//open the pipes
		transcodehandlerdatapipefd = open(TRANSCODEHANDLER_DATAPIPE_PATH,O_WRONLY);
		transcodehandlercontrolpipefd = open(TRANSCODEHANDLER_CONTROLPIPE_PATH,O_RDONLY);
		
		cout<<"Controller waiting for TranscodeHandler to shutdown\n";
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

	semop(queuesemid,&wait,1);
		rear = (rear + 1) % SERVER_MAXREQUESTHANDLE_COUNT;

    //after incrementing the front if the following holds true then the queue has become empty
    if(transcodetableempty == true){
    	transcodetableempty = false;
    }

	transcodetablequeue[rear] = transcodeindex;
 	semop(queuesemid,&signal,1);

	cout<<"rear = "<<rear<<"\tfront = "<<front<<"\n";

}

int Controller::Qdelete(){
	
	int retval;

	semop(queuesemid,&wait,1);
		
		retval = transcodetablequeue[front];
		front = (front + 1) % SERVER_MAXREQUESTHANDLE_COUNT;

		if(front == (rear + 1) % SERVER_MAXREQUESTHANDLE_COUNT){
			transcodetableempty = true;																							
		}

	semop(queuesemid,&signal,1);
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

	queuesemid = semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL);  //1 specifies that I need a single semaphore
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
	wait.sem_num = 0;
	wait.sem_op = -1;
	wait.sem_flg = SEM_UNDO;

	signal.sem_num = 0;
	signal.sem_op = 1;
	signal.sem_flg = SEM_UNDO;
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
	
		(transcodetable + i)->wait.sem_num = i;
		(transcodetable + i)->wait.sem_op = -1;
		(transcodetable + i)->wait.sem_flg = SEM_UNDO;
		
		(transcodetable + i)->signal.sem_num = i;
		(transcodetable + i)->signal.sem_op = 1;
		(transcodetable + i)->signal.sem_flg = SEM_UNDO;

		semctl(transcodetablesemid,i,SETVAL,0);
	}

	/* set the port number and the ip address */
	portnumber = atoi((*argv)[2]);  
/*	char *device;
	strcpy(device,(*argv)[1]);*/
	strcpy(ipaddress,(*argv)[1]);

	/*CODE TO FIND THE SIZE OF "addjs" and "jsreply" */
	char sysstr[100];
	char filename[50];
		
	sprintf(sysstr,"sed 's/IPADDRESS/%s:%d/g' %s/%s > %s/%s",ipaddress,portnumber,HOME_DIR_PATH,"actualaddjs.js",HOME_DIR_PATH,"addjs.js");
	system(sysstr);

	struct stat statbuffer;

	sprintf(filename,"%s/%s",HOME_DIR_PATH,"addjs.js");
	stat(filename,&statbuffer);	

	addjssize = statbuffer.st_size;	
	cout<<"addjssize = "<<addjssize<<"\n";
}	
