// Sequential socket server - accepting one client at a time.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"

typedef enum { WAIT_FOR_MSG, IN_MSG } ProcessingState;

void serve_connection(int sockfd) {
    if(send(sockfd, "*", 1, 0) < 1){ //initiate transmission of a message from the specified socket to its peer, if 0 bytes sent or error (-1), end connection
        perror_die("send");
    }

    ProcessingState state = WAIT_FOR_MSG;

    while(1){
        uint8_t buf[1024];
        int len = recv(sockfd, buf, sizeof buf, 0); //returns length of message (in bytes) written to the buffer
        if(len < 0){      //error
            perror_die("recv");
        } 
        else if(len == 0){ //no data received
            break;
        }

        for(int i=0;i<len;++i){
            switch (state) {
            case WAIT_FOR_MSG:
                if(buf[i] == '^'){
                    state = IN_MSG;
                }
                break;
            
            case IN_MSG:
                if(buf[i] == '$'){
                    state = WAIT_FOR_MSG;
                }
                else{
                    buf[i] += 1;
                    if(send(sockfd, &buf[i], 1, 0) < 1) {
                        perror("send error");
                        close(sockfd);
                        return;
                    }
                }
                break;
            }
        }
    }
    close(sockfd);
}

int main(int argc, char** argv){
    //setvbuf(FILE *stream, char *buffer, int mode, size_t size) - Changes the buffering mode of the given file stream
    //if buffer is NULL, resizes to internal buffer ti size argument
    //mode - _IONBF - no buffering(data written immediately)
    //size - size of buffer
    //returns 0 on success, non-zero on failure
    setvbuf(stdout, NULL, _IONBF, 0);

    int portnum = 9090;
    if(argc >=2){
        portnum = atoi(argv[1]); //atoi - convert string to int
    }
    printf("Serving on port %d\n", portnum);

    int sockfd = listen_inet_socket(portnum); //create a socket and listen on portnum

    while(1){
        struct sockaddr_in peer_addr;
        socklen_t peer_addr_len = sizeof(peer_addr);

        //The accept() function shall extract the first connection on the queue of pending connections, 
        //create a new socket with the same socket type protocol and address family as the specified socket, 
        //and allocate a new file descriptor for that socket.
        int newsockfd = accept(sockfd, (struct sockaddr*)&peer_addr, &peer_addr_len);

        if (newsockfd < 0){  //if error
            perror_die("ERROR on accept");
        }

        report_peer_connected(&peer_addr, peer_addr_len);
        serve_connection(newsockfd);
        printf("peer done\n");
    }

    return 0;
}

