/*
 * webmtranscode.cpp
 *
 *  Created on: 02-Jan-2011
 *      Author: anirudh
 */

#include<iostream>
#include<stdio.h>
#include<stdlib.h>
#include<signal.h>
#include<string.h>
#include<fcntl.h>
#include "structures.h"
#include<errno.h>
#include<unistd.h>

using namespace std;

class Transcoder{

	struct TranscodeEntryStructure *transcodeentry;
	int transcodeindex;

	int internalcontrolpipefd,internaldatapipefd;

	struct TranscodeHandlerInternalControlPipeDataType *internalcontrolpipedatatype;
	struct TranscodeHandlerInternalDataPipeDataType *internaldatapipedatatype;

public:

	void establishSignalHandler();
	void startTranscoding();
	void initializeTranscoder(int *,char ***);
	void clear();
	friend void sighandlerSIGUSR1(int);
};

Transcoder *t1; //I dont have any other choice...I need to access it in the signal handler & I can't pass it by parameter

void sighandlerSIGUSR1(int sig_num){

	read(t1->internaldatapipefd,t1->internaldatapipedatatype,TRANSCODEHANDLER_INTERNALDATAPIPE_DATASIZE);

	switch(t1->internaldatapipedatatype->msgcode){
		case KILL_TRANSCODING:	t1->internalcontrolpipedatatype->msgcode = TRANSCODING_KILLED;
														t1->internalcontrolpipedatatype->transcodeindex = t1->transcodeindex;
														strcpy(t1->internalcontrolpipedatatype->transcodedfilename,t1->transcodeentry->transcodedfilename);
														write(t1->internalcontrolpipefd,t1->internalcontrolpipedatatype,TRANSCODEHANDLER_INTERNALCONTROLPIPE_DATASIZE);		
										
														t1->clear();
														cout<<"Trancoder with transcodeindex \""<<t1->transcodeindex<<"\" killed self\n";
											
														cout<<"KILL STAT:\tpgid = "<<getpgid(getpid())<<"\n";
														killpg(getpgid(getpid()),SIGKILL);
														cout<<strerror(errno)<<"\n";	
														break;

		default: cout<<"unknown msgcode received in transcode\n";
	}

}

int main(int argc,char *argv[]){

	t1 = new Transcoder();

	t1->initializeTranscoder(&argc,&argv);
	t1->establishSignalHandler();	
	t1->startTranscoding();

	t1->clear();
	delete t1;

	return 0;
}



void Transcoder::clear(){

	//clean all the memory allocated
	delete internalcontrolpipedatatype;
	delete internaldatapipedatatype;
	delete transcodeentry;

	close(internalcontrolpipefd);
	close(internaldatapipefd);
}

void Transcoder::initializeTranscoder(int *argc,char ***argv){

	//Param are ... url,outputmime,transcodedfilename,currenttranscodeindex	
	srand((unsigned)time(NULL));

	while(setpgid(getpid(),rand()/1000) == -1);

	cout<<"Transcoder's new process group ID set\n";
	transcodeentry = new struct TranscodeEntryStructure();	
	//I could have stored param in other structures but since they are all based on shared memory strucure I preferred to use it

	strcpy(transcodeentry->url,(*argv)[1]);
	strcpy(transcodeentry->outputmime,(*argv)[2]);
	strcpy(transcodeentry->transcodedfilename,(*argv)[3]);
	transcodeindex = atoi((*argv)[4]);

	internalcontrolpipedatatype = new struct TranscodeHandlerInternalControlPipeDataType();
	internaldatapipedatatype =  new struct TranscodeHandlerInternalDataPipeDataType();

	internalcontrolpipefd = open(TRANSCODEHANDLER_INTERNALCONTROLPIPE_PATH,O_WRONLY);
	internaldatapipefd = open(TRANSCODEHANDLER_INTERNALDATAPIPE_PATH,O_RDONLY);
}

void Transcoder::establishSignalHandler(){

	if(signal(SIGUSR1,sighandlerSIGUSR1) == SIG_ERR){
			printf("\n\nError while establishing signal handler\n\n");
			//send transcoding failed
	
			internalcontrolpipedatatype->msgcode = TRANSCODING_FAILED;
			internalcontrolpipedatatype->transcodeindex = transcodeindex;
			strcpy(internalcontrolpipedatatype->transcodedfilename,transcodeentry->transcodedfilename);
			write(internalcontrolpipefd,internalcontrolpipedatatype,TRANSCODEHANDLER_INTERNALCONTROLPIPE_DATASIZE);		

		  clear();
			exit(2); //I donno what the exact return status should be..so I returned 2 randomly..I can't return 0 after an error 
	} 
}

void Transcoder::startTranscoding(){

	char launchstr[1000];

sprintf(launchstr,"gst-launch-0.10 oggmux name=mux ! filesink location=%s/%s uridecodebin uri=%s name=d { d. ! ffmpegcolorspace ! theoraenc ! queue ! mux. } { d. ! progressreport ! audioconvert ! audiorate ! vorbisenc ! queue ! mux. }	> %s/%s.log",TRANSCODED_VIDEO_PATH,transcodeentry->transcodedfilename,transcodeentry->url,TRANSCODED_VIDEO_PATH,transcodeentry->transcodedfilename);

	internalcontrolpipedatatype->msgcode = TRANSCODING_STARTED;
	internalcontrolpipedatatype->transcodeindex = transcodeindex;
	strcpy(internalcontrolpipedatatype->transcodedfilename,transcodeentry->transcodedfilename);
	write(internalcontrolpipefd,internalcontrolpipedatatype,TRANSCODEHANDLER_INTERNALCONTROLPIPE_DATASIZE);		

	cout<<"Transcoder about to start transcoding\n"<<launchstr<<"\n\n";
	system(launchstr);
	//if it returns -1 then return TRANSCODING_FAILED
	
	internalcontrolpipedatatype->msgcode = TRANSCODING_OVER;
	internalcontrolpipedatatype->transcodeindex = transcodeindex;
	strcpy(internalcontrolpipedatatype->transcodedfilename,transcodeentry->transcodedfilename);
	write(internalcontrolpipefd,internalcontrolpipedatatype,TRANSCODEHANDLER_INTERNALCONTROLPIPE_DATASIZE);		

}


