#include "gbn.h"

void handleTimeOut(int signal){
	//TODO handle timeout condition
    printf("Error happened");
}

//initialize system state
int initialize(){

	/*----- Randomizing the seed. This is used by the rand() function -----*/
	srand((unsigned)time(0));

	s.systemState=CLOSED;
	
	/*-----initialize signal handler-----*/
	timeoutAction.sa_handler=handleTimeOut;

	//initialize all signals
	if (sigfillset(&timeoutAction.sa_mask)< 0){
		perror("sigfillset failed");
		return(-1);
	}

	timeoutAction.sa_flags =0;

	if (sigaction(SIGALRM,&(timeoutAction),0)<0){
		perror("Signal action failed to be assigned");
		return(-1);
	}

	return SUCCESS;
}

void gbn_createHeader(int type, int seqnum, int checksum,gbnhdr *currPacket){

	currPacket->checksum=checksum;
	currPacket->type=type;
	currPacket->seqnum=seqnum;
}

uint16_t checksum(uint16_t *buf, int nwords)
{
	uint32_t sum;

	for (sum = 0; nwords > 0; nwords--)
		sum += *buf++;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	return ~sum;
}

ssize_t gbn_send(int sockfd, const void *buf, size_t len, int flags){

	/* Hint: Check the data length field 'len'.
	 *       If it is > DATALEN, you will have to split the data
	 *       up into multiple packets - you don't have to worry
	 *       about getting more than N * DATALEN.
	 */

	return(-1);
}

ssize_t gbn_recv(int sockfd, void *buf, size_t len, int flags){

	return (-1);
}

int gbn_close(int sockfd){

    if(close(sockfd)== -1){
        perror(gai_strerror(errno));
        return(-1);
    }
    else{
        return SUCCESS;
    }

}

int gbn_connect(int sockfd, const struct sockaddr *server, socklen_t socklen){

    //send syn packet to server
    gbnhdr synPacket;
    memset(&synPacket,0, sizeof(synPacket));
    int seqnum = rand();

    gbn_createHeader(SYN,seqnum,0, &synPacket);

    if(sendto(sockfd,&synPacket, sizeof(synPacket),0,server,socklen) == -1){
        perror("Couldnt send syn packet");
        return(-1);
    }

    //set timer and system state
    alarm(TIMEOUT); //start timer
    s.systemState = SYN_SENT;

    //connect to server
    if (connect(sockfd, server, socklen) == -1) {
        perror("Error connecting to socket");
        return (-1);
    }

    return SUCCESS;
}

int gbn_listen(int sockfd, int backlog){

    printf("Waiting for packet on socket %d", sockfd);

	for(;;){
        //receive syn packet and return success. if timeout, continue listening, if interrupt, return error
    }
    return SUCCESS;
}

int gbn_bind(int sockfd, const struct sockaddr *server, socklen_t socklen){

    if(bind(sockfd, server, socklen) == -1){
        perror(gai_strerror(errno));
        return(-1);
    }
    else{
        return SUCCESS;
    }
}	

int gbn_socket(int domain, int type, int protocol){

	//initialize seed and timeout condition
	if(initialize()== -1){
        return(-1);
    }

	int sockfd;
	sockfd = socket(domain, type, protocol);
	return sockfd; //sockfd will return -1 if error, hence error is not explicitly checked here

}

int gbn_accept(int sockfd, struct sockaddr *client, socklen_t *socklen){

	/* TODO: Your code here. */

	return(-1);
}

ssize_t maybe_sendto(int  s, const void *buf, size_t len, int flags, \
                     const struct sockaddr *to, socklen_t tolen){

	char *buffer = malloc(len);
	memcpy(buffer, buf, len);
	
	
	/*----- Packet not lost -----*/
	if (rand() > LOSS_PROB*RAND_MAX){
		/*----- Packet corrupted -----*/
		if (rand() < CORR_PROB*RAND_MAX){
			
			/*----- Selecting a random byte inside the packet -----*/
			int index = (int)((len-1)*rand()/(RAND_MAX + 1.0));

			/*----- Inverting a bit -----*/
			char c = buffer[index];
			if (c & 0x01)
				c &= 0xFE;
			else
				c |= 0x01;
			buffer[index] = c;
		}

		/*----- Sending the packet -----*/
		int retval = sendto(s, buffer, len, flags, to, tolen);
		free(buffer);
		return retval;
	}
	/*----- Packet lost -----*/
	else
		return(len);  /* Simulate a success */
}
