#include "EchoAssignment.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <arpa/inet.h>

// !IMPORTANT: allowed system calls.
// !DO NOT USE OTHER NETWORK SYSCALLS (send, recv, select, poll, epoll, fork
// etc.)
//  * socket
//  * bind
//  * listen
//  * accept
//  * read
//  * write
//  * close
//  * getsockname
//  * getpeername
// See below for their usage.
// https://github.com/ANLAB-KAIST/KENSv3/wiki/Misc:-External-Resources#linux-manuals

int EchoAssignment::serverMain(const char *bind_ip, int port,
                               const char *server_hello) {
  // Your server code
  // !IMPORTANT: do not use global variables and do not define/use functions
  // !IMPORTANT: for all system calls, when an error happens, your program must
  // return. e.g., if an read() call return -1, return -1 for serverMain.

  int BUFFER_SIZE = 1024;

  int sock_serv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock_serv < 0) return -1;
  int sock_client;

  struct sockaddr_in servAddr, clientAddr;
  socklen_t servAddrSize = sizeof(servAddr);
  socklen_t clientAddrSize = sizeof(clientAddr);

  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = inet_addr(bind_ip);
  servAddr.sin_port = htons(port); 

  if( bind(sock_serv, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0 ) return -1;

  if( listen(sock_serv, 50) < 0) return -1;

  while (true){
    if( (sock_client = accept(sock_serv, (struct sockaddr*)&servAddr, &servAddrSize) ) < 0 ) return -1;

    char command[BUFFER_SIZE];
    memset(command, 0, BUFFER_SIZE);

    if( read(sock_client, command, BUFFER_SIZE) < 0) return -1;

    if (getsockname(sock_client, (sockaddr *)&servAddr, &servAddrSize) < 0 ) return -1;
    if (getpeername(sock_client, (sockaddr *)&clientAddr, &clientAddrSize) < 0 ) return -1;

    submitAnswer(inet_ntoa(clientAddr.sin_addr), command);

    if( strcmp(command, "hello") == 0 ){
      if( write(sock_client, server_hello, strlen(server_hello)) < 0 ) return -1;
    }
    else if ( strcmp(command, "whoami") == 0){
      char* clientIP = inet_ntoa(clientAddr.sin_addr);
      if( write(sock_client, clientIP, strlen(clientIP)) < 0 ) return -1;
    }
    else if (strcmp(command, "whoru") == 0 ) {
      char* serverIP = inet_ntoa(servAddr.sin_addr);
      if( write(sock_client, serverIP, strlen(serverIP)) < 0 ) return -1;
    }
    else {
      if( write(sock_client, command, BUFFER_SIZE) < 0 ) return -1;
    }
    if (close(sock_client) < 0) return -1;
  }
  if (close(sock_serv) < 0) return -1;
  return 0;
}

int EchoAssignment::clientMain(const char *server_ip, int port,
                               const char *command) {
  // Your client code
  // !IMPORTANT: do not use global variables and do not define/use functions
  // !IMPORTANT: for all system calls, when an error happens, your program must
  // return. e.g., if an read() call return -1, return -1 for clientMain.

  int BUFFER_SIZE = 1024;
  
  struct sockaddr_in servAddr;
  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = inet_addr(server_ip);
  servAddr.sin_port = htons(port); 

  int sock_client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock_client < 0) return -1;
  
  if( connect(sock_client, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0) return -1;

  if( write(sock_client, command, strlen(command)) < 0) return -1;

  char buf[BUFFER_SIZE];
  memset(buf, 0, BUFFER_SIZE);
  if( read(sock_client, buf, BUFFER_SIZE) < 0 ) return -1;

  submitAnswer(inet_ntoa(servAddr.sin_addr), buf);

  return 0;
}

static void print_usage(const char *program) {
  printf("Usage: %s <mode> <ip-address> <port-number> <command/server-hello>\n"
         "Modes:\n  c: client\n  s: server\n"
         "Client commands:\n"
         "  hello : server returns <server-hello>\n"
         "  whoami: server returns <client-ip>\n"
         "  whoru : server returns <server-ip>\n"
         "  others: server echos\n"
         "Note: each command is terminated by newline character (\\n)\n"
         "Examples:\n"
         "  server: %s s 0.0.0.0 9000 hello-client\n"
         "  client: %s c 127.0.0.1 9000 whoami\n",
         program, program, program);
}

int EchoAssignment::Main(int argc, char *argv[]) {

  if (argc == 0)
    return 1;

  if (argc != 5) {
    print_usage(argv[0]);
    return 1;
  }

  int port = atoi(argv[3]);
  if (port == 0) {
    printf("Wrong port number\n");
    print_usage(argv[0]);
  }

  switch (*argv[1]) {
  case 'c':
    return clientMain(argv[2], port, argv[4]);
  case 's':
    return serverMain(argv[2], port, argv[4]);
  default:
    print_usage(argv[0]);
    return 1;
  }
}