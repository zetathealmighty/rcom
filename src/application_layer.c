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
    
    
    
    
}
