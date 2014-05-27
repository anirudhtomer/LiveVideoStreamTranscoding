/*
 * TranscodeHandler.cpp
 *
 *  Created on: 03-Jan-2011
 *      Author: anirudh
 */


#include "structures.h"
#include<iostream>
#include<string.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<fcntl.h>
#include<pthread.h>
#include<sys/shm.h>
#include<sys/sem.h>
#include<sys/stat.h>
#include<signal.h>
#include<sys/wait.h>
#include<dirent.h>

using namespace std;

void* internalControlPipeHandler(void *);

class Tables{

protected:
	
	struct ChildEntry{

		pid_t childid;
		int transcodeindex; //this is the primary key
	}*childtable;

public:
	Tables(){
		childtable = new struct ChildEntry[TRANSCODE_MAXREQUESTHANDLE_COUNT];
	}
	
	void updateChildEntry(int,pid_t);
	
};

class TranscodeHandler:public Tables{

	struct TranscodeEntryStructure *transcodeentry;
	int transcodetablesemid;
	
	int datapipefd,controlpipefd;
	int internaldatapipefd,internalcontrolpipefd;
	int fakeinternaldatapipefd,fakeinternalcontrolpipefd;

	bool shutdown;
	char *transcodedfilename;
	
	int totaltranscodingcount;
	struct TranscodeHandlerDataPipeDataType *datapipedatatype;
	struct TranscodeHandlerControlPipeDataType *controlpipedatatype;
	struct TranscodeHandlerInternalControlPipeDataType *internalcontrolpipedatatype;
	struct TranscodeHandlerInternalDataPipeDataType *internaldatapipedatatype;

	pthread_t threadinternalpipehandler;

public:
	TranscodeHandler(){
		shutdown = false;
	}

	void initializeTranscodeHandler(int *,char ***);
	void openExternalPipes();
	void startTranscodeHandler();
	void createThreads();
	void clear();
	void updateExistingFileTable();

	friend void* internalControlPipeHandler(void *);

};

void* internalControlPipeHandler(void *param){

	int childexitstatus;

	TranscodeHandler *transcodehandler = (TranscodeHandler *) param;

	transcodehandler->internaldatapipefd = open(TRANSCODEHANDLER_INTERNALDATAPIPE_PATH,O_WRONLY);
	transcodehandler->internalcontrolpipefd = open(TRANSCODEHANDLER_INTERNALCONTROLPIPE_PATH,O_RDONLY);

	//now continously read from the control pipe
	while(!transcodehandler->shutdown || transcodehandler->totaltranscodingcount){
		read(transcodehandler->internalcontrolpipefd,transcodehandler->internalcontrolpipedatatype,TRANSCODEHANDLER_INTERNALCONTROLPIPE_DATASIZE);
			
		switch(transcodehandler->internalcontrolpipedatatype->msgcode){
			case TRANSCODING_OVER:  transcodehandler->controlpipedatatype->msgcode = TRANSCODING_OVER;
								transcodehandler->controlpipedatatype->transcodeindex = transcodehandler->internalcontrolpipedatatype->transcodeindex;     
								strcpy(transcodehandler->controlpipedatatype->transcodedfilename,transcodehandler->internalcontrolpipedatatype->transcodedfilename);

								waitpid((transcodehandler->childtable + transcodehandler->controlpipedatatype->transcodeindex)->childid,&childexitstatus,WNOHANG);
								(transcodehandler->childtable + transcodehandler->controlpipedatatype->transcodeindex)->childid = -1;
								transcodehandler->totaltranscodingcount--;
								cout<<"Transcodehandler recieved TRANSCODING_OVER: transcodingcount = "<<transcodehandler->totaltranscodingcount<<"\n";
								break;

			case TRANSCODING_FAILED:	transcodehandler->controlpipedatatype->msgcode = TRANSCODING_FAILED;
								transcodehandler->controlpipedatatype->transcodeindex = transcodehandler->internalcontrolpipedatatype->transcodeindex;    
								strcpy(transcodehandler->controlpipedatatype->transcodedfilename,transcodehandler->internalcontrolpipedatatype->transcodedfilename);
										
								waitpid((transcodehandler->childtable + transcodehandler->controlpipedatatype->transcodeindex)->childid,&childexitstatus,WNOHANG);
								(transcodehandler->childtable + transcodehandler->controlpipedatatype->transcodeindex)->childid = -1;
								transcodehandler->totaltranscodingcount--;
								cout<<"Transcodehandler recieved TRANSCODING_FAILED: transcodingcount = "<<transcodehandler->totaltranscodingcount<<"\n";
								break;

			case TRANSCODING_KILLED:	transcodehandler->controlpipedatatype->msgcode = TRANSCODING_KILLED;
								transcodehandler->controlpipedatatype->transcodeindex = transcodehandler->internalcontrolpipedatatype->transcodeindex;    
								strcpy(transcodehandler->controlpipedatatype->transcodedfilename,transcodehandler->internalcontrolpipedatatype->transcodedfilename);
								
								waitpid((transcodehandler->childtable + transcodehandler->controlpipedatatype->transcodeindex)->childid,&childexitstatus,WNOHANG);
								(transcodehandler->childtable + transcodehandler->controlpipedatatype->transcodeindex)->childid = -1;
								transcodehandler->totaltranscodingcount--;
								cout<<"Transcodehandler recieved TRANSCODING_KILLED: transcodingcount = "<<transcodehandler->totaltranscodingcount<<"\n";
								break;

			case TRANSCODING_STARTED:	
								transcodehandler->controlpipedatatype->msgcode = TRANSCODING_STARTED;
								transcodehandler->controlpipedatatype->transcodeindex = transcodehandler->internalcontrolpipedatatype->transcodeindex;
								strcpy(transcodehandler->controlpipedatatype->transcodedfilename,transcodehandler->internalcontrolpipedatatype->transcodedfilename);
			
								//totaltranscoding count was already incremented
								break;

			case LISTEN_FOR_SHUTDOWN:	cout<<"Transcode Handler about to shutdown "<<transcodehandler->totaltranscodingcount<<"\n"; 
								break;
			default: cout<<"Unknown msg received from transcodehandlerinternalcontrolpipe";
		}		

		if(transcodehandler->internalcontrolpipedatatype->msgcode != LISTEN_FOR_SHUTDOWN)
			write(transcodehandler->controlpipefd,transcodehandler->controlpipedatatype,TRANSCODEHANDLER_CONTROLPIPE_DATASIZE);
	}

	if(transcodehandler->shutdown && !transcodehandler->totaltranscodingcount){
		//clear for shutdown
		cout<<"Transcodehandler is clear for shutdown \n";
		transcodehandler->controlpipedatatype->msgcode = CLEAR_FOR_SHUTDOWN;
		transcodehandler->controlpipedatatype->transcodeindex = -1;    
		strcpy(transcodehandler->controlpipedatatype->transcodedfilename,"");	

		write(transcodehandler->controlpipefd,transcodehandler->controlpipedatatype,TRANSCODEHANDLER_CONTROLPIPE_DATASIZE);
		
	}

	return (void *)0;
}

int main(int argc,char *argv[]){
	/* argv: transcodeentry key */

	TranscodeHandler *t1 = new TranscodeHandler();

	srand((unsigned)time(NULL));

	t1->initializeTranscodeHandler(&argc,&argv);
	t1->createThreads();
	
	//part of the main thread
	t1->openExternalPipes();
	t1->startTranscodeHandler();

	t1->clear();

	cout<<"TranscodeHandler down\n";	
	delete t1;
	return 0;
}

void Tables::updateChildEntry(int transcodeindex,pid_t childid){

	(childtable + transcodeindex)->transcodeindex = transcodeindex;
	(childtable + transcodeindex)->childid = childid;
}

void TranscodeHandler::clear(){

	delete []childtable;
	delete datapipedatatype;
	delete controlpipedatatype;
	delete internaldatapipedatatype;
	delete internalcontrolpipedatatype;
	delete transcodedfilename;

	close(datapipefd);
	close(controlpipefd);
	close(internaldatapipefd);
	close(internalcontrolpipefd);
	close(fakeinternaldatapipefd);
	close(fakeinternalcontrolpipefd);
	
}

void TranscodeHandler::createThreads(){

	//thread to handle internal data and control pipes
	if(pthread_create(&threadinternalpipehandler,NULL,internalControlPipeHandler,(void *)this)!=0){
  	cout<<"Error while creating the dbhandler thread\n";
  }

}

void TranscodeHandler::startTranscodeHandler(){

	int transnum;
	pid_t childid;

	char currenttranscodeindex[6];

	while(!shutdown){

		read(datapipefd,datapipedatatype,TRANSCODEHANDLER_DATAPIPE_DATASIZE);

		sprintf(currenttranscodeindex,"%d",datapipedatatype->transcodeindex);

		switch(datapipedatatype->msgcode){

			case START_TRANSCODING:
									//generate pseudo random 10 digit number  0 < num < RAND_MAX
									transnum = rand();
								
									sprintf(transcodedfilename,"DCMTOL_%d.%s",transnum,strchr(datapipedatatype->transcodeentry.outputmime,'/') + 1);
												
									childid = fork();

									if(childid==-1){
										cout<<"could not create child process";
										
										struct TranscodeHandlerInternalControlPipeDataType tempinternalcontrolpipedatatype;	

										tempinternalcontrolpipedatatype.msgcode = TRANSCODING_FAILED;
										tempinternalcontrolpipedatatype.transcodeindex = datapipedatatype->transcodeindex;
										strcpy(tempinternalcontrolpipedatatype.transcodedfilename,NULL);
		
										write(fakeinternalcontrolpipefd,&tempinternalcontrolpipedatatype,TRANSCODEHANDLER_INTERNALCONTROLPIPE_DATASIZE);									
										break;
									}
					
									if(childid){
										cout<<"TranscodeHandler started the transcoder for transcoding number : "<<++totaltranscodingcount<<"\n";
										updateChildEntry(datapipedatatype->transcodeindex,childid);				
									}	
								
									if(!childid){
										//urllink,outputmime,transcodedfilename,currenttranscodeindex
										execl(TRANSCODER_PATH,TRANSCODER_PATH,datapipedatatype->transcodeentry.url,datapipedatatype->transcodeentry.outputmime,transcodedfilename,currenttranscodeindex,NULL);										
									}
									break;

			case KILL_TRANSCODING:	cout<<"kill transcoding";

									internaldatapipedatatype->msgcode = KILL_TRANSCODING;
									internaldatapipedatatype->transcodeindex = datapipedatatype->transcodeindex;
				
									write(internaldatapipefd,internaldatapipedatatype,TRANSCODEHANDLER_INTERNALDATAPIPE_DATASIZE);
						
									kill((childtable + datapipedatatype->transcodeindex)->childid,SIGUSR1);
					
									break;

			case SHUTDOWN:	cout<<"TranscodeHandler recieved the shutdown msg from Controller\n";
											shutdown = true;
											internaldatapipedatatype->msgcode = KILL_TRANSCODING;

											for(int index=0;index<TRANSCODE_MAXREQUESTHANDLE_COUNT;index++){
												
												if((childtable + index)->childid!=-1){
													internaldatapipedatatype->transcodeindex = index;
													write(internaldatapipefd,internaldatapipedatatype,TRANSCODEHANDLER_INTERNALDATAPIPE_DATASIZE);
													kill((childtable + index)->childid,SIGUSR1);												
												}
											}

											struct TranscodeHandlerInternalControlPipeDataType tempinternalcontrolpipedatatype;	

											tempinternalcontrolpipedatatype.msgcode = LISTEN_FOR_SHUTDOWN;
											tempinternalcontrolpipedatatype.transcodeindex = -1;
											strcpy(tempinternalcontrolpipedatatype.transcodedfilename,"");
												
											write(fakeinternalcontrolpipefd,&tempinternalcontrolpipedatatype,TRANSCODEHANDLER_INTERNALCONTROLPIPE_DATASIZE);									
	
											break;

			default:				cout<<"unknown code recieved from transcodehandlerdatapipe";
											sleep(40);

		}
	}

	pthread_join(threadinternalpipehandler,NULL);
}

void TranscodeHandler::openExternalPipes(){

	datapipefd = open(TRANSCODEHANDLER_DATAPIPE_PATH,O_RDONLY);
	controlpipefd = open(TRANSCODEHANDLER_CONTROLPIPE_PATH,O_WRONLY);

	fakeinternaldatapipefd = open(TRANSCODEHANDLER_INTERNALDATAPIPE_PATH,O_RDONLY);
	fakeinternalcontrolpipefd = open(TRANSCODEHANDLER_INTERNALCONTROLPIPE_PATH,O_WRONLY);

	cout<<"Transcode Handler ready with all pipes open for communication\n";
}

void TranscodeHandler::initializeTranscodeHandler(int *argc,char ***argv){

	//intialize other variables

	cout<<"process group id of transcodehandler: "<<getpgid(getpid())<<"\n";

	datapipedatatype = new struct TranscodeHandlerDataPipeDataType();
	controlpipedatatype = new struct TranscodeHandlerControlPipeDataType();
	internalcontrolpipedatatype = new struct TranscodeHandlerInternalControlPipeDataType();
	internaldatapipedatatype = new struct TranscodeHandlerInternalDataPipeDataType();

	transcodedfilename = new char[TRANSCODED_FILENAMESIZE];

	for(int index=0;index<TRANSCODE_MAXREQUESTHANDLE_COUNT;index++){	
		(childtable + index)->childid = -1;
	}

	totaltranscodingcount = 0;
	//handle the internal pipes
	if(access(TRANSCODEHANDLER_INTERNALDATAPIPE_PATH,F_OK) == -1 ){
  	if(mkfifo(TRANSCODEHANDLER_INTERNALDATAPIPE_PATH,0666) == -1){
			cout<<"\nError creating the pipe for TRANSCODEHANDLER_DATAPIPE_PATH\n";
    }
	}

  if(access(TRANSCODEHANDLER_INTERNALCONTROLPIPE_PATH,F_OK) == -1 ){
	 	if(mkfifo(TRANSCODEHANDLER_INTERNALCONTROLPIPE_PATH,0666) == -1){
  	 	cout<<"\nError creating the pipe for TRANSCODEHANDLER_CONTROLPIPE_PATH\n";
    }
  }

}
