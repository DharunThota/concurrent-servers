// Asynchronous socket server - accepting multiple clients concurrently, multiplexing the connections with epoll.

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"

#define MAXFDS 16 * 1024

typedef enum { INITIAL_ACK, WAIT_FOR_MSG, IN_MSG } ProcessingState;

#define SENDBUF_SIZE 1024

typedef struct {
    ProcessingState state;
    uint8_t sendbuf[SENDBUF_SIZE];
    int sendbuf_end;
    int sendptr;
} peer_state_t;

peer_state_t global_state[MAXFDS];

typedef struct {
    bool want_read;
    bool want_write;
} fd_status_t;

const fd_status_t fd_status_R = {.want_read = true, .want_write = false};
const fd_status_t fd_status_W = {.want_read = false, .want_write = true};
const fd_status_t fd_status_RW = {.want_read = true, .want_write = true};
const fd_status_t fd_status_NORW = {.want_read = false, .want_write = false};

fd_status_t on_peer_connected(int sockfd, const struct sockaddr_in* peer_addr, socklen_t peer_addr_len){
    assert(sockfd < MAXFDS);
    report_peer_connected(peer_addr, peer_addr_len);

    peer_state_t* peerstate = &global_state[sockfd];
    peerstate->state = INITIAL_ACK;
    peerstate->sendbuf[0] = '*';
    peerstate->sendbuf_end = 1;
    peerstate->sendptr = 0;

    return fd_status_W;
}

fd_status_t on_peer_ready_recv(int sockfd){
    assert(sockfd < MAXFDS);
    peer_state_t* peerstate = &global_state[sockfd];

    if(peerstate->state == INITIAL_ACK || peerstate->sendptr < peerstate->sendbuf_end){
        return fd_status_W;
    }

    uint8_t buf[1024];
    int nbytes = recv(sockfd, buf, sizeof buf, 0);
    if (nbytes == 0) {
        return fd_status_NORW;
    } else if (nbytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return fd_status_R;
        } 
        else {
            perror_die("recv");
        }
    }
    bool ready_to_send = false;
    for (int i = 0; i < nbytes; ++i) {
        switch (peerstate->state) {
            case INITIAL_ACK:
                assert(0 && "can't reach here");
                break;
            case WAIT_FOR_MSG:
                if (buf[i] == '^') {
                    peerstate->state = IN_MSG;
                }
                break;
            case IN_MSG:
                if (buf[i] == '$') {
                    peerstate->state = WAIT_FOR_MSG;
                }
                else {
                    assert(peerstate->sendbuf_end < SENDBUF_SIZE);
                    peerstate->sendbuf[peerstate->sendbuf_end++] = buf[i] + 1;
                    ready_to_send = true;
                }
                break;
        }
    }

    return (fd_status_t){.want_read = !ready_to_send, .want_write = ready_to_send};
}

fd_status_t on_peer_ready_send(int sockfd) {
    assert(sockfd < MAXFDS);
    peer_state_t* peerstate = &global_state[sockfd];

    if (peerstate->sendptr >= peerstate->sendbuf_end) {
        return fd_status_RW;
    }
    int sendlen = peerstate->sendbuf_end - peerstate->sendptr;
    int nsent = send(sockfd, &peerstate->sendbuf[peerstate->sendptr], sendlen, 0);
    if (nsent == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return fd_status_W;
        } 
        else {
            perror_die("send");
        }
    }
    if (nsent < sendlen) {
        peerstate->sendptr += nsent;
        return fd_status_W;
    }
    else {
        peerstate->sendptr = 0;
        peerstate->sendbuf_end = 0;

        if (peerstate->state == INITIAL_ACK) {
            peerstate->state = WAIT_FOR_MSG;
        }

        return fd_status_R;
    }
}

int main(int argc, char** argv){
    setvbuf(stdout, NULL, _IONBF, 0);

    int portnum = 9090;
    if (argc >= 2) {
        portnum = atoi(argv[1]);
    }
    printf("Serving on port %d\n", portnum);

    int listener_sockfd = listen_inet_socket(portnum);
    make_socket_non_blocking(listener_sockfd);

    int epollfd = epoll_create1(0);
    if(epollfd < 0){
        perror_die("epoll_create1");
    }

    struct epoll_event accept_event;
    accept_event.data.fd = listener_sockfd;
    accept_event.events = EPOLLIN;
    //This system call performs control operations on the epoll(7) instance 
    //returns zero on success, -1 on error and errno is set appropriately.
    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, listener_sockfd, &accept_event) < 0){
        perror_die("epoll_ctl EPOLL_CTL_ADD");
    }

    struct epoll_event* events = calloc(MAXFDS, sizeof(struct epoll_event));
    if(events == NULL){
        die("Unable to allocate memory for epoll_events");
    }

    //https://linux.die.net/man/4/epoll
    while(1){
        int nready = epoll_wait(epollfd, events, MAXFDS, -1);
        for(int i=0;i<nready;i++){
            if(events[i].events & EPOLLERR){
                perror_die("epoll_wait returned EPOLLERR");
            }

            if(events[i].data.fd == listener_sockfd){
                // The listening socket is ready; this means a new peer is connecting.
                struct sockaddr_in peer_addr;
                socklen_t peer_addr_len = sizeof(peer_addr);
                int newsockfd = accept(listener_sockfd, (struct sockaddr*)&peer_addr, &peer_addr_len);
                if(newsockfd < 0){
                    if(errno == EAGAIN || errno == EWOULDBLOCK){
                        printf("accept returned EAGAIN or EWOULDBLOCK");
                    }
                    else{
                        perror_die("accept");
                    }
                }
                else{
                    make_socket_non_blocking(newsockfd);
                    if(newsockfd >= MAXFDS){
                        die("socket fd (%d) >= MAXFDS (%d)", newsockfd, MAXFDS);
                    }

                    fd_status_t status = on_peer_connected(newsockfd, &peer_addr, peer_addr_len);
                    struct epoll_event event = {0};
                    event.data.fd = newsockfd;
                    if(status.want_read){
                        event.events |= EPOLLIN;
                    }
                    if(status.want_write){
                        event.events |= EPOLLOUT;
                    }

                    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, newsockfd, &event) < 0){
                        perror_die("epoll_ctl EPOLL_CTL_ADD");
                    }
                }
            }
            else{
                if(events[i].events & EPOLLIN){
                    int fd = events[i].data.fd;
                    fd_status_t status = on_peer_ready_recv(fd);
                    struct epoll_event event = {0};
                    event.data.fd = fd;
                    if(status.want_read){
                        event.events |= EPOLLIN;
                    }
                    if(status.want_write){
                        event.events |= EPOLLOUT;
                    }
                    if(event.events == 0){
                        printf("socket %d closing\n", fd);
                        if(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0){
                            perror_die("epoll_ctl EPOLL_CTL_DEL");
                        }
                        close(fd);
                    }
                    else if(epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) < 0){
                        perror_die("epoll_ctl EPOLL_CTL_MOD");
                    }
                }
                else if(events[i].events & EPOLLOUT){
                    int fd = events[i].data.fd;
                    fd_status_t status = on_peer_ready_send(fd);
                    struct epoll_event event = {0};
                    event.data.fd = fd;

                    if(status.want_read){
                        event.events |= EPOLLIN;
                    }
                    if(status.want_write){
                        event.events |= EPOLLOUT;
                    }
                    if(event.events == 0){
                        printf("socket %d closing\n", fd);
                        if(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0){
                            perror_die("epoll_ctl EPOLL_CTL_DEL");
                        }
                        close(fd);
                    }
                    else if(epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) < 0){
                        perror_die("epoll_ctl EPOLL_CTL_MOD");
                    }
                }
            }
        }
    }
    
    return 0;
}