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
#define TCP_HEADER_SIZE 20
#define MAX_SEGMENT_SIZE 1460
#define TWO_MEGA 2097152
#define READ_BUFFER_SIZE TWO_MEGA
#define WRITE_BUFFER_SIZE TWO_MEGA

#define SYN 0b10
#define ACK 0b10000

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

enum TimerFrom {
  TIMER_FROM_BLAHBLAH,
  TIMER_FROM_SOCKET,
  TIMER_FROM_CLOSE,
  TIMER_FROM_READ,
  TIMER_FROM_WRITE,
  TIMER_FROM_CONNECT,
  TIMER_FROM_LISTEN,
  TIMER_FROM_ACCEPT,
  TIMER_FROM_BIND,
  TIMER_FROM_GETSOCKNAME,
  TIMER_FROM_GETPEERNAME,
  TIMER_FROM_HANDSHAKE
};

struct readBufMarker {
  size_t start;
  size_t end;
};

struct socket {
  TCPState state;
  sockaddr_in localAddr;
  sockaddr_in remoteAddr;
  bool binded;
  UUID connect_timerUUID;
  UUID handshake_timerUUID;
  UUID write_timerUUID;
  int connect_syscallUUID;
  int write_syscallUUID;

  char* readBuf;
  char* writeBuf;
  uint32_t readStart;
  uint32_t readEnd;
  uint32_t readBufOffset; // (starting sequence number)
  bool readBufOffsetSet;
  std::list<readBufMarker> readBufMarkers;
  uint32_t seq;
  uint32_t ack;

  uint32_t writeSent;
};

struct backlog { // 용어 개선 가능하다
  int capacity;
  int current;
  std::queue<int> q;
};

struct timerPayload {
  TimerFrom from;
  int syscallUUID;
  int pid;
  int fd;

  // CONNECT
  sockaddr* connect_addrPtr;
  socklen_t connect_addrLen;

  // ACCEPT
  sockaddr* accept_addrPtr;
  socklen_t* accept_addrLenPtr;
  socklen_t accept_addrLen;

  // READ
  void* read_start;
  uint32_t read_len;

  // WRITE
  void* write_start;
  uint32_t write_len;

  // HANDSHAKE
  Packet handshake_packet;
  socket* handshake_socketPtr;
  
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
  void syscall_read(UUID, int, int, void*, uint32_t);
  void syscall_write(UUID, int, int, void*, uint32_t);
  void syscall_connect(UUID, int, int, sockaddr*, socklen_t);
  void syscall_listen(UUID, int, int, int);
  void syscall_accept(UUID, int, int, sockaddr*, socklen_t*);
  void syscall_bind(UUID, int, int, sockaddr*, socklen_t);
  void syscall_getsockname(UUID, int, int, sockaddr*, socklen_t*);
  void syscall_getpeername(UUID, int, int, sockaddr*, socklen_t*);

  std::map<int, std::map<int, socket>> socketMap;
  std::map<int, backlog> backlogMap; // waiting queue는 pid 당 하나만 있으면 됨

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