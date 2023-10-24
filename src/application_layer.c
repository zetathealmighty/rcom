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

unsigned char* controlPacket(int which, const char* filename, long int len, int* size)  {
	const int len1 = (int) ceil(log2f((float)len) / 8.0);
    const int len2 = strlen(filename);

	*size = 3 + len1 + 2 + len2;
	
	printf("controlpacket size is %i\n", *size);
	fflush(stdout);

	unsigned char* packet = (unsigned char*) malloc(*size);

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
    
    strcpy(ll.serialPort, serialPort);
    ll.role = strcmp(role, "rx") ? LlTx : LlRx;
    ll.baudRate = baudRate;
    ll.nRetransmissions = nTries;
    ll.timeout = timeout;
	
	printf("before llopen\n");
	fflush(stdout);

    int fd = llopen(ll);
    
    printf("after llopen\n");
    fflush(stdout);

    if(fd == -1) {
        perror("llopen error");
        exit(-1);
    }
    
    switch(ll.role) {
    	case LlRx: {
			unsigned char* packet = (unsigned char*) malloc(MAX_PAYLOAD_SIZE);
			
			printf("before llread\n");
			fflush(stdout);

			if((llread(packet)) < 0) {
				perror("rx llread control error\n");
				exit(-1);
			}
			
			printf("after llread\n");
			fflush(stdout);

			long int fsize;
			unsigned char fsizeBytes = packet[2];
			unsigned char fsizeParse[fsizeBytes];
			
			printf("file len bytes is  %i\n", fsizeBytes);
			fflush(stdout);

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
				printf("%i, %li vs %li\n", readNow, readAlr, fsize);
				fflush(stdout);
				
				if(readNow == 0) {
					printf("found close\n");
					fflush(stdout);
					break;
				}
				
				readAlr += readNow - 8;

				if(packet[0] != 3) {
					unsigned char *buffer = (unsigned char*) malloc(readNow);
					memcpy(buffer, packet + 4, readNow - 4);

					fwrite(buffer, sizeof(unsigned char), readNow - 4, outFile);
					free(buffer);
				} else {
					printf("found close\n");
					fflush(stdout);
					continue;
				}
			}
			
			free(packet);
			
			printf("before closing\n");
			fflush(stdout);
			
    		// for some reason this absolute idiot is closing the file and segfault?	
    		// error creating the file?		
			fclose(outFile);
			
			printf("closed\n");
			fflush(stdout);

    		break;
    	} case LlTx: {
			FILE* inFile = fopen(filename, "rb");
		    if(inFile == NULL) {
        		perror("tx fopen error\n");
        		exit(-1);
    		}
    		
    		fseek(inFile, 0L, SEEK_END);
    		
    		long int fsize = ftell(inFile);
    		
    		rewind(inFile);
    		
    		printf("file size is %li\n", fsize);
			fflush(stdout);
			
			int packetLen;
			unsigned char* startPacket = controlPacket(2, filename, fsize, &packetLen);
			
			printf("before startpacket\n");
			fflush(stdout);
			
            if(llwrite(startPacket, packetLen) < 0) {
                perror("tx llwrite controlstart error\n");
                exit(-1);
            }
            
            printf("after startpacket\n");
			fflush(stdout);

			unsigned char which = FALSE;
			unsigned char* buffer = (unsigned char*) malloc(MAX_PAYLOAD_SIZE);
			
			long int i = fsize;
			
			
			while(i != 0) {
				int readNow = fread(buffer, sizeof(unsigned char), MAX_PAYLOAD_SIZE, inFile);
				
				int packetLen;
				unsigned char* packet = (unsigned char*) malloc(MAX_PAYLOAD_SIZE);
				packet = dataPacket((int) which, buffer, readNow, &packetLen);

				if(llwrite(packet, packetLen) < 0) {
					perror("tx llwrite error\n");
					exit(-1);
				}

				i -= readNow;
				which = !which;
			}
			
			free(buffer);

			unsigned char* endControl = controlPacket(3, filename, fsize, &packetLen);
			
			printf("before controlend\n");
			fflush(stdout);

			if(llwrite(endControl, packetLen) < 0) { 
                perror("tx llwrite controlend error \n");
                exit(-1);
            }
            
            printf("after controlend\n");
			fflush(stdout);

            llclose(fd);
            
            printf("closed\n");
			fflush(stdout);

            break;
    	} default: {
    		break;
    	}
    }
   
}
