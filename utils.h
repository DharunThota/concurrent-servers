// Utility functions for socket servers in C.
//
// Eli Bendersky [http://eli.thegreenplace.net]
// This code is in the public domain.
#ifndef UTILS_H
#define UTILS_H

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _GNU_SOURCE
#include <netdb.h>

#define N_BACKLOG 64

// Dies (exits with a failure status) after printing the given printf-like
// message to stdout.
void die(char* fmt, ...);

// Wraps malloc with error checking: dies if malloc fails.
void* xmalloc(size_t size);

// Dies (exits with a failure status) after printing the current perror status
// prefixed with msg.
void perror_die(char* msg);

// Reports a peer connection to stdout. sa is the data populated by a successful
// accept() call.
void report_peer_connected(const struct sockaddr_in* sa, socklen_t salen);

// Creates a bound and listening INET socket on the given port number. Returns
// the socket fd when successful; dies in case of errors.
int listen_inet_socket(int portnum);

// Sets the given socket into non-blocking mode.
void make_socket_non_blocking(int sockfd);

#endif /* UTILS_H */

// Utility functions for socket servers in C.
//
// Eli Bendersky [http://eli.thegreenplace.net]
// This code is in the public domain.

void die(char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  exit(EXIT_FAILURE);
}

void* xmalloc(size_t size) {
  void* ptr = malloc(size);
  if (!ptr) {
    die("malloc failed");
  }
  return ptr;
}

void perror_die(char* msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

void report_peer_connected(const struct sockaddr_in* sa, socklen_t salen) {
  char hostbuf[NI_MAXHOST];
  char portbuf[NI_MAXSERV];
  if (getnameinfo((struct sockaddr*)sa, salen, hostbuf, NI_MAXHOST, portbuf,
                  NI_MAXSERV, 0) == 0) {
    printf("peer (%s, %s) connected\n", hostbuf, portbuf);
  } else {
    printf("peer (unknonwn) connected\n");
  }
}

int listen_inet_socket(int portnum) {
	//int socket(int domain, int type, int protocol);
	//AF_INET is used to represent the IPv4 address of the client to which a connection should be made
	//SOCK_STREAM uses the TCP - provides a reliable byte stream of data flow  
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror_die("ERROR opening socket");
	}

	//int setsockopt(int socket_descriptor, int level, int option_name, const void *value_of_option, socklen_t option_length);
	//level -  level at which the option for the socket must be applied, SOL_SOCK - apply at socket level
	//option_name - rules or options that should be modified for the socket, SO_REUSEADDR - enable the reusing of local addresses in bind()
	//value_of_option - specify the value for the options set in the option_name parameter.
	//returns 0 on success, -1 on failure
	// This helps avoid spurious EADDRINUSE when the previous instance of this
	// server died.
	int opt = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		perror_die("setsockopt");
	}

	//https://www.gta.ufrj.br/ensino/eel878/sockets/sockaddr_inman.html
	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY; //used when we dont want to bind a socket to any specific ip
	serv_addr.sin_port = htons(portnum); //converts the unsigned short integer hostshort from host byte order to network byte order.

	//assign address to the socket
	//int bind(int socket_descriptor , const struct sockaddr *address, socklen_t length_of_address);
	//typecasting serv_addr to a pointer of type sockaddr
	//returns 0 on success, -1 on failure
	if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
		perror_die("ERROR on binding");
	}

	//make the server node wait and listen for connections from the client node on the port and address specified by the bind() function
	// int listen(int socket_descriptor, int back_log);
	//back_log - maximum number of connection requests that can be made to the server by client nodes at a time
	//N_BACKLOG - 64
	if (listen(sockfd, N_BACKLOG) < 0) {
		perror_die("ERROR on listen");
	}

	return sockfd;
}

void make_socket_non_blocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags == -1) {
    perror_die("fcntl F_GETFL");
  }

  if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror_die("fcntl F_SETFL O_NONBLOCK");
  }
}