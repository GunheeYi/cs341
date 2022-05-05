/*
 * E_TCPAssignment.hpp
 *
 *  Created on: 2014. 11. 20.
 *      Author: Keunhong Lee
 */

#ifndef E_TCPASSIGNMENT_HPP_
#define E_TCPASSIGNMENT_HPP_

#include <E/Networking/E_Host.hpp>
#include <E/Networking/E_Networking.hpp>
#include <E/Networking/E_TimerModule.hpp>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#define IP_START 14
#define TCP_START 34
#define HANDSHAKE_PACKET_SIZE 54

#define SYN 0b10
#define ACK 0b10000
#define TWOMEGA 2097152

namespace E {

enum TCPState {
  TCP_CLOSED,
  TCP_LISTEN,
  TCP_SYN_SENT,
  TCP_SYN_RCVD,
  TCP_ESTABLISHED,
  TCP_FIN_WAIT_1,
  TCP_FIN_WAIT_2,
  TCP_CLOSE_WAIT,
  TCP_CLOSING,
  TCP_LAST_ACK,
  TCP_TIME_WAIT
};

struct socket {
  TCPState state;
  sockaddr_in localAddr;
  sockaddr_in remoteAddr;
  bool binded;
  UUID timerUUID;
  int syscallUUID;
};

struct backlog {
  int capacity;
  int current;
  std::queue<int> q;
};

struct timerPayload {
  SystemCallInterface::SystemCall from;
  int syscallUUID;
  int pid;
  int fd;
  sockaddr* addrPtr;
  socklen_t* addrLenPtr;
  socklen_t addrLen;
};

class TCPAssignment : public HostModule,
                      private RoutingInfoInterface,
                      public SystemCallInterface,
                      public TimerModule {
private:
  virtual void timerCallback(std::any payload) final;

public:
  TCPAssignment(Host &host);
  virtual void initialize();
  virtual void finalize();
  virtual ~TCPAssignment();

protected:
  virtual void systemCallback(UUID syscallUUID, int pid, const SystemCallParameter &param) final;
  virtual void packetArrived(std::string fromModule, Packet &&packet) final;

  void syscall_socket(UUID, int, int, int, int);
  void syscall_close(UUID, int, int);
  void syscall_read(UUID, int, int, char*, int);
  void syscall_write(UUID, int, int, char*, int);
  void syscall_connect(UUID, int, int, sockaddr*, socklen_t);
  void syscall_listen(UUID, int, int, int);
  void syscall_accept(UUID, int, int, sockaddr*, socklen_t*);
  void syscall_bind(UUID, int, int, sockaddr*, socklen_t);
  void syscall_getsockname(UUID, int, int, sockaddr*, socklen_t*);
  void syscall_getpeername(UUID, int, int, sockaddr*, socklen_t*);

  std::map<int, std::map<int, socket>> socketMap;
  std::map<int, backlog> backlogMap; // waiting queue는 pid 당 하나만 있으면 됨

};

class DataBlock{
public:
  unsigned int seq, bufPos;
  unsigned int len;
};

class ReceiveBuffer{
  std::vector<DataBlock> blockList;
  char* buffer;
public:
  unsigned int lastOffset;
  size_t readStart;
  size_t readEnd;

  ReceiveBuffer();
  unsigned int addPacket(char* source, size_t len, unsigned int seq);
};

class TCPAssignmentProvider {
private:
  TCPAssignmentProvider() {}
  ~TCPAssignmentProvider() {}

public:
  static void allocate(Host &host) { host.addHostModule<TCPAssignment>(host); }
};

} // namespace E

#endif /* E_TCPASSIGNMENT_HPP_ */