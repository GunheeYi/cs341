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

  const char* hello = "hello";
  const char* whoami = "whoami";
  const char* whoru = "whoru";

  int sock_serv = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in servAddr;
  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = inet_addr(bind_ip);
  servAddr.sin_port = htons(port); 

  if( bind(sock_serv, (struct sockaddr*)&servAddr, sizeof(servAddr)) != 0 ){
    printf("server bind error\n");
    close(sock_serv);
    return -1;
  }

  if( listen(sock_serv, 10) != 0){
    printf("server listen error\n");
    close(sock_serv);
    return -1;
  }

  char buf[1025];

  int sock_client;
  struct sockaddr_in clientAddr;
  socklen_t clientAddrLen = sizeof(clientAddr);

  printf("server listening\n");
  while (true){
    if( (sock_client = accept(sock_serv, (struct sockaddr*)&clientAddr, &clientAddrLen) ) == -1 ){
      printf("server accept fail\n");
      close(sock_client);
      close(sock_serv);
      return -1;
    }

    int recvSiz;
    recvSiz = read(sock_client, buf, sizeof(buf) - 1);
    if(recvSiz == -1){
      printf("server read fail\n");
      close(sock_client);
      close(sock_serv);
      return -1;
    }
    
    buf[recvSiz] = '\0';

    if( strcmp(buf, hello) == 0){
      if( write(sock_client, server_hello, sizeof(server_hello)) == -1 ){
        printf("server write fail(hello)\n");
        close(sock_client);
        close(sock_serv);
        return -1;
      }
      submitAnswer(inet_ntoa(clientAddr.sin_addr), buf);

    }else if(strcmp(buf, whoami) == 0){
      char* ip = inet_ntoa(clientAddr.sin_addr);
      char responseStr[sizeof(ip) + 1];
      strcpy(responseStr, ip);
      responseStr[-1] = '\n';
      if( write(sock_client, responseStr , sizeof(responseStr)) == -1 ){
        printf("server write fail(whoami)\n");
        close(sock_client);
        close(sock_serv);
        return -1;
      }
      responseStr[-1] = '\0';
      submitAnswer(inet_ntoa(clientAddr.sin_addr), buf);

    }else if(strcmp(buf, whoru) == 0){
      char* ip = inet_ntoa(servAddr.sin_addr);
      char responseStr[sizeof(ip) + 1];
      strcpy(responseStr, ip);
      responseStr[-1] = '\n';
      if( write(sock_client, responseStr , sizeof(responseStr)) == -1 ){
        printf("server write fail(whoru)\n");
        close(sock_client);
        close(sock_serv);
        return -1;
      }
      responseStr[-1] = '\0';
      submitAnswer(inet_ntoa(clientAddr.sin_addr), buf);

    }else{
      if( write(sock_client, buf, recvSiz) == -1 ){
        printf("server write fail(echo)\n");
        close(sock_client);
        close(sock_serv);
        return -1;
      }
      submitAnswer(inet_ntoa(clientAddr.sin_addr), buf);
    }
    printf("server termination\n");
    close(sock_client);
    close(sock_serv);
    return 0;
  }

}

int EchoAssignment::clientMain(const char *server_ip, int port,
                               const char *command) {
  // Your client code
  // !IMPORTANT: do not use global variables and do not define/use functions
  // !IMPORTANT: for all system calls, when an error happens, your program must
  // return. e.g., if an read() call return -1, return -1 for clientMain.
  
  struct sockaddr_in servAddr;
  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = inet_addr(server_ip);
  servAddr.sin_port = htons(port); 

  int sock_client = socket(AF_INET, SOCK_STREAM, 0);
  
  if( connect(sock_client, (struct sockaddr*)&servAddr, sizeof(servAddr)) != 0){
    close(sock_client);
    return -1;
  }

  if( write(sock_client, command, sizeof(command)) == -1){
    close(sock_client);
    return -1;
  }

  char buf[1025];
  int recvSiz;
  if( (recvSiz = read(sock_client, buf, sizeof(buf) - 1) ) == -1){
    close(sock_client);
    return -1;
  }
  buf[recvSiz] = '\0';
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
