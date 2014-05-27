/*
 * structures.h
 *
 *  Created on: 05-Jan-2011
 *      Author: anirudh
 */

#include "filepath.h"
#include<sys/sem.h>

const int MYSQL_PORT = 3306;
const int STREAMING_SERVER_PORT = 81;
const int SERVER_MAXREQUESTHANDLE_COUNT = 1000;
const int TRANSCODE_MAXREQUESTHANDLE_COUNT = 50;
const int TRANSCODED_FILENAMESIZE = 100;
const int SOCKET_QUEUESIZE = 50;
const int HOSTNAME_LENGTH = 70;
const int GETREQUEST_LENGTH = 16384 + 1;  // BUFSIZ * 2
const int USERAGENT_LENGTH = 300;

const key_t DBSEMKEY = 1234;
const key_t TRANSCODESEMKEY = 1235;
const key_t QUEUESEMKEY = 1236;

class Controller;

enum ExitCodes{
	NORMAL_EXIT,SERVER_START_FAIL
};

enum RequestProtocol{
	HTTP=80,RTP
};
			
enum PipeCode{
	TRANSCODING_STARTED,TRANSCODING_OVER,TRANSCODING_FAILED,START_TRANSCODING,KILL_TRANSCODING,TRANSCODING_KILLED,SHUTDOWN,CLEAR_FOR_SHUTDOWN,UNKNOWNCODE_RECEIVED,LISTEN_FOR_SHUTDOWN
};

struct ClientInfo{

  Controller *controller;

  int clientsockfd;
  char clientipaddress[16];
  int clientport;
  char clientrequest[GETREQUEST_LENGTH];
	char clientuseragent[USERAGENT_LENGTH];

  int hostsockfd;
  int hostport;
  char hostipaddress[16];
  char hostname[HOSTNAME_LENGTH];

};

struct TranscodeEntryStructure{

	int transcodeindex;
	struct sembuf waitop,signalop;

	char url[500];
	char inputmime[50]; //at the max I can support 1K mime types
	char outputmime[50];

	bool transcodingstarted;
	char transcodedfilename[TRANSCODED_FILENAMESIZE];
	 
};

const int TRANSCODE_ENTRYSIZE = sizeof(struct TranscodeEntryStructure);

struct TranscodeHandlerDataPipeDataType{
	int msgcode;
	
	/*	START_TRANSCODING
	 *	KILL_TRANSCODING
	 *	SHUTDOWN
	 */

	struct TranscodeEntryStructure transcodeentry;
	int transcodeindex;
};

const int TRANSCODEHANDLER_DATAPIPE_DATASIZE = sizeof(struct TranscodeHandlerDataPipeDataType);

struct TranscodeHandlerControlPipeDataType{
	int msgcode;	
	
	/*	TRANSCODING_STARTED
	 *	TRANSCODING_OVER
	 *	TRANSCODING_FAILED
	 *	TRANSCODING_KILLED
	 *	CLEAR_FOR_SHUTDOWN
	 */

	int transcodeindex;
	char transcodedfilename[TRANSCODED_FILENAMESIZE];	
	char outputmime[50];
};

const int TRANSCODEHANDLER_CONTROLPIPE_DATASIZE = sizeof(struct TranscodeHandlerControlPipeDataType);

struct TranscodeHandlerInternalDataPipeDataType{
	int msgcode;
	/*
	 *	KILL_TRANSCODING
	 */	

	int transcodeindex;
};

const int TRANSCODEHANDLER_INTERNALDATAPIPE_DATASIZE = sizeof(struct TranscodeHandlerInternalDataPipeDataType);

struct TranscodeHandlerInternalControlPipeDataType{
	int msgcode;

	/*	TRANSCODING_OVER
	 *	TRANSCODING_FAILED
	 *	TRANSCODING_KILLED
	 *	TRANSCODING_STARTED
	 *	LISTEN_FOR_SHUTDOWN
	 */

	int transcodeindex;
	char transcodedfilename[TRANSCODED_FILENAMESIZE];
};

const int TRANSCODEHANDLER_INTERNALCONTROLPIPE_DATASIZE = sizeof(struct TranscodeHandlerInternalControlPipeDataType);

