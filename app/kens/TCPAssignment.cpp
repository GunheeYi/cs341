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
    // printf("SYSCALL: SOCKET\n");
    this->syscall_socket(
      syscallUUID, pid, 
      std::get<int>(param.params[0]), std::get<int>(param.params[1]), std::get<int>(param.params[2])
    );
    break;
  case BIND:
    // printf("SYSCALL: BIND\n");
    this->syscall_bind(
      syscallUUID, pid, std::get<int>(param.params[0]),
      static_cast<struct sockaddr *>(std::get<void *>(param.params[1])),
      (socklen_t)std::get<int>(param.params[2])
    );
    break;
  case LISTEN:
    // printf("SYSCALL: LISTEN\n");
    this->syscall_listen(
      syscallUUID, pid, 
      std::get<int>(param.params[0]),
      std::get<int>(param.params[1])
    );
    break;
  case ACCEPT:
    // printf("SYSCALL: ACCEPT\n");
    this->syscall_accept(
      syscallUUID, pid, std::get<int>(param.params[0]),
      static_cast<struct sockaddr *>(std::get<void *>(param.params[1])),
      static_cast<socklen_t *>(std::get<void *>(param.params[2]))
    );
    break;
  case CONNECT:
    // printf("SYSCALL: CONNECT\n");
    this->syscall_connect(
      syscallUUID, pid, std::get<int>(param.params[0]), 
      static_cast<struct sockaddr *>(std::get<void *>(param.params[1])),
      (socklen_t)std::get<int>(param.params[2])
    );
    break;
  case READ:
    // printf("SYSCALL: READ\n");
    this->syscall_read(syscallUUID, pid, std::get<int>(param.params[0]),
                       std::get<void *>(param.params[1]),
                       std::get<int>(param.params[2]));
    break;
  case WRITE:
    // printf("SYSCALL: WRITE\n");
    this->syscall_write(syscallUUID, pid, std::get<int>(param.params[0]),
                        std::get<void *>(param.params[1]),
                        std::get<int>(param.params[2]));
    break;
  case CLOSE:
    // printf("SYSCALL: CLOSE\n");
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
  s.readBuf = (char*) malloc(READ_BUFFER_SIZE);
  s.writeBuf = (char*) malloc(WRITE_BUFFER_SIZE);
  s.readStart = 0;
  s.readEnd = 0;
  s.readBufOffsetSet = false;
  s.seq = rand();
  
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
    tp->from = TIMER_FROM_ACCEPT;
    tp->syscallUUID = syscallUUID;
    tp->pid = pid;
    tp->fd = fd;
    tp->accept_addrPtr = addrPtr;
    tp->accept_addrLenPtr = addrLenPtr;
    this->addTimer(tp, 100000000U);
    return;
  }

  int fdToAccept = this->backlogMap[pid].q.front();
  this->backlogMap[pid].q.pop();

  assert(this->socketMap[pid].find(fdToAccept) != this->socketMap[pid].end());
  socket* s = &this->socketMap[pid][fdToAccept];
  
  sockaddr_in* addrPtr_in = (sockaddr_in*) addrPtr;
  memcpy(addrPtr_in, &s->remoteAddr, sizeof(sockaddr_in));
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

  socket* s = &this->socketMap[pid][fd];
  if (!s->binded) {
    s->localAddr.sin_family = AF_INET;
    s->localAddr.sin_addr.s_addr = (uint32_t) NetworkUtil::arrayToUINT64(*ipSrc);
    s->localAddr.sin_port = portSrc;
    s->binded = true;
  }

  s->remoteAddr.sin_family = AF_INET;
  s->remoteAddr.sin_addr.s_addr = addrPtr_in->sin_addr.s_addr;
  s->remoteAddr.sin_port = addrPtr_in->sin_port;
  
  uint32_t seqN = htonl(s->seq);
  uint32_t ackN = htonl(0);
  uint8_t headLen = 5 << 4, flags = SYN;
  uint16_t window = 51200, checksum = 0, urgent = 0, newChecksum;
  
  Packet p(TCP_START + TCP_HEADER_SIZE);

  p.writeData(IP_START+12, &ipSrc, 4);
  p.writeData(IP_START+16, &addrPtr_in->sin_addr.s_addr, 4);
  p.writeData(TCP_START+0, &portSrc, 2);
  p.writeData(TCP_START+2, &addrPtr_in->sin_port, 2);
  p.writeData(TCP_START+4, &seqN, 4);
  p.writeData(TCP_START+8, &ackN, 4);
  p.writeData(TCP_START+12, &headLen, 1);
  p.writeData(TCP_START+13, &flags, 1);
  p.writeData(TCP_START+14, &window, 2);
  p.writeData(TCP_START+16, &checksum, 2);
  p.writeData(TCP_START+18, &urgent, 2);

  uint8_t buf[TCP_START + TCP_HEADER_SIZE];
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
  s->state = TCP_SYN_SENT;

  timerPayload* tp = (timerPayload*) malloc(sizeof(timerPayload));
  tp->from = TIMER_FROM_CONNECT;
  tp->syscallUUID = syscallUUID;
  tp->pid = pid;
  tp->fd = fd;
  tp->connect_addrPtr = addrPtr;
  tp->connect_addrLen = addrLen;
  s->connect_timerUUID = this->addTimer(tp, 100000000U);
  s->connect_syscallUUID = syscallUUID;

};

void TCPAssignment::syscall_read(UUID syscallUUID, int pid, int fd, void* start, uint32_t len) {
  assert(this->socketMap.find(pid) != this->socketMap.end());
  assert(this->socketMap[pid].find(fd) != this->socketMap[pid].end());

  socket* s = &this->socketMap[pid][fd];

  if (s->state != TCP_ESTABLISHED) this->returnSystemCall(syscallUUID, -1);

  if (s->readStart == s->readEnd) { // 읽을 데이터가 없음
    // printf("READ: no data to read in read buffer\n");
    timerPayload* tp = (timerPayload*) malloc(sizeof(timerPayload));
    tp->from = TIMER_FROM_READ;
    tp->syscallUUID = syscallUUID;
    tp->pid = pid;
    tp->fd = fd;
    tp->read_start = start;
    tp->read_len = len;
    this->addTimer(tp, 100000000U);
    return;
  }
  uint32_t available = s->readEnd - s->readStart;
  uint32_t readLen = available < len ? available : len;

  memcpy(start, s->readBuf + s->readStart, readLen);
  s->readStart += readLen;

  this->returnSystemCall(syscallUUID, readLen);

  return;
};

void TCPAssignment::syscall_write(UUID syscallUUID, int pid, int fd, void* start, uint32_t len) {
  assert(this->socketMap.find(pid) != this->socketMap.end());
  assert(this->socketMap[pid].find(fd) != this->socketMap[pid].end());
  socket* s = &this->socketMap[pid][fd];

  uint32_t ipSrc = s->localAddr.sin_addr.s_addr;
  uint32_t ipDst = s->remoteAddr.sin_addr.s_addr;
  uint16_t portSrc = s->localAddr.sin_port;
  uint16_t portDst = s->remoteAddr.sin_port;

  uint32_t newSeqN;
  uint32_t newAckN = htonl(s->ack);
  uint8_t newHeadLen = 5 << 4;
  uint8_t newFlags = ACK;
  uint16_t newWindow = htons(51200);
  uint16_t newChecksum = htons(0);
  uint16_t newUrgent = htons(0);

  uint32_t sent = 0;
  uint32_t remaining = len;
  uint32_t sending, packetSize;
  
	while(remaining != 0){
		sending = remaining > MAX_SEGMENT_SIZE ? MAX_SEGMENT_SIZE : remaining;
    packetSize = TCP_START + TCP_HEADER_SIZE + sending;
    
		Packet p(packetSize);
    newSeqN = s->seq;
		newSeqN = htonl(newSeqN);
    p.writeData(IP_START+12, &ipSrc, 4);
    p.writeData(IP_START+16, &ipDst, 4);
    p.writeData(TCP_START+0, &portSrc, 2);
    p.writeData(TCP_START+2, &portDst, 2);
    p.writeData(TCP_START+4, &newSeqN, 4);
    p.writeData(TCP_START+8, &newAckN, 4);
    p.writeData(TCP_START+12, &newHeadLen, 1);
    p.writeData(TCP_START+13, &newFlags, 1);
    p.writeData(TCP_START+14, &newWindow, 2);
    p.writeData(TCP_START+16, &newChecksum, 2);
    p.writeData(TCP_START+18, &newUrgent, 2);
    p.writeData(TCP_START+TCP_HEADER_SIZE, (char*)start + sent, sending);

		uint8_t buf[packetSize];
    p.readData(0, buf, packetSize);
    assert(buf[TCP_START + 16] == 0);
    assert(buf[TCP_START + 17] == 0);
    newChecksum = NetworkUtil::tcp_sum(
      *(uint32_t *)&buf[IP_START+12], *(uint32_t *)&buf[IP_START+16],
      &buf[TCP_START], TCP_HEADER_SIZE + sending
    );
    newChecksum = ~newChecksum;
    uint8_t newChecksum1 = (newChecksum & 0xff00) >> 8;
    uint8_t newChecksum2 = (newChecksum & 0x00ff);
    p.writeData(TCP_START + 16, &newChecksum1, 1);
    p.writeData(TCP_START + 17, &newChecksum2, 1);
    
    this->sendPacket("IPv4", std::move(p));

    s->seq += sending;
    sent += sending;
    remaining -= sending;
	}

  timerPayload* tp = (timerPayload*) malloc(sizeof(timerPayload));
  tp->from = TIMER_FROM_WRITE;
  tp->syscallUUID = syscallUUID;
  tp->pid = pid;
  tp->fd = fd;
  tp->write_start = start;
  tp->write_len = len;
  s->write_timerUUID = this->addTimer(tp, 100000000U);
  s->write_syscallUUID = syscallUUID;
  s->writeSent = sent;

	// this->returnSystemCall(syscallUUID, sent);
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
  // printf("PACKET ARRIVED.\n");

  socket* newSocket; // Used exclusively for SYN flagged packets.

  uint16_t tcpSegLen;
  uint32_t ipSrc, ipDst;
  uint16_t portSrc, portDst;
  uint8_t seqBuf[4], ackBuf[4];
  uint32_t seq, ack;
  uint8_t headLen, flags;
  uint16_t window, checksum, urgent;
  size_t payloadLen;

  uint32_t newSeq, newAck, newSeqN, newAckN;
  uint8_t newHeadLen, newFlags;
  uint16_t newWindow, newChecksum, newUrgent;

  packet.readData(IP_START+2, &tcpSegLen, 2);
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

  tcpSegLen = ntohs(tcpSegLen);
  uint8_t tempbuf[TCP_START + tcpSegLen];
  packet.readData(0, tempbuf, TCP_START + tcpSegLen);
  tempbuf[TCP_START + 16] = 0;
  tempbuf[TCP_START + 17] = 0;

  uint16_t calculatedChecksum;
  calculatedChecksum = NetworkUtil::tcp_sum(
    *(uint32_t *)&tempbuf[IP_START+12], *(uint32_t *)&tempbuf[IP_START+16],
    &tempbuf[TCP_START], tcpSegLen - 20
  );
  calculatedChecksum = ~calculatedChecksum;
  if(calculatedChecksum != ntohs(checksum)){
    // printf("Checksum error. !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    return;
  }

  seq = ntohl( *(uint32_t *)seqBuf );
  ack = ntohl( *(uint32_t *)ackBuf );

  headLen = (headLen & 0xf0) >> 2;

  payloadLen = packet.getSize() - (TCP_START + headLen);

  Packet p(TCP_START + TCP_HEADER_SIZE);

  switch(flags) {
    case SYN:
    { 
      int pid = -1, fd = -1;

      for (std::map<int, std::map<int, socket>>::iterator itPid = this->socketMap.begin(); itPid != this->socketMap.end(); itPid++) {
        for (std::map<int, socket>::iterator itFd = itPid->second.begin(); itFd != itPid->second.end(); itFd++) {
          if (
            (itFd->second.state == TCP_SYN_RCVD) &&
            itFd->second.localAddr.sin_addr.s_addr == ipDst &&
            itFd->second.localAddr.sin_port == portDst &&
            itFd->second.remoteAddr.sin_addr.s_addr == ipSrc &&
            itFd->second.remoteAddr.sin_port == portSrc
          ) {
            pid = itPid->first;
            fd = itFd->first;
            // printf("Found SYN-RCVD socket.\n");
            break;
          }
        }
      }

      if (pid != -1 && fd != -1) {
        newSeq = this->socketMap[pid][fd].seq;
        newAck = seq + 1;
        newFlags = SYN | ACK;
        break;
      }

      // packet에 dstIp, dstPort로 listening socket을 찾아
      // 새로운 socket 생성, 거기에 listening socket의 localAddr를 복사
      // packet의 srcIp, srcPort를 새로운 socket의 remoteAddr로 복사
      
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
            fd = itFd->first;
            break;
          }
        }
      }

      if (
        pid == -1 || 
        fd == -1 || 
        this->backlogMap[pid].current >= this->backlogMap[pid].capacity
      ) return;

      this->backlogMap[pid].current++;

      int newfd = this->createFileDescriptor(pid);
      newSocket = &this->socketMap[pid][newfd];
      memcpy(&newSocket->localAddr, &this->socketMap[pid][fd].localAddr, sizeof(sockaddr_in));
      newSocket->localAddr.sin_addr.s_addr = ipDst;
      newSocket->remoteAddr.sin_family = AF_INET;
      newSocket->remoteAddr.sin_addr.s_addr = ipSrc;
      newSocket->remoteAddr.sin_port = portSrc;
      newSocket->state = TCP_SYN_RCVD;

      newSocket->readBuf = (char*) malloc(READ_BUFFER_SIZE);
      newSocket->writeBuf = (char*) malloc(WRITE_BUFFER_SIZE);
      newSocket->readStart = 0;
      newSocket->readEnd = 0;
      newSocket->readBufOffsetSet = false;
      newSocket->seq = rand();

      newSeq = newSocket->seq;
      newAck = seq + 1;
      newFlags = SYN | ACK;

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

      socket* s = &this->socketMap[pid][fd];
      s->state = TCP_ESTABLISHED;

      s->seq += 1;
      newSeq = s->seq;

      s->ack = seq + 1;
      newAck = s->ack;

      newFlags = ACK;

      this->cancelTimer(s->connect_timerUUID);
      this->returnSystemCall(s->connect_syscallUUID, 0);

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

      socket* s = &this->socketMap[pid][fd];

      if (payloadLen == 0) {
        if (s->state != TCP_ESTABLISHED) { // handshaking 패킷임
          // printf("PACKET ARRIVED: ACK packet for last handshaking step.\n");
          // accpet될 수 있도록 q에 넣어줘
          this->backlogMap[pid].current--;
          this->backlogMap[pid].q.push(fd);

          s->state = TCP_ESTABLISHED;
          s->seq += 1;

          this->cancelTimer(s->handshake_timerUUID);

          // simulatneous connect handling
          // TODO: socket에 syscallUUID랑 timerUUID를 저장해놓는게 맞아?
          if (s->connect_syscallUUID) {
            this->cancelTimer(s->connect_timerUUID);
            this->returnSystemCall(s->connect_syscallUUID, 0);
          }
          
          return;
        }

        // data 패킷에 대한 ack 응답임
        // printf("PACKET ARRIVED: ACK response to data packet. Returning write system call with return value %u.\n", s->writeSent);
        this->cancelTimer(s->write_timerUUID);
        this->returnSystemCall(s->write_syscallUUID, s->writeSent);
        return;

      } else { // payload를 담고 있는 data 패킷임
        // printf("PACKET ARRIVED: Data packet with payload.\n");
        assert(s->state == TCP_ESTABLISHED);
        if (!s->readBufOffsetSet) {
          s->readBufOffset = seq;
          s->readBufOffsetSet = true;
        }

        // relative sequence number
        size_t seqRel = seq - s->readBufOffset;
        size_t seqRel_ = seqRel % READ_BUFFER_SIZE; // _가 붙은 변수는 mod READ_BUFFER_SIZE 연산이 되었음을 의미

        // read buffer overflow
        if (
          seqRel < s->readStart ||
          seqRel + payloadLen > s->readStart + READ_BUFFER_SIZE) {
          // printf("PACKET ARRIVED: Read buffer overflow. Rejecting incoming packet.\n");
          // TODO: 이전 유효 ack number 다시 보내줘야 하나?
          return;
        }

        size_t PAYLOAD_START = TCP_START + headLen;

        // payload를 read buffer에 쓰기
        if (seqRel_ + payloadLen > READ_BUFFER_SIZE) { // write should be wrapped
          size_t len1 = READ_BUFFER_SIZE - seqRel_;
          size_t len2 = payloadLen - len1;
          packet.readData(PAYLOAD_START, s->readBuf + seqRel_, len1);
          packet.readData(PAYLOAD_START + len1, s->readBuf, len2);
        } else { // write is simple
          packet.readData(PAYLOAD_START, s->readBuf + seqRel_, payloadLen);
        }

        readBufMarker marker;
        marker.start = seqRel;
        marker.end = seqRel + payloadLen;

        // 일단 벡터에 새 마커를 순서 맞게 넣어
        // 그리고 readEnd 확장할 수 있는데까지 확장하고 처리된 마커들 vector에서 pop해

        // 일단 벡터에 새 마커를 순서 맞게 넣어
        std::list<readBufMarker>::iterator itMarker;
        for (itMarker = s->readBufMarkers.begin(); itMarker != s->readBufMarkers.end(); itMarker++) {
          if (
            itMarker->start <= marker.start && 
            (
              std::next(itMarker) == s->readBufMarkers.end() ||
              std::next(itMarker)->start >= marker.start
            )
          ) break;
        }
        s->readBufMarkers.insert(std::next(itMarker), marker);

        // 그리고 readEnd 확장할 수 있는데까지 확장하고 처리된 마커들 vector에서 pop해
        for (itMarker = s->readBufMarkers.begin(); itMarker != s->readBufMarkers.end(); ) {
          std::list<readBufMarker>::iterator itMarkerNext = std::next(itMarker);
          if (itMarker->start <= s->readEnd) {
            s->readEnd = itMarker->end;
            s->readBufMarkers.erase(itMarker);
          } else break;
          itMarker = itMarkerNext;
        }

        newSeq = 1;
        newAck = s->readEnd + s->readBufOffset;
        newFlags = ACK;

        break;
      }
    }
    default:
    {
      return;
    }
  }

  newHeadLen = 5 << 4;
  newWindow = 51200;
  newChecksum = 0;
  newUrgent = 0;

  newSeqN = htonl(newSeq);
  newAckN = htonl(newAck);
  p.writeData(IP_START+12, &ipDst, 4);
  p.writeData(IP_START+16, &ipSrc, 4);
  p.writeData(TCP_START+0, &portDst, 2);
  p.writeData(TCP_START+2, &portSrc, 2);
  p.writeData(TCP_START+4, &newSeqN, 4);
  p.writeData(TCP_START+8, &newAckN, 4);
  p.writeData(TCP_START+12, &newHeadLen, 1);
  p.writeData(TCP_START+13, &newFlags, 1);
  p.writeData(TCP_START+14, &newWindow, 2);
  p.writeData(TCP_START+16, &newChecksum, 2);
  p.writeData(TCP_START+18, &newUrgent, 2);
  
  uint8_t buf[TCP_START + TCP_HEADER_SIZE];
  p.readData(0, buf, 54);
  assert(buf[TCP_START + 16] == 0);
  assert(buf[TCP_START + 17] == 0);
  newChecksum = NetworkUtil::tcp_sum(
    *(uint32_t *)&buf[IP_START+12], *(uint32_t *)&buf[IP_START+16],
    &buf[TCP_START], TCP_HEADER_SIZE
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
    case TIMER_FROM_ACCEPT:
      this->syscall_accept(tp->syscallUUID, tp->pid, tp->fd, tp->accept_addrPtr, tp->accept_addrLenPtr);
      break;
    case TIMER_FROM_CONNECT:
      this->syscall_connect(tp->syscallUUID, tp->pid, tp->fd, tp->connect_addrPtr, tp->connect_addrLen);
      // this->returnSystemCall(tp->syscallUUID, -1);
      break;
    case TIMER_FROM_READ:
      this->syscall_read(tp->syscallUUID, tp->pid, tp->fd, tp->read_start, tp->read_len);
      break;
    case TIMER_FROM_WRITE:
      this->syscall_write(tp->syscallUUID, tp->pid, tp->fd, tp->write_start, tp->write_len);
      break;
    case TIMER_FROM_CLOSE:
      // printf("timerCallback: CLOSE\n");
      // this->syscall_close(tp->syscallUUID, tp->pid, tp->fd);
      break;
    default:
      break;
  }
}

} // namespace E