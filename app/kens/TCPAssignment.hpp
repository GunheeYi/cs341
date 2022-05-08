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
#define PSH 0b1000
#define FIN 1

namespace E {
  //여기서부터 새 코드.
  const int LinkSize = 4096;

class LinkedDataNode
{
public:
	LinkedDataNode* to_head;
	LinkedDataNode* to_tail;
	int usedLength;
	char* linkStorage;

	LinkedDataNode();
	LinkedDataNode(LinkedDataNode* prevNode);
	~LinkedDataNode();
};

extern int MAX_LINK_LENGTH;
class SyncChannel
{
private:
  unsigned int head_link_offset;
	void pushToRecycledLink(LinkedDataNode* oldNode);
	LinkedDataNode* popFromRecycledLink();
public:
	LinkedDataNode* datalink_head;
	LinkedDataNode* datalink_tail;

	LinkedDataNode* recycledLink_head;
	LinkedDataNode* recycledLink_tail;
	int RecycledLinkLen;
	int MaxLinkLen; //max link limit
	int LinkLen; //total count of link.
	int pushData(char* data, int length); //copy data with length. return successfully pushed length. return 0 if link is already full.

	//this returns 'head'
	char* getDataPos(int* length); //dataPos must be pointer of address. length will be overwritted.
	//returns 1 if suggested length was useable. return 0 if suggest length was unavaliable, and new length is overwritted. throw error if cannot use Data.
	//be careful! If you don't use overwritten length, it can leak secure data.

	void Pop(); //delete one link from head. throw if no link.
	void PopAndRecycle(); //pop one link from head and put it after tail.
  int popData(char* Dst, int length);

	void setMaxLinkCount(int newLen);
	void TestPrint();

	SyncChannel();
	~SyncChannel();
};

const int MAX_WINDOW_SIZE = 16; // 4k for each link, 64k in total.

	struct UnseqBlock {
		UnseqBlock* to_head;

		unsigned int headSeq; //맨 첫번째 바이트
		unsigned int tailSeq; //맨 마지막 바이트 + 1

		UnseqBlock* to_tail;
	};

	enum class WindowState{
		EMPTY,
		RECEIVING,
		CONGESTED
	};

	class PacketWindow
	{
	private:
		SyncChannel* targetChannel; //이 윈도우가 연결된 채널.

		UnseqBlock* UB_head;
		UnseqBlock* UB_tail;

		LinkedDataNode* window_head;
		LinkedDataNode* window_tail;
	public:
		unsigned int lastSeq; //현재 window의 가장 머리(버퍼에 가까운 쪽)의 바이트의 SEQ.
    
		WindowState state;
		int ReceivePacket(char* data, unsigned int seq, int length); // if success, -1 if congestion required.

		PacketWindow(SyncChannel* _target, unsigned int initial_seq);
		void InspectWindow();
	};
//여기까지 새 코드

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
  
  //receive buffer
  SyncChannel rcvBuffer;
  PacketWindow* rcvWindow;
  //아래의 두 값은 write 시에 사용.
  uint32_t localseq; //내가 보낼 첫 바이트
  uint32_t remoteseq; //상대가 보낼 첫 바이트

 
  //accept의 반환에 사용.
  // sockaddr* addrPtr;
  // socklen_t* addrLenPtr;
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
  //accept의 반환에 사용.
  sockaddr* addrPtr;
  socklen_t* addrLenPtr;
  socklen_t addrLen; 
  
  //read의 반환에 사용.
  char* read_dst;
  int read_size;
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

class TCPAssignmentProvider {
private:
  TCPAssignmentProvider() {}
  ~TCPAssignmentProvider() {}

public:
  static void allocate(Host &host) { host.addHostModule<TCPAssignment>(host); }
};

} // namespace E

#endif /* E_TCPASSIGNMENT_HPP_ */