#include "gbn.h"

state_t s;

void handleTimeout(int signal){
	//TODO handle timeout condition
    printf("Timeout occured");
}


//initialize system state
int gbn_init(){

    //initializing system with state CLOSED
    s = *(state_t*)malloc(sizeof(s));
    s.seqnum = (uint8_t)rand();
    s.system_state =CLOSED;

    //set up timeout handler
    signal(SIGALRM,handleTimeout);
    siginterrupt(SIGALRM,1);

	return SUCCESS;
}


uint16_t checksum(uint16_t *buf, int nwords)
{
	uint32_t sum;

	for (sum = 0; nwords > 0; nwords--)
		sum += *buf++;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	return (uint16_t) ~sum;
}

uint16_t header_checksum(gbnhdr *currPacket){

    int packetlength = sizeof(currPacket->type) + sizeof(currPacket->data) + sizeof(currPacket->seqnum);

    uint16_t  buffer[packetlength];
    memcpy(buffer,currPacket->data, sizeof(currPacket->data));
    buffer[sizeof(currPacket->data)+1]=currPacket->seqnum;
    buffer[sizeof(currPacket->data)+1]=currPacket->type;

    return checksum(buffer,packetlength);

}

void gbn_createHeader(uint16_t type, uint16_t seqnum, gbnhdr *currPacket){

    memset(currPacket->data,'\0',0);
    currPacket->checksum=header_checksum(currPacket);
    currPacket->type= type;
    currPacket->seqnum=seqnum;
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

    //create syn packet
    gbnhdr *synPacket = malloc(sizeof(*synPacket));
    gbn_createHeader(SYN,s.seqnum, synPacket);

    //create syn ack packet
    gbnhdr *synAckPacket = malloc(sizeof(*synAckPacket));

    //create data ack packet
    gbnhdr *dataAckPacket = malloc(sizeof(*dataAckPacket));
    gbn_createHeader(DATAACK,0,dataAckPacket);

    //storage for server address
    struct sockaddr from;
    socklen_t fromLen = sizeof(from);

    int attempts = 0;
    s.system_state=SYN_SENT;

    while(s.system_state!= ESTABLISHED && s.system_state!=CLOSED){
        printf("%d",attempts);
        if(s.system_state==SYN_SENT && attempts<5){
            //send syn
            if(sendto(sockfd,&synPacket, sizeof(synPacket),0,server,socklen) == -1){
                perror("Couldn't send syn packet");
                s.system_state=CLOSED;
                break;
            }
            //syn sent successfully
            alarm(TIMEOUT);
            attempts++;

        }
        else{
            if(attempts>=5)
                printf("Connection appears broken");
            else
                printf("Some Problem occured");
            s.system_state=CLOSED;
            break;
        }

        //receive acknowledgement
        if(recvfrom(sockfd,&synAckPacket, sizeof(synAckPacket),0,&from,&fromLen) >0){
            if(synAckPacket->type == SYNACK && synAckPacket->checksum == header_checksum(synAckPacket)){
                printf("SYNACK recieved successfully");
                s.system_state = ESTABLISHED;
                s.seqnum = synAckPacket->seqnum;
                dataAckPacket->seqnum = synAckPacket->seqnum;
                dataAckPacket->checksum = synAckPacket->checksum;
                //maybe store server address in system state

                //sending ack from client to server for three way handshake
                if(sendto(sockfd,&dataAckPacket, sizeof(dataAckPacket),0,server,socklen) == -1){
                    perror("Couldn't send data ack packet to server");
                    s.system_state=CLOSED;
                    break;
                }
            }
        }else{
            //if timeout, send syn again
            if(errno==EINTR){
                continue;
            }
            else{
                printf("Error in receiving SYN acknowledgement");
                s.system_state=CLOSED;
                break;
            }
        }

    }

    free(synPacket);
    free(synAckPacket);
    free(dataAckPacket);

    if(s.system_state == ESTABLISHED){
        printf("Connected successfully");
        return SUCCESS;
    }

    return(-1);
}


int gbn_listen(int sockfd, int backlog){
    //bypassing listen function
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

    /*----- Randomizing the seed. This is used by the rand() function -----*/
    srand((unsigned)time(0));

    //initialize timer and system state for each socket
    if(gbn_init()== -1){
        return(-1);
    }

	int sockfd;
	sockfd = socket(domain, type, protocol);
	return sockfd; //sockfd will return -1 if error, hence error is not explicitly checked here

}


int gbn_accept(int sockfd, struct sockaddr *client, socklen_t *socklen){

    return (-1);
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
