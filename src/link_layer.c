// Link layer protocol implementation

#include "link_layer.h"

#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

// MISC
// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256

#define FLAG_RCV 0x7E;
#define A_TX 0x03;
#define A_RX 0x01;
#define C_SET 0x03;
#define C_UA 0x07;
#define C_DISC 0x0B;
#define ESC 0x7D;

typedef enum {
	START,
	FLAG,
	A,
	C,
	BCC1,
	BCC2,
	STOP,
	DISC
} llState;

volatile int STOP = FALSE;

int alarmTriggered = FALSE;
int alarmCount = 0;
int timeout = 0;
int retransmissions = 0;

void alarmHandler(int signal) {
    alarmTriggered = TRUE;
    alarmCount++;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
	llState state = START;
	
	int fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);
	
	if (fd < 0) {
        perror(serialPortName);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    if (tcgetattr(fd, &oldtio) == -1) {
        perror("tcgetattr");
        exit(-1);
    }

    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0;
    newtio.c_cc[VMIN] = 1;
    
    tcflush(fd, TCIOFLUSH);
    
    if (tcsetattr(fd, TCSANOW, &newtio) == -1) {
        perror("tcsetattr");
        exit(-1);
    }
    
    unsigned char curr;
    timeout = connectionParameters.timeout;
    retransmissions = connectionParameters.nRetransmissions;
    
    switch(connectionParameters.role) {
    	case LlRx: {
    			while(state != STOP) {
    				if(read(fd, &curr, 1) > 0) {
    					switch(state) {
    						case START: {
    							if(curr == FLAG) state = FLAG_RCV;
    							break;
    						} case FLAG_RCV: {
    							if(curr == A_RX) state = A;
    							else if(curr != FLAG) state = START;
    							break;
    						} case A: {
    							if(curr == C_SET) state = C;
    							else if(curr == FLAG) state = FLAG_RCV;
    							else state = START;
    							break;
    						} case C: {
    							if(curr == (A_TX ^ C_SET)) state = BCC1;
    							else if(curr == FLAG) state = FLAG_RCV;
    							else state = START;
    							break;
    						} case BCC1: {
    							if(curr == FLAG) state = STOP;
    							else state = START;
    							break;
    						} default: {
    							break;
    						}    						
    					}
    				}
    			} unsigned char buf[5] = {FLAG, A_RX, C_RX, A_RX ^ C_RX, FLAG};
    			write(fd, buf, 5);
				break;
				
    	} case LlTx: {
    		(void)signal(SIGALRM, alarmHandler);
    		
    		while(connectionParameters.nRetransmissions != 0 && state != STOP) {
    			unsigned char buf[5] = {FLAG, A_RX, C_RX, A_RX ^ C_RX, FLAG};
    			write(fd, buf, 5);
    			
    			alarm(connectionParameters.timeout);
    			alarmTriggered = FALSE;
    			
    			while(alarmTriggered == FALSE && state != STOP) {
    				if(read(fd, &curr, 1) > 0) {
    					switch(state) {
    						case START: {
    							if(curr == FLAG) state = FLAG_RCV;
    							break;
    						} case FLAG_RCV: {
    							if(curr == A_RX) state = A;
    							else if(curr != FLAG) state = START;
    							break;
    						} case A: {
    							if(curr == C_UA) state = C;
    							else if(curr == FLAG) state = FLAG_RCV;
    							else state = START;
    							break;
    						} case C: {
    							if(curr == (A_RX ^ C_UA)) state = BCC1;
    							else if(curr == FLAG) state = FLAG_RCV;
    							else state = START;
    							break;
    						} case BCC1: {
    							if(curr == FLAG) state = STOP;
    							else state = START;
    							break;
    						} default: {
    							break;
    						}    						
    					}
    				}
    			} connectionParameters.nRetransmissions--;
    		} if(state != STOP) return -1;
    		break;
    	} default: {
    		return -1;
    		break;
    	} 
    } return fd;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize, int fd)
{
    // TODO
	unsigned char frame[2 * bufSize + 6];
	//Header
    frame[0] = FLAG_RCV;
    frame[1] = A_TX;
    frame[2] = C;
    frame[3] = frame[1] ^ frame[2];
	
	//Calculate BCC2
	unsigned char BCC2 = buf[0];
	for(int i = 1; i < bufSize; i++){
		BCC2 = BCC2 ^ buf[i];
	}

	int frameIndex = 4;
	
	for(int i = 0; i < bufSize; i++){
	

		//Byte stuffing
		if(buf[i] == FLAG_RCV || buf[i] == ESC){
			frame[frameIndex++] = ESC;
			frame[frameIndex++] = buf[i] ^ 0x20;
		} else {
			frame[frameIndex++] = buf[i];
		}

	}

	
	
	frame[frameIndex++] = BCC2;
	frame[frameIndex++] = FLAG_RCV;

	
	


	
	


    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // TODO

    return 1;
}

