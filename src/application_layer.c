// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

unsigned char* controlPacket(int which, const char* filename, long int len)  {
	const int len1 = (int) ceil(log2f((float)len) / 8.0);
    const int len2 = strlen(filename);

	long int size = 3 + len1 + 2 + len2;

	unsigned char* packet = (unsigned char*) malloc(size);

	packet[0] = which;
	packet[1] = 0;
	packet[2] = len1;

	for(int j = 0; j < len1; j++) {
		packet[2 + len1 - j] = len & 0xFF;
		len >>= 8;
	}

	packet[len1 + 3] = 1;
	packet[len1 + 4] = len2;

	memcpy(packet + len1 + 4, filename, len2);

	return packet;
}

unsigned char* dataPacket(int which, unsigned char* data, int dataLen, int *packetLen) {
	*packetLen = dataLen + 4;
	
	unsigned char* packet = (unsigned char*) malloc(*packetLen);
	
	packet[0] = 1;
	packet[1] = which;
	packet[2] = dataLen >> 8 & 0xFF;
	packet[3] = dataLen & 0xFF;
	
	memcpy(packet + 4, data, dataLen);
	
	return packet;
}

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer ll;    
    
    ll.role = strcmp(role, "rx") ? LlRx : LlTx;
    ll.baudRate = baudRate;
    ll.nRetransmissions = nTries;
    ll.timeout = timeout;

	printf(ll.serialPort, "%s", serialPort);

    int fd = llopen(ll);

    if(fd == -1) {
        printf("llopen error");
        exit(-1);
    }
    
    switch(ll.role) {
    	case LlRx: {
			unsigned char* packet = (unsigned char*) malloc(MAX_PAYLOAD_SIZE);

			if((llread(packet)) < 0) {
				printf("rx llread control error\n");
				exit(-1);
			}

			long int fsize;
			unsigned char fsizeBytes = packet[2];
			unsigned char fsizeParse[fsizeBytes];

			memcpy(fsizeParse, packet + 3, fsizeBytes);

			for(int i = 1; i < fsizeBytes; i++) {
				fsize |= (fsizeParse[fsizeBytes - i - 1] << (8*i));
			}

			unsigned char nameBytes = packet[3 + fsizeBytes	+ 1];
			unsigned char name[nameBytes];

			memcpy(name, packet + 3 + nameBytes + 2, nameBytes);
			
			FILE* outFile = fopen((char *) name, "wb+");

			long int readAlr = 0;
			int readNow;

			while(readAlr != fsize) {
				while((readNow = llread(packet)) < 0);
				readAlr += readNow;

				if(packet[0] != 3) {
					unsigned char *buffer = (unsigned char*) malloc(readNow);
					memcpy(buffer, packet + 4, readNow - 4);

					fwrite(buffer, sizeof(unsigned char), readNow - 4, outFile);
					free(buffer);
				}
			}
			fclose(outFile);

    		break;
    	} case LlTx: {
			FILE* inFile = fopen(filename, "rb");
		    if(inFile == NULL) {
        		printf("tx fopen error\n");
        		exit(-1);
    		}
    		
    		fseek(inFile, 0L, SEEK_END);
    		
    		long int fsize = ftell(inFile);
    		
    		rewind(inFile);

			unsigned char* startPacket = controlPacket(2, filename, fsize);
            if(llwrite(startPacket, fsize + 5) < 0) {
                printf("tx llwrite controlstart error\n");
                exit(-1);
            }
    		
			unsigned char which = FALSE;
			unsigned char* buffer = (unsigned char*) malloc(MAX_PAYLOAD_SIZE);
			
			long int i = fsize;
			
			while(i != 0) {
				int readNow = fread(buffer, sizeof(unsigned char), fsize, inFile);
				
				int packetLen;
				unsigned char* packet = dataPacket((int) which, buffer, readNow, &packetLen);

				if(llwrite(packet, packetLen) < 0) {
					printf("tx llwrite error\n");
					exit(-1);
				}

				i -= readNow;
				which = !which;
			}

			unsigned char* endControl = controlPacket(3, filename, fsize);

			if(llwrite(endControl, fsize + 5) < 0) { 
                printf("tx llwrite controlend error \n");
                exit(-1);
            }

            llclose(fd);

            break;
    	} default: {
    		break;
    	}
    }
   
}
