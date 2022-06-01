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

namespace E {

RoutingAssignment::RoutingAssignment(Host &host)
    : HostModule("UDP", host), RoutingInfoInterface(host),
      TimerModule("UDP", host) {}

RoutingAssignment::~RoutingAssignment() {}

void RoutingAssignment::initialize() {
  // 나 자신을 table에 metric 0으로 추가
  // source 나, dest 255.255.255.255로 request broadcast
}

void RoutingAssignment::finalize() {}

/**
 * @brief Query cost for a host
 *
 * @param ipv4 querying host's IP address
 * @return cost or -1 for no found host
 */
Size RoutingAssignment::ripQuery(const ipv4_t &ipv4) {
  // Implement below

  // table에서 해당 ipv4를 찾아서 metric을 반환
  return -1;
}

void RoutingAssignment::packetArrived(std::string fromModule, Packet &&packet) {
  // Remove below
  (void)fromModule;
  (void)packet;

  // packet이 request라면
  // request source를 table에 metric 1로 추가
  // table을 request 보낸 애한테 전송

  // packet이 response라면
  // table을 업데이트
}

void RoutingAssignment::timerCallback(std::any payload) {
  // Remove below
  (void)payload;

  // 현재 table을 broadcast
}

} // namespace E
