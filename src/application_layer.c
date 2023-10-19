// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    struct LinkLayer ll;
    
    sprintf(ll.serialPort, "%s", serialPort);
    
    ll.role = strcmp(role, "rx") ? LlRx : LlTx;
    ll.baudrate = baudRate;
    ll.nRetransmissions = nTries;
    ll.timeout = timeout;
    
    if(llopen(ll) == -1) {
        printf("llopen error");
        exit(1);
    }

    int bufSize = MAX_PAYLOAD_SIZE - 1;
    unsigned char buf[bufSize + 1];

    
    switch(ll.role) {
    	case LlTx: {
		    if((FILE* file = fopen(filename, "rb")) == NULL) {
        		printf("fopen error");
        		exit(-1);
    		}
    		
    		fseek(file, 0L, SEEK_END);
    		
    		long int fsize = ftell(file);
    		
    		rewind(file);
    		
			unsigned char* fcontent = (unsigned char*) malloc(fileLength);
			fread(content, fileLength, file);
			
			long int i = fileSize;
			
			while(i > 0) {
				int dataLen;
				if(i > MAX_PAYLOAD_SIZE) {
					dataLen = MAX_PAYLOAD_SIZE;
				} else {
					dataLen = i;
				}
				
				unsigned char* data = (unsigned char*) malloc(datalen);
				
				memcpy(data, fcontent, dataLen);
				
				
			}
    		
    		break;
    	} case LlRx: {
    	
    		break;
    	} default: {
    		break;
    	}
    }
   
}

unsigned char* makePacket(unsigned int which, unsigned char* data, int dataSize) {
	int packetSize = datasize + 4;
	
	unsigned char* packet = (unsigned char*) malloc(packetSize);
	
	packet[0] = which;
	packet[1] = dataSize >> 8 & 0xFF;
	packet[2] = dataSize & 0xFF;
	
	memcpy(packet + 4, data, dataSize);
	
	return packet;
}
