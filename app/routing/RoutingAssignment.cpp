/*
 * E_RoutingAssignment.cpp
 *
 */

#include <E/E_Common.hpp>
#include <E/Networking/E_Host.hpp>
#include <E/Networking/E_NetworkUtil.hpp>
#include <E/Networking/E_Networking.hpp>
#include <E/Networking/E_Packet.hpp>
#include <cerrno>

#include "RoutingAssignment.hpp"
#include <arpa/inet.h>

namespace E {

uint8_t iph_prot = 17;

void RoutingTable::updateEntry(unsigned int ip, int metric){
  auto entry = table.find(ip);
  if(entry == table.end()){
    table[ip] = metric;
  }else{
    if(table[ip] > metric) table[ip] = metric;
  }
}

int RoutingTable::getMetric(unsigned int ip){
  auto entry = table.find(ip);
  if(entry == table.end()){
    return UNREACHABLE_COST; //unreachable
  }else{
    return table[ip];
  }
}

int RoutingTable::size(){
  return table.size();
}

int RoutingTable::writeToPacket(char* dst, size_t entryCount){
  uint32_t header = 0x00000200;

  memset(dst, 0, IN_PACKET_ENTRY_SIZE * entryCount);
  int count;
  for(auto entry = table.begin(); entry != table.end(); entry++){
    int* tempPtr = (int*)dst;
    tempPtr[0] = header;
    tempPtr[1] = entry->first;
    tempPtr[4] = htonl(entry->second);

    count ++;
    if(count == entryCount) return count; //returns the number of written entries.
    dst += IN_PACKET_ENTRY_SIZE;
  }
  return count;
}

RoutingAssignment::RoutingAssignment(Host &host)
    : HostModule("UDP", host), RoutingInfoInterface(host),
      TimerModule("UDP", host) {}

RoutingAssignment::~RoutingAssignment() {
}

void RoutingAssignment::initialize() {
  uint16_t port = htons(520);
  {
    int portcount = this->getPortCount();
    for(int i = 0; i < portcount; i++){
      std::optional<ipv4_t> ipSrc = this->getIPAddr(i);
      uint32_t localip = (uint32_t) NetworkUtil::arrayToUINT64(*ipSrc);
      routingTable.updateEntry(localip, 0);
    }
  }

  // source 나, dest 255.255.255.255로 request broadcast
  {
    int portcount = this->getPortCount();
    for(int i = 0; i < portcount; i++){
      std::optional<ipv4_t> ipSrc = this->getIPAddr(i);
      uint32_t localip = (uint32_t) NetworkUtil::arrayToUINT64(*ipSrc);
      
      int mypacketsize = RIP_START + 4 + IN_PACKET_ENTRY_SIZE;
      Packet mypacket(mypacketsize);

      uint32_t remoteip = -1;
      //ip 헤더.
      uint16_t n_packetsize = htons(mypacketsize);
      mypacket.writeData(IP_START + 2, &n_packetsize, 2);
      mypacket.writeData(IP_START + 9, &iph_prot, 1);
      mypacket.writeData(IP_START + 12, &localip, 4);
      mypacket.writeData(IP_START + 16, &remoteip, 4);

      //udp 헤더.
      uint16_t udpsize = htons(mypacketsize - UDP_START);
      uint16_t szero = 0;
      mypacket.writeData(UDP_START, &port, 2);
      mypacket.writeData(UDP_START + 2, &port, 2);
      mypacket.writeData(UDP_START + 4, &udpsize, 2);
      mypacket.writeData(UDP_START + 6, &szero, 2);

      uint8_t newcommand = 1;
      uint8_t newversion = 1;
      uint16_t newfamily = 0;
      mypacket.writeData(RIP_START, &newcommand, 1);
      mypacket.writeData(RIP_START + 1, &newversion, 1);
      mypacket.writeData(RIP_START + 2, &szero, 2);
      mypacket.writeData(RIP_START + 4, &newfamily, 2);
      mypacket.writeData(RIP_START + 6, &szero, 2);

      mypacket.writeData(RIP_START + 8, &szero, 2);
      mypacket.writeData(RIP_START + 10, &szero, 2);

      mypacket.writeData(RIP_START + 12, &szero, 2);
      mypacket.writeData(RIP_START + 14, &szero, 2);
      mypacket.writeData(RIP_START + 16, &szero, 2);
      mypacket.writeData(RIP_START + 18, &szero, 2);
      mypacket.writeData(RIP_START + 20, &szero, 2);
      mypacket.writeData(RIP_START + 22, &szero, 2);
      mypacket.writeData(RIP_START + 24, &szero, 2);
      mypacket.writeData(RIP_START + 26, &szero, 2);
      
      mypacket.writeData(RIP_START + 28, &szero, 2);
      mypacket.writeData(RIP_START + 30, &szero, 2);

      this->sendPacket("IPv4", std::move(mypacket));
    }
  }

  this->addTimer(this, 10000000000U);

}

void RoutingAssignment::finalize() {
}
/**
 * @brief Query cost for a host
 *
 * @param ipv4 querying host's IP address
 * @return cost or -1 for no found host
 */
Size RoutingAssignment::ripQuery(const ipv4_t &ipv4) {
  // Implement below
  unsigned int n_ip;
  ((char *)(&n_ip))[0] = ipv4[0];
  ((char *)(&n_ip))[1] = ipv4[1];
  ((char *)(&n_ip))[2] = ipv4[2];
  ((char *)(&n_ip))[3] = ipv4[3];

  std::optional<ipv4_t> ipSrc = this->getIPAddr(0);
  uint32_t localip = (uint32_t) NetworkUtil::arrayToUINT64(*ipSrc);
  return (routingTable.getMetric(n_ip));
  // table에서 해당 ipv4를 찾아서 metric을 반환
}

void RoutingAssignment::packetArrived(std::string fromModule, Packet &&packet) {
  // Remove below
  uint32_t remoteip;
  uint16_t length;
  uint8_t command;
  uint16_t familyid;
  {
    packet.readData(IP_START + 12, &remoteip, 4);

    packet.readData(UDP_START + 4, &length, 2);
    length = ntohs(length);

    packet.readData(RIP_START, &command, 1);

    packet.readData(RIP_START + 4, &familyid, 2);
    familyid = ntohs(familyid);
  }

  uint32_t localip = 0;
  int thislinkportnum = 0;
  {
    int portcount = this->getPortCount();
    for(int i = 0; i < portcount; i++){
      std::optional<ipv4_t> ipSrc = this->getIPAddr(i);
      uint32_t temp_localip = (uint32_t) NetworkUtil::arrayToUINT64(*ipSrc);

      if((temp_localip << 8) >> 8 == (remoteip << 8) >> 8){
        thislinkportnum = i;
        localip = temp_localip;
        break;
      }
    }
  }

  int thislinkcost = this->linkCost(thislinkportnum);
  if(command == 2){ //response
    int remoteTableSize = (length - 8)/IN_PACKET_ENTRY_SIZE; // 8바이트의 UDP 헤더와, 8바이트의 RIP 헤더.
    for(int i = 0; i<remoteTableSize; i++){
      uint32_t ip;
      packet.readData(RIP_START + 8 + i*IN_PACKET_ENTRY_SIZE, &ip, 4);

      uint32_t metric;
      packet.readData(RIP_START + 8 + i*IN_PACKET_ENTRY_SIZE + 12, &metric, 4);
      metric = htonl(metric);

      this->routingTable.updateEntry(ip, metric + thislinkcost); // metric + 1인 이유는, 이 테이블이 한 단계를 건너서 넘어온 것이기 때문.
    }
    return;
  }else if(command == 1){ //request

    int mypacketsize = RIP_START + 4 + IN_PACKET_ENTRY_SIZE * routingTable.size();
    Packet mypacket(mypacketsize);

    //ip 헤더.
    uint16_t n_packetsize = htons(mypacketsize);
    mypacket.writeData(IP_START + 2, &n_packetsize, 2);
    mypacket.writeData(IP_START + 9, &iph_prot, 1);
    mypacket.writeData(IP_START + 12, &localip, 4);
    mypacket.writeData(IP_START + 16, &remoteip, 4);
    
    //udp 헤더.
    uint16_t port = htons(520);
    uint16_t udpsize = htons(mypacketsize - UDP_START);
    uint16_t szero = 0;
    mypacket.writeData(UDP_START, &port, 2);
    mypacket.writeData(UDP_START + 2, &port, 2);
    mypacket.writeData(UDP_START + 4, &udpsize, 2);
    mypacket.writeData(UDP_START + 6, &szero, 2);

    uint8_t newcommand = 2;
    uint8_t newversion = 1;
    uint16_t newfamily = 2;
    newfamily = htons(newfamily);
    mypacket.writeData(RIP_START, &newcommand, 1);
    mypacket.writeData(RIP_START + 1, &newversion, 1);
    mypacket.writeData(RIP_START + 2, &szero, 2);
    mypacket.writeData(RIP_START + 4, &newfamily, 2);
    mypacket.writeData(RIP_START + 6, &szero, 2);

    char* maindata = (char*)malloc(IN_PACKET_ENTRY_SIZE * routingTable.size());
    routingTable.writeToPacket(maindata, routingTable.size());

    mypacket.writeData(RIP_START + 4, maindata, IN_PACKET_ENTRY_SIZE * routingTable.size());
    free(maindata);

    this->sendPacket("IPv4", std::move(mypacket));
  }


  // packet이 request라면
  // request source를 table에 metric 1로 추가
  // table을 request 보낸 애한테 전송
  
  // packet이 response라면
  // table을 업데이트
}

void RoutingAssignment::timerCallback(std::any payload) {
  uint16_t port = htons(520);
  // 현재 table을 broadcast
  {
    int portcount = this->getPortCount();
    for(int i = 0; i < portcount; i++){
      std::optional<ipv4_t> ipSrc = this->getIPAddr(i);
      uint32_t localip = (uint32_t) NetworkUtil::arrayToUINT64(*ipSrc);
      
      int mypacketsize = RIP_START + 4 + IN_PACKET_ENTRY_SIZE * routingTable.size();
      Packet mypacket(mypacketsize);

      uint32_t remoteip = -1;
      //ip 헤더.
      uint16_t n_packetsize = htons(mypacketsize);
      mypacket.writeData(IP_START + 2, &n_packetsize, 2);
      mypacket.writeData(IP_START + 9, &iph_prot, 1);
      mypacket.writeData(IP_START + 12, &localip, 4);
      mypacket.writeData(IP_START + 16, &remoteip, 4);

      //udp 헤더.
      uint16_t udpsize = htons(mypacketsize - UDP_START);
      uint16_t szero = 0;
      mypacket.writeData(UDP_START, &port, 2);
      mypacket.writeData(UDP_START + 2, &port, 2);
      mypacket.writeData(UDP_START + 4, &udpsize, 2);
      mypacket.writeData(UDP_START + 6, &szero, 2);

      uint8_t newcommand = 2;
      uint8_t newversion = 1;
      uint16_t newfamily = 2;
      mypacket.writeData(RIP_START, &newcommand, 1);
      mypacket.writeData(RIP_START + 1, &newversion, 1);
      mypacket.writeData(RIP_START + 2, &szero, 2);
      mypacket.writeData(RIP_START + 4, &newfamily, 2);
      mypacket.writeData(RIP_START + 6, &szero, 2);

      char* maindata = (char*)malloc(IN_PACKET_ENTRY_SIZE * routingTable.size());
      routingTable.writeToPacket(maindata, routingTable.size());

      mypacket.writeData(RIP_START + 4, maindata, IN_PACKET_ENTRY_SIZE * routingTable.size());
      free(maindata);

      this->sendPacket("IPv4", std::move(mypacket));
    }
  }

  this->addTimer(this, 10000000000U);
}

} // namespace E