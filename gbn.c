#include "gbn.h"

state_t s;

void handleTimeout(int signal){
    printf("Timeout occured\n");
}


//initialize system state
int gbn_init(){

    //initializing system with state CLOSED
    s = *(state_t*)malloc(sizeof(s));
    s.seqnum = (uint8_t)rand();
    s.window=1;
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

    int bufLen = sizeof(currPacket->type) + sizeof(currPacket->seqnum) + sizeof(currPacket->data);
    uint16_t buffer[bufLen];
    buffer[0]=(uint16_t)currPacket->seqnum;
    buffer[1]=(uint16_t)currPacket->type;
    memcpy(buffer+2,currPacket->data, sizeof(currPacket->data));
    return checksum(buffer, (bufLen / sizeof(uint16_t)));

}

void gbn_createHeader(uint8_t type, uint8_t seqnum, gbnhdr *currPacket){

    memset(currPacket->data,'\0', DATALEN);
    currPacket->type= type;
    currPacket->seqnum=seqnum;
    currPacket->checksum=header_checksum(currPacket);
}

ssize_t gbn_send(int sockfd, const void *buf, size_t len, int flags){

    printf("Sending data \n");

    // data packet
    gbnhdr *dataPacket = malloc(sizeof(*dataPacket));

    //initialize data acknowledgement packet
    gbnhdr *dataAckPacket = malloc(sizeof(*dataAckPacket));

    //storage for remote address
    struct sockaddr from;
    socklen_t fromLen = sizeof(from);

    socklen_t serverLen = sizeof(s.remote_address);

    int attempts=0;
    int i,j;
    for(i=0;i<len;){
        int unack_packets=0;

        switch(s.system_state){
            case CLOSED:gbn_close(sockfd);
                         break;

            case ESTABLISHED:

                //send complete window size in one attempt
                for(j=0;j<s.window;j++){

                    if((len-i-(DATALEN-2)*j) >0){
                        //calulate length to data to put in packet- either DATALEN-2 or remaining data
                        size_t data_length = min(len-i-(DATALEN-2)*j, DATALEN-2);

                        //initializing header of data packet
                        gbn_createHeader(DATA,s.seqnum + (uint8_t)j,dataPacket);

                        memcpy(dataPacket->data, (uint16_t *) &data_length, 2);
                        memcpy(dataPacket->data + 2, buf + i + (DATALEN-2)*j, data_length);
                        dataPacket->checksum = header_checksum(dataPacket);

                        //Sending Data
                        if(attempts<5){
                            if(maybe_sendto(sockfd,dataPacket, sizeof(*dataPacket),0, &s.remote_address, serverLen)==-1){
                                printf("Error in sending data. %d \n", errno);
                                s.system_state = CLOSED;
                                break;
                            }
                        }else{
                            printf("Connection appears broken\n");
                            s.system_state=CLOSED;
                            return(-1);
                        }

                        printf("Data packet with seqnum %d and checksum %d sent\n", dataPacket->seqnum, dataPacket->checksum);
                        //Data sent
                        unack_packets++;
                    }
                }//end of loop for sliding window

                attempts++;

                //get acknowledgements and handle all possible messages from server
                size_t ack_packets=0;
                for(j=0; j<unack_packets;j+=ack_packets){

                    if(recvfrom(sockfd,dataAckPacket, sizeof(*dataAckPacket),0,&from,&fromLen) >= 0) {

                        if (dataAckPacket->type == DATAACK && dataAckPacket->checksum == header_checksum(dataAckPacket)) {

                            printf("Data Ack packet with seqnum %d and checksum %d recevied\n", dataAckPacket->seqnum, dataAckPacket->checksum);

                            //check for duplicate acknowledgements
                            int diff = ((int)dataAckPacket->seqnum - (int)s.seqnum);
                            ack_packets = diff>=0? (size_t)(diff): (size_t)(diff+256);

                            unack_packets-=ack_packets;
                            s.seqnum = dataAckPacket->seqnum;

                            i+= min(len-i-(DATALEN-2)*j, DATALEN-2); // update looping variable for data sent

                            if (s.window < MAXWINDOWSIZE) {
                                s.window++;
                                printf("Entering Fast Mode\n");
                            }
                            if (unack_packets == 0) {
                                alarm(0);//remove alarm
                            } else {
                                //reset alarm
                                alarm(TIMEOUT);
                            }

                        } else if(dataAckPacket->type == FIN && dataAckPacket->checksum == header_checksum(dataAckPacket)){
                            //fin received
                            attempts=0; //reset attempts to the beginning
                            s.system_state = FIN_RCVD;
                            alarm(0); // remove alarm
                            break;
                        }

                    }else{

                        if (errno == EINTR) {
                            //timeout received, hence reduce window size by half
                            if (s.window > 1) {
                                s.window/=2;
                                printf("Entering Slow Mode\n");
                                break;
                            }
                        } else {
                            s.system_state = CLOSED;
                            return(-1);
                        }
                    }
                }
                break;
            case SYN_RCVD: gbn_close(sockfd);
                            break;
            default: break;

        }//end of switch
    }//end of for

    free(dataPacket);
    free(dataAckPacket);

    if(s.system_state == ESTABLISHED){
        return len;
    }
    return -1;
 }


ssize_t gbn_recv(int sockfd, void *buf, size_t len, int flags){

    // data packet
    gbnhdr *dataPacket = malloc(sizeof(*dataPacket));

    //initialize data acknowledgement packet
    gbnhdr *dataAckPacket = malloc(sizeof(*dataAckPacket));

    //storage for remote address
    struct sockaddr from;
    socklen_t fromLen = sizeof(from);

    socklen_t remoteLen = sizeof(s.remote_address);

    int flag=0; //if new data received, break out of while
    int returnValue=0;// used to calculate the size of data received

    while(s.system_state == ESTABLISHED && flag==0){

        if(recvfrom(sockfd,dataPacket, sizeof(*dataPacket),0,&from,&fromLen) >= 0){

            printf("Packet with seqnum %d and checksum %d received\n", dataPacket->seqnum, dataPacket->checksum);

            if(dataPacket->type==DATA && dataPacket->checksum == header_checksum(dataPacket)){

                //create acknowledgment based on sequence number received
                if(dataPacket->seqnum == s.seqnum){
                    //correct sequence number
                    printf("Correct data \n");

                    //update sequence number
                    s.seqnum = dataPacket->seqnum + (uint8_t)1;

                    //store it in buffer
                    memcpy(buf, dataPacket->data+2, sizeof(dataPacket->data)-2);
                    returnValue+= sizeof(dataPacket->data);

                    //create acknowledgement
                    gbn_createHeader(DATAACK,s.seqnum,dataAckPacket);

                    flag=1;//used to break out of while
                    returnValue+= sizeof(dataPacket->data);

                }else{
                    //correct sequence number
                    printf("Incorrect data \n");

                    //incorrect sequence number. Create duplicate acknowledgement
                    gbn_createHeader(DATAACK,s.seqnum,dataAckPacket);
                }

                //send acknowledgment
                if(maybe_sendto(sockfd,dataAckPacket, sizeof(*dataAckPacket),0, &s.remote_address, remoteLen)==-1){
                    printf("Error in sending data acknowledgment. %d \n", errno);
                    s.system_state = CLOSED;
                    break;
                }else{
                    printf("Data Ack  with sequence number %d and checksum %d successfully sent \n", dataAckPacket->seqnum, dataAckPacket->checksum);
                }
            }else if(dataPacket->type == FIN && dataPacket->checksum==header_checksum(dataPacket)){

                s.seqnum = dataPacket->seqnum + (uint8_t)1;
                s.system_state = FIN_RCVD;
                break;
            }

        }else{
            if(errno!=EINTR){
                s.system_state=CLOSED;
                return(-1);
            }
        }
    }//end of while

    free(dataPacket);
    free(dataAckPacket);

    if(s.system_state == CLOSED){
        return(-1);
    }

    return returnValue;
}


int gbn_close(int sockfd){

    //create fin packet 1
    gbnhdr *finPacket1 = malloc(sizeof(*finPacket1));

    //create fin packet 2
    gbnhdr *finPacket2 = malloc(sizeof(*finPacket2));

    //create fin acknowledge packet
    gbnhdr *finAckPacket1 = malloc(sizeof(*finAckPacket1));

    //create fin acknowledge packet
    gbnhdr *finAckPacket2 = malloc(sizeof(*finAckPacket2));

    struct sockaddr from;
    socklen_t fromlen = sizeof(from);
    socklen_t socklen = sizeof(s.remote_address);

    int attempts=0;

    while(s.system_state!=CLOSED){

        switch (s.system_state){

            case FIN_RCVD: //send fin acknowledge for fin received
                            gbn_createHeader(FINACK,s.seqnum,finAckPacket1);
                            if(sendto(sockfd, finAckPacket1, sizeof(*finAckPacket1), 0, &s.remote_address, socklen) >=0) {

                                printf("Fin acknowledgment1 sent\n");
                                alarm(0);//reset alarm

                                //check if fin ack from other end received to close connection
                                if(finAckPacket2->type == FINACK && finAckPacket2->checksum == header_checksum(finAckPacket2)){
                                    //both sets of fin and fin-ack exist. Close conenction
                                    s.system_state = CLOSED;
                                }else{
                                    //needs to send fin
                                    s.system_state=ESTABLISHED;
                                }
                            } else {
                                printf("Error in sending Fin Acknowledgment\n");
                                s.system_state = CLOSED;
                            }
                            break;
            case ESTABLISHED: //send fin
                            finPacket2->checksum = header_checksum(finPacket2);
                            gbn_createHeader(FIN,s.seqnum,finPacket2);

                            if(attempts<5){

                                if(sendto(sockfd, finPacket2, sizeof(*finPacket2), 0, &s.remote_address, socklen) >=0) {
                                    printf("Fin2 sent\n");

                                } else {
                                    printf("Error in sending Fin\n");
                                    s.system_state=CLOSED;
                                    return(-1);
                                }
                            }else{
                                printf("Connection appears broken\n");
                                s.system_state=CLOSED;
                                return(-1);
                            }
                            attempts++;
                            alarm(TIMEOUT);

                            //receive FIN ACK
                            if (recvfrom(sockfd, finAckPacket2, sizeof(*finAckPacket2), 0, &from, &fromlen)>=0) {

                                if (finAckPacket2->type == FINACK && finAckPacket2->checksum == header_checksum(finAckPacket2)) {

                                    printf("Fin Ack 2 received\n");
                                    //check if fin ack from other end received to close connection
                                    if(finAckPacket1->type == FINACK && finAckPacket1->checksum == header_checksum(finAckPacket1)){
                                        //close connection
                                        s.system_state = CLOSED;
                                        break;
                                    }else{
                                        //needs to receive fin
                                        printf("Waiting for FIN\n");
                                        s.system_state=FIN_SENT;
                                        break;
                                    }
                                }
                            } else {
                                if(errno != EINTR) {
                                    printf("Error in receiving Fin Ack 2");
                                    s.system_state = CLOSED;
                                    break;
                                }
                            }
                            break;
            case FIN_SENT: //receive fin
                            if (recvfrom(sockfd, finPacket1, sizeof(*finPacket1), 0, &from, &fromlen) >=0) {
                                if (finPacket1->type == FIN && finPacket1->checksum == header_checksum(finPacket1)) {
                                    //fin received send acknowledgment
                                    printf("Fin1 received\n");
                                    s.seqnum = finPacket1->seqnum + (uint8_t)1;
                                    s.system_state = FIN_RCVD;
                                }

                            } else {
                                if(errno != EINTR) {
                                    printf("Error in receiving FIN\n");
                                    s.system_state = CLOSED;
                                    break;
                                }

                            }
                            break;

            default: break;
        }
    }

    free(finPacket1);
    free(finPacket2);
    free(finAckPacket1);
    free(finAckPacket2);

    if(s.system_state == CLOSED){
        printf("Bye Bye. Conenction Closing.\n");
        return close(sockfd);
    }else{
        return(-1);
    }
}


int gbn_connect(int sockfd, const struct sockaddr *server, socklen_t socklen){

    /*create packets for handshake*/

    //create syn packet
    gbnhdr *synPacket = malloc(sizeof(*synPacket));
    gbn_createHeader(SYN,s.seqnum, synPacket);

    //create syn ack packet
    gbnhdr *synAckPacket = malloc(sizeof(*synAckPacket));

    //create data ack packet
    gbnhdr *dataAckPacket = malloc(sizeof(*dataAckPacket));

    struct sockaddr from;
    socklen_t fromLen = sizeof(from);

    int attempts = 0;
    s.system_state=SYN_SENT;

    while(s.system_state!= ESTABLISHED && s.system_state!=CLOSED){

        if(s.system_state==SYN_SENT && attempts<5){
            //send syn
            if(sendto(sockfd,synPacket, sizeof(*synPacket),0,server,socklen) == -1){
                perror("Couldn't send syn packet");
                s.system_state=CLOSED;
                return (-1);
            }
            //syn sent successfully
            alarm(TIMEOUT);
            attempts++;

        }
        else{
            if(attempts>=5)
                printf("Connection appears broken\n");
            else
                printf("Some Problem occurred\n");
            s.system_state=CLOSED;
            alarm(0);
            return (-1);
        }

        //receive acknowledgement
        if(recvfrom(sockfd,synAckPacket, sizeof(*synAckPacket),0,&from,&fromLen) >= 0){
            if(synAckPacket->type == SYNACK && synAckPacket->checksum == header_checksum(synAckPacket)){
                printf("SYNACK received successfully\n");
                s.seqnum = synAckPacket->seqnum;
                gbn_createHeader(DATAACK,synAckPacket->seqnum,dataAckPacket);
                s.remote_address = *server;

                //sending ack from client to server for three way handshake
                if(sendto(sockfd,dataAckPacket, sizeof(*dataAckPacket),0,server,socklen) == -1){
                    perror("Couldn't send data ack packet to server\n");
                    s.system_state=CLOSED;
                    return (-1);
                }else{
                    s.system_state = ESTABLISHED;
                    alarm(0); // reset the alarm
                }
            }

        }else{
            //if timeout, try again
            if(errno!=EINTR){
                printf("Error in receiving SYN acknowledgement\n");
                s.system_state=CLOSED;
                return (-1);
            }
        }
    }//end of while

    free(synPacket);
    free(synAckPacket);
    free(dataAckPacket);

    if(s.system_state == ESTABLISHED){
        printf("Woot Woot. Connected successfully\n");
        return SUCCESS;
    }

    return(-1);
}


int gbn_listen(int sockfd, int backlog){
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

    s.system_state = CLOSED; // start state

    /*create packets for handshake*/

    //create syn packet
    gbnhdr *synPacket = malloc(sizeof(*synPacket));

    //create syn ack packet
    gbnhdr *synAckPacket = malloc(sizeof(*synAckPacket));
    gbn_createHeader(SYNACK,0, synAckPacket);

    //create data ack packet
    gbnhdr *dataAckPacket = malloc(sizeof(*dataAckPacket));
    gbn_createHeader(DATAACK,0,dataAckPacket);

    int attempts =0;

    while(s.system_state!=ESTABLISHED){
        if(s.system_state == CLOSED){
            //wait for syn
            if(recvfrom(sockfd,synPacket, sizeof(*synPacket),0,client,socklen)>= 0){

                if(synPacket->type == SYN && synPacket->checksum == header_checksum(synPacket)) {
                    printf("SYN recieved successfully\n");
                    s.system_state = SYN_RCVD;
                    s.seqnum = synPacket->seqnum + (uint8_t) 1;
                }

            }else{

                printf("Error in receiving SYN\n");
                s.system_state=CLOSED;
                break;
            }
        } else if(s.system_state == SYN_RCVD){

            //setting sequence number for acknowledgement
            synAckPacket->seqnum = s.seqnum;
            synAckPacket->checksum = header_checksum(synAckPacket);

            if(attempts<5){
                //send syn ack
                if(sendto(sockfd,synAckPacket, sizeof(*synAckPacket),0,client,*socklen) == -1){
                    perror("Couldn't send syn packet\n");
                    s.system_state=CLOSED;
                    return (-1);
                }
            }else{
                printf("Connection seems broken\n");
                s.system_state=CLOSED;
                return (-1);
            }
            //syn ack successfully sent
            attempts++;
            alarm(TIMEOUT);

            //receive data ack packet form client
            if(recvfrom(sockfd,dataAckPacket, sizeof(*dataAckPacket),0,client,socklen) >= 0){

                if(dataAckPacket->type == DATAACK && dataAckPacket->checksum == header_checksum(dataAckPacket)) {
                    printf("DATA ACK received successfully\n");
                    s.system_state = ESTABLISHED;
                    s.remote_address = *client;
                    break;
                }
            }else{
                //if timeout try again
                if(errno!=EINTR){
                    printf("Error receiving data ack from client\n");
                    s.system_state=CLOSED;
                    return (-1);
                }
            }
        }//end of elseif
    }//end of while

    free(synPacket);
    free(synAckPacket);
    free(dataAckPacket);

    if(s.system_state == ESTABLISHED){
        printf("Server has accepted the connection successfully\n");
        alarm(0); //reset alarm
        return sockfd;
    }

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
