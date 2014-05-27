DCMTOL: all

all: DCMTOL_RUN ./Executables/transcodehandler ./Executables/transcoder
	sudo chmod ugo+rw ./Pipes/*

DCMTOL_RUN: ./Source/Controller.cpp
	g++ -g -o DCMTOL_RUN ./Source/Controller.cpp -I /usr/include/mysql/ -lmysqlclient -lpthread

./Executables/transcodehandler: ./Source/TranscodeHandler.cpp
	g++ -g -o ./Executables/transcodehandler ./Source/TranscodeHandler.cpp -lpthread

./Executables/transcoder: ./Source/Transcoder.cpp
	g++ -g -o ./Executables/transcoder ./Source/Transcoder.cpp

