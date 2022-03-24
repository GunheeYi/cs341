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
  //printf("SERVER HELLO: %s\n", server_hello);


  const char* hello = "hello";
  const char* whoami = "whoami";
  const char* whoru = "whoru";

  int sock_serv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  int sock_client;

  struct sockaddr_in servAddr, clientAddr;
  socklen_t servAddrSize = sizeof(servAddr);
  socklen_t clientAddrSize = sizeof(clientAddr);

  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = inet_addr(bind_ip);
  servAddr.sin_port = htons(port); 

  if( bind(sock_serv, (struct sockaddr*)&servAddr, sizeof(servAddr)) != 0 ){
    //printf("server bind error\n");
    close(sock_serv);
    return -1;
  }

  if( listen(sock_serv, 10) != 0){
    //printf("server listen error\n");
    close(sock_serv);
    return -1;
  }

  char buf[BUFSIZ];
  while (true){

    // accept client's connect(); servAddr is overwrote with server IP included
    if( (sock_client = accept(sock_serv, (struct sockaddr*)&servAddr, &servAddrSize) ) < 0 ){
      //printf("server accept fail\n");
      close(sock_client);
      close(sock_serv);
      return -1;
    }

    // get clientAddr with client IP included
    if (getpeername(sock_client, (struct sockaddr*)&clientAddr, &clientAddrSize) < 0 ){
      //printf("server getpeername fail\n");
      close(sock_client);
      continue;
    }

    // read client's message
    int recvLength;
    while(true){
      //printf("mallang:receive new message\n");
      if( (recvLength = read(sock_client, buf, BUFSIZ)) < 0){
        //printf("server read fail\n");
        close(sock_client);
        break;
      }else if(recvLength == 0){ //tcp connection terminated.
        close(sock_client);
        break;
      }
      buf[recvLength] = '\0';
      // printf("SERVER RECEIVED: %s\n", buf);

      // if "hello" requested, send string stored in server_hello
      if( strncmp(buf, hello, strlen(hello)) == 0 ){
        if( write(sock_client, server_hello, strlen(server_hello)) < 0 ){
          //printf("server write fail (hello)\n");
          close(sock_client);
          break;
        }
        submitAnswer(inet_ntoa(clientAddr.sin_addr), hello);
      }
      // if "whoami" requested, send client's IP address
      else if ( strncmp(buf, whoami, strlen(whoami)) == 0 ){
        char* ip = inet_ntoa(clientAddr.sin_addr);
        size_t l = strlen(ip);
        if( write(sock_client, ip, l+1) == -1 ){
          //printf("server write fail (whoami)\n");
          close(sock_client);
          break;
        }
        submitAnswer(inet_ntoa(clientAddr.sin_addr), whoami);
      }
      // if "whoru" requested, send server's IP address
      else if (strncmp(buf, whoru, strlen(whoru)) == 0) {
        struct sockaddr_in submitAddr;
        socklen_t submitAddrLen = sizeof(submitAddr);
        getsockname(sock_client, (sockaddr *)&submitAddr, &submitAddrLen);
        
        char* ip = inet_ntoa(submitAddr.sin_addr);
        size_t l = strlen(ip);
        if( write(sock_client, ip, l+1) == -1 ){
          //printf("server write fail (whoami)\n");
          close(sock_client);
          break;
        }
        submitAnswer(inet_ntoa(clientAddr.sin_addr), whoru);
      }
      // for other requests, just serve echo response
      else {
        if( write(sock_client, buf, BUFSIZ) == -1 ){
          //printf("server write fail(echo)\n");
          close(sock_client);
          break;
        }
        //printf("SERVER SENDING: %s\n", buf);
        submitAnswer(inet_ntoa(clientAddr.sin_addr), buf);
      }
      break;
    }
  }

  close(sock_serv);
  //printf("server termination\n");
  return 0;
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

  int sock_client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  
  if( connect(sock_client, (struct sockaddr*)&servAddr, sizeof(servAddr)) != 0){
    close(sock_client);
    return -1;
  }

  //printf("CLIENT SENDING: %s\n", command);
  if( write(sock_client, command, strlen(command)) == -1){
    close(sock_client);
    return -1;
  }

  char buf[BUFSIZ];
  int readLength;
  if( (readLength = read(sock_client, buf, BUFSIZ)) < 0 ){
    close(sock_client);
    return -1;
  }
  buf[readLength] = '\0';
  //printf("CLIENT RECEIVED: %s\n", buf);

  struct sockaddr_in submitAddr;
  socklen_t submitAddrLen = sizeof(submitAddr);
  getpeername(sock_client, (sockaddr *)&submitAddr, &submitAddrLen);
  submitAnswer(inet_ntoa(submitAddr.sin_addr), buf);

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