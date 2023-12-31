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

#define FLAG_RCV 0x7E
#define A_TX 0x03
#define A_RX 0x01
#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x0B
#define C_I0 0x00
#define C_I1 0x40
#define C_RR0 0x05
#define C_REJ0 0x01
#define C_RR1 0x85
#define C_REJ1 0x81

#define ESC 0x7D

typedef enum {
	START,
	FLAG,
	A,
	C,
	BCC1,
	BCC2,
	STOP_,
	DISC
} llState;

volatile int STOP = FALSE;

int alarmTriggered = FALSE;
int alarmCount = 0;
int timeout = 0;
int attempts = 0;
int trama = 1;

int fd;

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
	
	fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);
	
	if (fd < 0) {
        perror(connectionParameters.serialPort);
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
    newtio.c_cc[VMIN] = 0;
    
    tcflush(fd, TCIOFLUSH);
    
    if (tcsetattr(fd, TCSANOW, &newtio) == -1) {
        perror("tcsetattr");
        exit(-1);
    }
    
    unsigned char curr;
    timeout = connectionParameters.timeout;
    attempts = connectionParameters.nRetransmissions;
    
    switch(connectionParameters.role) {
    	case LlRx: {
			while(state != STOP_) {
				if(read(fd, &curr, 1) > 0) {
					switch(state) {
						case START: {
							if(curr == FLAG_RCV) state = FLAG;
							break;
						} case FLAG: {
							if(curr == A_TX) state = A;
							else if(curr != FLAG_RCV) state = START;
							break;
						} case A: {
							if(curr == C_SET) state = C;
							else if(curr == FLAG_RCV) state = FLAG;
							else state = START;
							break;
						} case C: {
							if(curr == (A_TX ^ C_SET)) state = BCC1;
							else if(curr == FLAG_RCV) state = FLAG;
							else state = START;
							break;
						} case BCC1: {
							if(curr == FLAG_RCV) state = STOP_;
							else state = START;
							break;
						} default: {
							break;
						}    						
					}
				}
			}
    			
			unsigned char buf[5] = {FLAG_RCV, A_RX, C_UA, A_RX ^ C_UA, FLAG_RCV};
			write(fd, buf, 5);
			break;
				
    	} case LlTx: {
    		(void) signal(SIGALRM, alarmHandler);
    		
    		while(connectionParameters.nRetransmissions != 0 && state != STOP_) {
    			unsigned char buf[5] = {FLAG_RCV, A_TX, C_SET, A_TX ^ C_SET, FLAG_RCV};
    			write(fd, buf, 5);
    			
    			alarm(connectionParameters.timeout);
    			alarmTriggered = FALSE;
    			
    			while(alarmTriggered == FALSE && state != STOP_) {
    				if(read(fd, &curr, 1) > 0) {
    					switch(state) {
    						case START: {
    							if(curr == FLAG_RCV) state = FLAG;
    							break;
    						} case FLAG: {
    							if(curr == A_RX) state = A;
    							else if(curr != FLAG_RCV) state = START;
    							break;
    						} case A: {
    							if(curr == C_UA) state = C;
    							else if(curr == FLAG_RCV) state = FLAG;
    							else state = START;
    							break;
    						} case C: {
    							if(curr == (A_RX ^ C_UA)) state = BCC1;
    							else if(curr == FLAG_RCV) state = FLAG;
    							else state = START;
    							break;
    						} case BCC1: {
    							if(curr == FLAG_RCV) state = STOP_;
    							else state = START;
    							break;
    						} default: {
    							break;
    						}    						
    					}
    				}
    			} 
    			
    			connectionParameters.nRetransmissions--;
    		} if(state != STOP_) return -1;
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
int llwrite(const unsigned char *buf, int bufSize)
{
	unsigned char* frame = (unsigned char*) malloc(2 * bufSize + 6);
	//Header
    frame[0] = FLAG_RCV;
    frame[1] = A_TX;
    frame[2] = trama ? C_I1 : C_I0;
    frame[3] = frame[1] ^ frame[2];
    
    trama = !trama;

	//Calculate BCC2
	unsigned char BCC2 = buf[0];
	for(int i = 1; i < bufSize; i++){
		BCC2 ^= buf[i];
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
	
	if(BCC2 == FLAG_RCV || BCC2 == ESC) {
		BCC2 ^= 0x20;
	}
	frame[frameIndex++] = BCC2;
	frame[frameIndex++] = FLAG_RCV;

	frame = realloc(frame, frameIndex);

	int check_OK = FALSE;
	int bytes_sent = 0;
	unsigned char curr = 0;
	unsigned char save = 0;
	int count = 0;

	(void) signal(SIGALRM, alarmHandler);
	
	while(count < attempts){
		count++;

		llState state = START;
		bytes_sent = write(fd, frame, frameIndex + 1);

		//Wait until all bytes have been wrtien
		sleep(1);

		alarm(timeout);
		alarmTriggered = FALSE;	

		while(state != STOP_ && alarmTriggered == FALSE){		
			if(read(fd, &curr, 1) > 0){
				switch(state){
					case START: {
						if(curr == FLAG_RCV) state = FLAG;
						break;
					} case FLAG: {
						if(curr == A_RX){
							state = A;
						} else if(curr == FLAG_RCV){
							state = FLAG;
						} else {
							state = START;
						}
						break;
					} case A: {
						if(curr == C_RR0 || curr == C_RR1 || curr == C_REJ0 || curr == C_REJ1 || curr == C_DISC){
							state = C;
							save = curr;
						}else if(curr == FLAG_RCV){
							state = FLAG;
						} else {
							state = START;
						}
						break;
					} case C: {
						if(curr == (save ^ A_RX)){
							state = BCC1;
						} else if(curr == FLAG_RCV){
							state = FLAG;
						} else {
							state = START;
						}
						break;
					} case BCC1: {
						if(curr == FLAG_RCV){
							state = STOP_;
						} else {
							state = START;
						}
						break;
					} default: {
						break;
					}
				}
			}
		}
		
		
		if(save == C_RR0 || save == C_RR1) {
			check_OK = TRUE;
			break;
		} else if(save == C_REJ0 || save == C_REJ1) {
			continue;
		}
	}
	
	free(frame);
	
	if(check_OK){
		return bytes_sent;
	} else {
		llclose(fd);
		return -1;
	}
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
	llState state = START;

	unsigned char curr, save;
	unsigned int where = 0;

    while(state != STOP_ && alarmTriggered == FALSE){
		if(read(fd, &curr, 1) > 0){
			switch(state){
				case START: {
					if(curr == FLAG_RCV) state = FLAG;
					break;
				} case FLAG: {
					if(curr == A_TX){
						state = A;
					} else if(curr == FLAG_RCV){
						state = FLAG;
					} else {
						state = START;
					}
					break;
				} case A: {
					if(curr == C_I0 || curr == C_I1 || curr == C_DISC) {
						state = C;
						save = curr;
					}else if(curr == FLAG_RCV){
						state = FLAG;
					} else {
						state = START;
					}
					break;
				} case C: {
					if(curr == (save ^ A_TX)){
						if(save == C_DISC) {

							unsigned char frame[5] = {FLAG_RCV, A_RX, C_DISC, A_RX ^ C_DISC, FLAG_RCV};
							write(fd, frame, 5);
							return 0;
						}
						state = BCC1;
					} else if(curr == FLAG_RCV){
						state = FLAG;
					} else {
						state = START;
					}
					break;
				} case BCC1: {
					if(curr == ESC) {
						read(fd, &curr, 1);
						packet[where++] = curr ^ 0x20;
					} else if(curr == FLAG_RCV) {
						unsigned char bcc = packet[--where]; // x bcc2 flag
						packet[where] = '\0';

						unsigned char cmp = packet[0];

						for(int i = 1; i < where; i++) {
							cmp ^= packet[i];
						}

						if(bcc == cmp) {
							state = STOP_;
							unsigned char which = (save == C_I0) ? C_RR0 : C_RR1;

							unsigned char frame[5] = {FLAG_RCV, A_RX, which, A_RX ^ which, FLAG_RCV};
							write(fd, frame, 5);

							return where;
						} else {
							unsigned char which = (save == C_I0) ? C_REJ0 : C_REJ1;
							unsigned char frame[5] = {FLAG_RCV, A_RX, which, A_RX ^ which, FLAG_RCV};
							write(fd, frame, 5);

							return -1;
						}
					} else {
						packet[where++] = curr;
					}
					break;
				} default: {
					break;
				}
			}
		}
	}
    return -1;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    llState state = START;
	unsigned char curr;

	(void) signal(SIGALRM, alarmHandler);

	while(attempts != 0 && state != STOP_) {
		unsigned char frame[5] = {FLAG_RCV, A_TX, C_DISC, A_TX ^ C_DISC, FLAG_RCV};

		write(fd, frame, 5);

		alarm(timeout);
		alarmTriggered = FALSE;

		while(alarmTriggered == FALSE && state != STOP) {
			if(read(fd, &curr, 1) > 0) {
				switch(state) {
					case START: {
						if(curr == FLAG_RCV) state = FLAG;
						break;
					} case FLAG: {
						if(curr == A_RX) state = A;
						else if(curr != FLAG_RCV) state = START;
						break;
					} case A: {
						if(curr == C_DISC) state = C;
						else if(curr == FLAG_RCV) state = FLAG;
						else state = START;
						break;
					} case C: {
						if(curr == (A_RX ^ C_DISC)) state = BCC1;
						else if(curr == FLAG_RCV) state = FLAG;
						else state = START;
						break;
					} case BCC1: {
						if(curr == FLAG_RCV) state = STOP_;
						else state = START;
						break;
					} default: {
						break;
					}
				}
			} 
		} attempts--;
	}

	if(state != STOP_) {
		return -1;
	}

	unsigned char frame[5] = {FLAG_RCV, A_TX, C_UA, A_TX ^ C_UA, FLAG_RCV};

	write(fd, frame, 5);

	return close(fd);
}
