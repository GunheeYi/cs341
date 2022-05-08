/*
 * E_TCPAssignment.cpp
 *
 *  Created on: 2014. 11. 20.
 *      Author: Keunhong Lee
 */

#include "TCPAssignment.hpp"
#include <E/E_Common.hpp>
#include <E/Networking/E_Host.hpp>
#include <E/Networking/E_NetworkUtil.hpp>
#include <E/Networking/E_Networking.hpp>
#include <E/Networking/E_Packet.hpp>
#include <cerrno>

namespace E {

TCPAssignment::TCPAssignment(Host &host)
    : HostModule("TCP", host), RoutingInfoInterface(host),
      SystemCallInterface(AF_INET, IPPROTO_TCP, host),
      TimerModule("TCP", host) {}

TCPAssignment::~TCPAssignment() {}

void TCPAssignment::initialize() {}

void TCPAssignment::finalize() {}

void TCPAssignment::systemCallback(UUID syscallUUID, int pid, const SystemCallParameter &param) {
  switch (param.syscallNumber) {
  case SOCKET:
    this->syscall_socket(
      syscallUUID, pid, 
      std::get<int>(param.params[0]), std::get<int>(param.params[1]), std::get<int>(param.params[2])
    );
    break;
  case BIND:
    this->syscall_bind(
      syscallUUID, pid, std::get<int>(param.params[0]),
      static_cast<struct sockaddr *>(std::get<void *>(param.params[1])),
      (socklen_t)std::get<int>(param.params[2])
    );
    break;
  case LISTEN:
    this->syscall_listen(
      syscallUUID, pid, 
      std::get<int>(param.params[0]),
      std::get<int>(param.params[1])
    );
    break;
  case ACCEPT:
    this->syscall_accept(
      syscallUUID, pid, std::get<int>(param.params[0]),
      static_cast<struct sockaddr *>(std::get<void *>(param.params[1])),
      static_cast<socklen_t *>(std::get<void *>(param.params[2]))
    );
    break;
  case CONNECT:
    this->syscall_connect(
      syscallUUID, pid, std::get<int>(param.params[0]), 
      static_cast<struct sockaddr *>(std::get<void *>(param.params[1])),
      (socklen_t)std::get<int>(param.params[2])
    );
    break;
  case READ:
    // this->syscall_read(syscallUUID, pid, std::get<int>(param.params[0]),
    //                    std::get<void *>(param.params[1]),
    //                    std::get<int>(param.params[2]));
    break;
  case WRITE:
    // this->syscall_write(syscallUUID, pid, std::get<int>(param.params[0]),
    //                     std::get<void *>(param.params[1]),
    //                     std::get<int>(param.params[2]));
    break;
  case CLOSE:
    this->syscall_close(syscallUUID, pid, std::get<int>(param.params[0]));
    break;
  case GETSOCKNAME:
    this->syscall_getsockname(
      syscallUUID, pid, std::get<int>(param.params[0]),
      static_cast<struct sockaddr *>(std::get<void *>(param.params[1])),
      static_cast<socklen_t *>(std::get<void *>(param.params[2]))
    );
    break;
  case GETPEERNAME:
    this->syscall_getpeername(
      syscallUUID, pid, std::get<int>(param.params[0]),
      static_cast<struct sockaddr *>(std::get<void *>(param.params[1])),
      static_cast<socklen_t *>(std::get<void *>(param.params[2]))
    );
    break;
  default:
    assert(0);
  }
}

void TCPAssignment::syscall_socket(UUID syscallUUID, int pid, int domain, int type, int protocol) {
  if (this->socketMap.find(pid) == this->socketMap.end()) {
    std::map<int, socket> sm;
    this->socketMap.insert(
      std::pair<int, std::map<int, socket>>(pid, sm)
    );
  }
  int fd = createFileDescriptor(pid);
  struct socket s;
  s.state = TCP_CLOSED;
  s.binded = false;
  s.readBuf = malloc(READ_BUFFER_SIZE);
  s.writeBuf = malloc(WRITE_BUFFER_SIZE);
  s.readStart = 0;
  s.readEnd = 0;
  
  this->socketMap[pid].insert(std::pair<int, socket>(fd, s));
  this->returnSystemCall(syscallUUID, fd);
};

void TCPAssignment::syscall_bind(UUID syscallUUID, int pid, int fd, sockaddr* addrPtr, socklen_t addrLen) {
  if (this->socketMap[pid][fd].binded) {
    this->returnSystemCall(syscallUUID, -1);
    return;
  }
  // 다른 pid의 socket에다 bind하려는 경우 허용해야하나?
  sockaddr_in* addrPtr_in = (sockaddr_in*) addrPtr;
  for (std::map<int, socket>::iterator it = this->socketMap[pid].begin(); it != this->socketMap[pid].end(); it++) {
    if (
      it->second.binded &&
      (
        it->second.localAddr.sin_addr.s_addr == addrPtr_in->sin_addr.s_addr ||
        it->second.localAddr.sin_addr.s_addr == INADDR_ANY ||
        addrPtr_in->sin_addr.s_addr == INADDR_ANY
      ) && 
      it->second.localAddr.sin_port == addrPtr_in->sin_port
    ) {
      this->returnSystemCall(syscallUUID, -1);
      return;
    }
  }
  memcpy(&this->socketMap[pid][fd].localAddr, addrPtr, addrLen);
  this->socketMap[pid][fd].binded = true;
  this->returnSystemCall(syscallUUID, 0);
};

void TCPAssignment::syscall_listen(UUID syscallUUID, int pid, int fd, int capacity) {
  if (!this->socketMap[pid][fd].binded) {
    this->returnSystemCall(syscallUUID, -1);
    return;
  }
  this->backlogMap[pid].capacity = capacity;
  this->backlogMap[pid].current = 0;
  while ( !this->backlogMap[pid].q.empty() ) this->backlogMap[pid].q.pop();
  this->socketMap[pid][fd].state = TCP_LISTEN;
  this->returnSystemCall(syscallUUID, 0);
};

void TCPAssignment::syscall_accept(UUID syscallUUID, int pid, int fd, sockaddr* addrPtr, socklen_t* addrLenPtr) {
  if ( this->backlogMap[pid].q.empty() ) {
    timerPayload* tp = (timerPayload*) malloc(sizeof(timerPayload));
    tp->from = ACCEPT;
    tp->syscallUUID = syscallUUID;
    tp->pid = pid;
    tp->fd = fd;
    tp->addrPtr = addrPtr;
    tp->addrLenPtr = addrLenPtr;
    this->addTimer(tp, 1000000000);
    return;
  }

  int fdToAccept = this->backlogMap[pid].q.front();
  this->backlogMap[pid].q.pop();
  assert(this->socketMap[pid].find(fdToAccept) != this->socketMap[pid].end());
  
  sockaddr_in* addrPtr_in = (sockaddr_in*) addrPtr;
  memcpy(addrPtr_in, &this->socketMap[pid][fdToAccept].remoteAddr, sizeof(sockaddr_in));
  *addrLenPtr = sizeof(sockaddr_in);

  this->returnSystemCall(syscallUUID, fdToAccept);
};

void TCPAssignment::syscall_connect(UUID syscallUUID, int pid, int fd, sockaddr* addrPtr, socklen_t addrLen) {
  sockaddr_in* addrPtr_in = (sockaddr_in*) addrPtr;

  this->backlogMap[pid].capacity = 1; // ????????????????

  std::array<uint8_t, 4> arr;
  for (int i = 0; i < 4; i++) arr[i] = addrPtr_in->sin_addr.s_addr >> (i * 8) & 0xFF;
  ipv4_t ipv4 = (ipv4_t) arr;
  int portSrc = this->getRoutingTable(ipv4);
  std::optional<ipv4_t> ipSrc = this->getIPAddr(portSrc);

  // TODO: 이미 bind된 소켓 중에 ip/port 겹치는 것 있는지 확인 필요?
  assert(this->socketMap.find(pid) != this->socketMap.end());
  assert(this->socketMap[pid].find(fd) != this->socketMap[pid].end());
  if (!this->socketMap[pid][fd].binded) {
    this->socketMap[pid][fd].localAddr.sin_family = AF_INET;
    this->socketMap[pid][fd].localAddr.sin_addr.s_addr = (uint32_t) NetworkUtil::arrayToUINT64(*ipSrc);
    this->socketMap[pid][fd].localAddr.sin_port = portSrc;
    this->socketMap[pid][fd].binded = true;
  }

  this->socketMap[pid][fd].remoteAddr.sin_family = AF_INET;
  this->socketMap[pid][fd].remoteAddr.sin_addr.s_addr = addrPtr_in->sin_addr.s_addr;
  this->socketMap[pid][fd].remoteAddr.sin_port = addrPtr_in->sin_port;
  
  uint8_t seq[4], ack[4]; // seq int여야함?
  seq[3] = 100;
  uint8_t headLen = 5 << 4, flags = SYN;
  uint16_t window = 51200, checksum = 0, urgent = 0, newChecksum;
  
  Packet p(HANDSHAKE_PACKET_SIZE);

  p.writeData(IP_START+12, &ipSrc, 4);
  p.writeData(IP_START+16, &addrPtr_in->sin_addr.s_addr, 4);
  p.writeData(TCP_START+0, &portSrc, 2);
  p.writeData(TCP_START+2, &addrPtr_in->sin_port, 2);
  p.writeData(TCP_START+4, &seq, 4);
  p.writeData(TCP_START+8, &ack, 4);
  p.writeData(TCP_START+12, &headLen, 1);
  p.writeData(TCP_START+13, &flags, 1);
  p.writeData(TCP_START+14, &window, 2);
  p.writeData(TCP_START+16, &checksum, 2);
  p.writeData(TCP_START+18, &urgent, 2);
  assert(checksum==0);

  uint8_t buf[HANDSHAKE_PACKET_SIZE];
  p.readData(0, buf, 54);
  assert(buf[TCP_START + 16] == 0);
  assert(buf[TCP_START + 17] == 0);
  newChecksum = NetworkUtil::tcp_sum(
    *(uint32_t *)&buf[IP_START+12], *(uint32_t *)&buf[IP_START+16],
    &buf[TCP_START], 20
  );
  newChecksum = ~newChecksum;
  uint8_t newChecksum1 = (newChecksum & 0xff00) >> 8;
  uint8_t newChecksum2 = (newChecksum & 0x00ff);
  p.writeData(TCP_START + 16, &newChecksum1, 1);
  p.writeData(TCP_START + 17, &newChecksum2, 1);
  
  this->sendPacket("IPv4", std::move(p));
  this->socketMap[pid][fd].state = TCP_SYN_SENT;

  timerPayload* tp = (timerPayload*) malloc(sizeof(timerPayload));
  tp->from = CONNECT;
  tp->syscallUUID = syscallUUID;
  this->socketMap[pid][fd].timerUUID = this->addTimer(tp, 1000000000);
  this->socketMap[pid][fd].syscallUUID = syscallUUID;

};

void TCPAssignment::syscall_read(UUID syscallUUID, int pid, int fd, char*, int size) {
  
};

void TCPAssignment::syscall_write(UUID syscallUUID, int pid, int fd, char*, int size) {

};

void TCPAssignment::syscall_close(UUID syscallUUID, int pid, int fd) {
  
  free(this->socketMap[pid][fd].readBuf);
  free(this->socketMap[pid][fd].writeBuf);

  // this->socketMap[pid][fd].state = TCP_CLOSED;
  this->removeFileDescriptor(pid, fd);
  this->socketMap[pid].erase(fd);

  this->returnSystemCall(syscallUUID, 0);
};

void TCPAssignment::syscall_getsockname(UUID syscallUUID, int pid, int fd, sockaddr* addrPtr, socklen_t* addrLenPtr) {
  if (
    this->socketMap.find(pid) == this->socketMap.end() ||
    this->socketMap[pid].find(fd) == this->socketMap[pid].end()
  ) {
    this->returnSystemCall(syscallUUID, -1);
    return;
  }
  memcpy(addrPtr, &this->socketMap[pid][fd].localAddr, *addrLenPtr);
  this->returnSystemCall(syscallUUID, 0);
};

void TCPAssignment::syscall_getpeername(UUID syscallUUID, int pid, int fd, sockaddr* addrPtr, socklen_t* addrLenPtr) {
  if (
    this->socketMap.find(pid) == this->socketMap.end() ||
    this->socketMap[pid].find(fd) == this->socketMap[pid].end()
  ) {
    this->returnSystemCall(syscallUUID, -1);
    return;
  }
  memcpy(addrPtr, &this->socketMap[pid][fd].remoteAddr, *addrLenPtr);
  this->returnSystemCall(syscallUUID, 0);
};

void TCPAssignment::packetArrived(std::string fromModule, Packet &&packet) {
  uint32_t ipSrc, ipDst;
  uint16_t portSrc, portDst;
  uint8_t seqBuf[4], ackBuf[4];
  uint32_t seq, ack, seqN;
  uint8_t headLen, flags;
  uint16_t window, checksum, urgent;
  size_t payloadLen;

  int randSeq = rand();
  uint8_t newHeadLen, newFlags;
  uint16_t newWindow, newChecksum, newUrgent;

  packet.readData(IP_START+12, &ipSrc, 4);
  packet.readData(IP_START+16, &ipDst, 4);
  packet.readData(TCP_START+0, &portSrc, 2);
  packet.readData(TCP_START+2, &portDst, 2);
  packet.readData(TCP_START+4, &seqBuf, 4);
  packet.readData(TCP_START+8, &ackBuf, 4);
  packet.readData(TCP_START+12, &headLen, 1);
  packet.readData(TCP_START+13, &flags, 1);
  packet.readData(TCP_START+14, &window, 2);
  packet.readData(TCP_START+16, &checksum, 2);
  packet.readData(TCP_START+18, &urgent, 2);

  seq = ntohl( *(uint32_t *)seqBuf );
  ack = ntohl( *(uint32_t *)ackBuf );

  headLen = (headLen & 0xf0) >> 2;

  payloadLen = packet.getSize() - (TCP_START + headLen);

  // std::cout << "seq: " << unsigned(seqBuf[0]) << " " << unsigned(seqBuf[1]) << " " << unsigned(seqBuf[2]) << " " << unsigned(seqBuf[3]) << "++++++++++++++++++++++++++++++++=" << std::endl;
  // std::cout << "seq: " << seq << std::endl;
  // std::cout << "headlen: " << unsigned(headLen) << "++++++++++++++++++++++++++++++++=" << std::endl;
  // // print packet size
  // std::cout << "payload size: " << unsigned(payloadLen) << std::endl;

  Packet p(HANDSHAKE_PACKET_SIZE);

  switch(flags) {
    case SYN:
    { 
      // packet에 dstIp, dstPort로 listening socket을 찾아
      // 새로운 socket 생성, 거기에 listening socket의 localAddr를 복사
      // packet의 srcIp, srcPort를 새로운 socket의 remoteAddr로 복사
      int pid = -1, listeningfd = -1;
      for (std::map<int, std::map<int, socket>>::iterator itPid = this->socketMap.begin(); itPid != this->socketMap.end(); itPid++) {
        for (std::map<int, socket>::iterator itFd = itPid->second.begin(); itFd != itPid->second.end(); itFd++) {
          if (
            (itFd->second.state == TCP_LISTEN || itFd->second.state == TCP_SYN_SENT) &&
            (
              itFd->second.localAddr.sin_addr.s_addr == INADDR_ANY ||
              itFd->second.localAddr.sin_addr.s_addr == ipDst 
            ) &&
            itFd->second.localAddr.sin_port == portDst
          ) {
            pid = itPid->first;
            listeningfd = itFd->first;
            break;
          }
        }
      }

      if (
        pid == -1 || 
        listeningfd == -1 || 
        this->backlogMap[pid].current >= this->backlogMap[pid].capacity
      ) return;

      this->backlogMap[pid].current++;

      int newfd = this->createFileDescriptor(pid);
      memcpy(&this->socketMap[pid][newfd].localAddr, &this->socketMap[pid][listeningfd].localAddr, sizeof(sockaddr_in));
      this->socketMap[pid][newfd].remoteAddr.sin_family = AF_INET;
      this->socketMap[pid][newfd].remoteAddr.sin_addr.s_addr = ipSrc;
      this->socketMap[pid][newfd].remoteAddr.sin_port = portSrc;
      this->socketMap[pid][newfd].state = TCP_SYN_RCVD;

      // seqBuf[3] = seqBuf[3] + 1;
      seq += 1;
      seqN = htonl(seq);
      newHeadLen = 5 << 4;
      newFlags = SYN | ACK;
      newWindow = 51200;
      newChecksum = 0;
      newUrgent = 0;
      p.writeData(IP_START+12, &ipDst, 4);
      p.writeData(IP_START+16, &ipSrc, 4);
      p.writeData(TCP_START+0, &portDst, 2);
      p.writeData(TCP_START+2, &portSrc, 2);
      p.writeData(TCP_START+4, &randSeq, 4);
      // p.writeData(TCP_START+8, &seqBuf, 4);
      p.writeData(TCP_START+8, &seqN, 4);
      p.writeData(TCP_START+12, &headLen, 1);
      p.writeData(TCP_START+13, &newFlags, 1);
      p.writeData(TCP_START+14, &newWindow, 2);
      p.writeData(TCP_START+16, &newChecksum, 2);
      p.writeData(TCP_START+18, &newUrgent, 2);

      break;
    }
    case (SYN | ACK):
    {
      // 대응되는 socket에 이미 remoteAddr가 적혀있어
      // 그걸로 찾아 socket을
      // 상태 established로 바꿔주고, ack 패킷 보내줘
      int pid, fd = -1;
      for (std::map<int, std::map<int, socket>>::iterator itPid = this->socketMap.begin(); itPid != this->socketMap.end(); itPid++) {
        for (std::map<int, socket>::iterator itFd = itPid->second.begin(); itFd != itPid->second.end(); itFd++) {
          if (
            (itFd->second.state == TCP_SYN_SENT || itFd->second.state == TCP_LISTEN) && 
            itFd->second.remoteAddr.sin_addr.s_addr == ipSrc &&
            itFd->second.remoteAddr.sin_port == portSrc
          ) {
            pid = itPid->first;
            fd = itFd->first;
            break;
          }
        }
      }
      if (pid == -1 || fd == -1) return;

      this->socketMap[pid][fd].state = TCP_ESTABLISHED;

      // seqBuf[3] = seqBuf[3] + 1;
      seq += 1;
      seqN = htonl(seq);
      newHeadLen = 5 << 4;
      newFlags = ACK;
      newWindow = 51200;
      newChecksum = 0;
      newUrgent = 0;
      p.writeData(IP_START+12, &ipDst, 4);
      p.writeData(IP_START+16, &ipSrc, 4);
      p.writeData(TCP_START+0, &portDst, 2);
      p.writeData(TCP_START+2, &portSrc, 2);
      p.writeData(TCP_START+4, &randSeq, 4);
      // p.writeData(TCP_START+8, &seqBuf, 4);
      p.writeData(TCP_START+8, &seqN, 4);
      p.writeData(TCP_START+12, &headLen, 1);
      p.writeData(TCP_START+13, &newFlags, 1);
      p.writeData(TCP_START+14, &newWindow, 2);
      p.writeData(TCP_START+16, &newChecksum, 2);
      p.writeData(TCP_START+18, &newUrgent, 2);

      this->cancelTimer(this->socketMap[pid][fd].timerUUID);
      this->returnSystemCall(this->socketMap[pid][fd].syscallUUID, 0);

      break;
    }
    case ACK:
    {
      // 대응되는 socket에 이미 remoteAddr가 적혀있어
      // 그걸로 찾아 socket을
      int pid, fd = -1;
      for (std::map<int, std::map<int, socket>>::iterator itPid = this->socketMap.begin(); itPid != this->socketMap.end(); itPid++) {
        for (std::map<int, socket>::iterator itFd = itPid->second.begin(); itFd != itPid->second.end(); itFd++) {
          if (
            // itFd->second.state == TCP_LISTEN && 
            itFd->second.remoteAddr.sin_addr.s_addr == ipSrc &&
            itFd->second.remoteAddr.sin_port == portSrc
          ) {
            pid = itPid->first;
            fd = itFd->first;
            break;
          }
        }
      }
      if (pid == -1 || fd == -1) return;

      if (payloadLen == 0) { // handshaking 패킷임
        // 그리고 accpet될 수 있도록 q에 넣어줘
        this->backlogMap[pid].current--;
        this->backlogMap[pid].q.push(fd);

        this->socketMap[pid][fd].state = TCP_ESTABLISHED;

        if (this->socketMap[pid][fd].syscallUUID) {
          this->cancelTimer(this->socketMap[pid][fd].timerUUID);
          this->returnSystemCall(this->socketMap[pid][fd].syscallUUID, 0);
        }
        return;

      } else { // payload를 담고 있는 data 패킷임
        
      }

      return;
    }
    default:
    {
      return;
    }
  }
  
  uint8_t buf[HANDSHAKE_PACKET_SIZE];
  p.readData(0, buf, 54);
  assert(buf[TCP_START + 16] == 0);
  assert(buf[TCP_START + 17] == 0);
  newChecksum = NetworkUtil::tcp_sum(
    *(uint32_t *)&buf[IP_START+12], *(uint32_t *)&buf[IP_START+16],
    &buf[TCP_START], 20
  );
  newChecksum = ~newChecksum;
  uint8_t newChecksum1 = (newChecksum & 0xff00) >> 8;
  uint8_t newChecksum2 = (newChecksum & 0x00ff);
  p.writeData(TCP_START + 16, &newChecksum1, 1);
  p.writeData(TCP_START + 17, &newChecksum2, 1);
  
  this->sendPacket("IPv4", std::move(p));
}

void TCPAssignment::timerCallback(std::any payload) {
  timerPayload* tp = std::any_cast<timerPayload*>(payload);
  switch (tp->from) {
    case ACCEPT:
      this->syscall_accept(tp->syscallUUID, tp->pid, tp->fd, tp->addrPtr, tp->addrLenPtr);
      break;
    case CONNECT:
      this->returnSystemCall(tp->syscallUUID, -1);
      break;
    case CLOSE:
      // printf("timerCallback: CLOSE\n");
      // this->syscall_close(tp->syscallUUID, tp->pid, tp->fd);
      break;
    default:
      break;
  }
}

} // namespace E