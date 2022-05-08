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

//여기서부터 과제 3 번 코드
LinkedDataNode::LinkedDataNode() {
	to_head = nullptr;
	to_tail = nullptr;
	usedLength = 0;
	linkStorage = (char*)malloc(LinkSize);
	if (linkStorage == NULL) {
		throw std::runtime_error("malloc failed");
	}
}

LinkedDataNode::LinkedDataNode(LinkedDataNode* prevNode) {
	this->to_head = prevNode;
	this->to_tail = nullptr;
	usedLength = 0;
	linkStorage = (char*)malloc(LinkSize);
	if (linkStorage == NULL) {
		throw std::runtime_error("malloc failed");
	}
	prevNode->to_tail = this;
}

LinkedDataNode::~LinkedDataNode() { //remove only this node.
	if (to_head != nullptr) {
		to_head->to_tail = to_tail;
	}

	if (to_tail != nullptr) {
		to_tail->to_head = to_head;
	}
	free(linkStorage);
}

int MAX_LINK_LENGTH = 250000;

SyncChannel::SyncChannel() {
  head_link_offset = 0;
	datalink_head = nullptr;
	datalink_tail = nullptr;
	recycledLink_head = nullptr;
	recycledLink_tail = nullptr;
	LinkLen = 0;
	RecycledLinkLen = 0;
	MaxLinkLen = MAX_LINK_LENGTH; //100MB for default.
}

SyncChannel::~SyncChannel() {
	LinkedDataNode* currentLink = datalink_head;
	while (currentLink != nullptr) {
		LinkedDataNode* oldNode = currentLink;
		currentLink = currentLink->to_tail;

		oldNode->~LinkedDataNode();
	}

	currentLink = recycledLink_head;
	while (currentLink != nullptr) {
		LinkedDataNode* oldNode = currentLink;
		currentLink = currentLink->to_tail;

		oldNode->~LinkedDataNode();
	}
}

LinkedDataNode* SyncChannel::popFromRecycledLink() {
	LinkedDataNode* returnNode = recycledLink_head;

	if (RecycledLinkLen == 1) {
		recycledLink_head = nullptr;
		recycledLink_tail = nullptr;
		RecycledLinkLen = 0;
	}
	else {
		recycledLink_head = recycledLink_head->to_tail;
		recycledLink_head->to_head = nullptr;
		RecycledLinkLen--;
	}
	returnNode->to_tail = nullptr;
	returnNode->to_head = nullptr;
	return returnNode;
}

int SyncChannel::pushData(char* data, int length) {
	//최초의 채널 상태 복사.
	LinkedDataNode* temporalHead = datalink_head;
	LinkedDataNode* temporalTail = datalink_tail;
	int temporalLinkLen = LinkLen;

	char* dataPos = data;

	if (temporalTail == nullptr) {
		//if there is recycled link.
		if (RecycledLinkLen != 0) {
			temporalHead = popFromRecycledLink();
		}
		else {
			temporalHead = new LinkedDataNode();
		}
		temporalTail = temporalHead;
		temporalLinkLen = 1;
	}
	//now, at least one link is present.

	char* writablePos;
	int writableLen;
	int LeftoverLength = length;

	while (true) {
		//when tail link is full.
		if (temporalTail->usedLength == LinkSize) {
			//can't add new link.
			if (temporalLinkLen == MaxLinkLen) {
				break;
			}

			//append link to tail
			if (RecycledLinkLen != 0) {
				temporalTail->to_tail = popFromRecycledLink();
				temporalTail->to_tail->to_head = temporalTail;
			}
			else {
				temporalTail->to_tail = new LinkedDataNode(temporalTail);
			}
			temporalTail = temporalTail->to_tail;
			temporalLinkLen++;
		}

		writablePos = &(temporalTail->linkStorage[temporalTail->usedLength]);
		writableLen = LinkSize - temporalTail->usedLength;

		if (LeftoverLength < writableLen) {
			memcpy(writablePos, dataPos, LeftoverLength);
			temporalTail->usedLength += LeftoverLength;
			LeftoverLength = 0;
			break;
		}
		else {
			//writable space is smaller than data to write.
			memcpy(writablePos, dataPos, writableLen);
			temporalTail->usedLength = LinkSize;
			dataPos += writableLen;
			LeftoverLength -= writableLen;
		}
	}


	datalink_tail = temporalTail;
	LinkLen = temporalLinkLen;
	datalink_head = temporalHead;
	return length - LeftoverLength;
}

char* SyncChannel::getDataPos(int* length) { //write readable length to argument.
	if (datalink_head == nullptr) {
		*length = 0;
		return nullptr;
	}
	*length = datalink_head->usedLength;

	return datalink_head->linkStorage;
}

void SyncChannel::Pop() {
	if (datalink_head == nullptr) {
		throw std::out_of_range("no head link to pop");
	}

	//if only one link exist.
	if (datalink_head->to_tail == nullptr) {
		datalink_head->~LinkedDataNode();
		datalink_head = nullptr;
		datalink_tail = nullptr;
		LinkLen = 0;
		return;
	}

	datalink_head = datalink_head->to_tail;
	datalink_head->to_head->~LinkedDataNode();
	LinkLen--;
}

void SyncChannel::pushToRecycledLink(LinkedDataNode* oldNode) {
	oldNode->usedLength = 0;
	oldNode->to_tail = nullptr;

	if (recycledLink_tail == nullptr) {
		recycledLink_head = oldNode;
		recycledLink_tail = oldNode;
		oldNode->to_head = nullptr;
		return;
	}

	recycledLink_tail->to_tail = oldNode;
	oldNode->to_head = recycledLink_tail;
	recycledLink_tail = oldNode;
}

void SyncChannel::PopAndRecycle() {
	if (datalink_head == nullptr) {
		throw std::out_of_range("no head link to pop");
	}

	//if only one link exist.
	if (datalink_head->to_tail == nullptr) {
		pushToRecycledLink(datalink_head);
		
		datalink_head = datalink_head->to_tail;
		datalink_tail = nullptr;
		LinkLen = 0;
		RecycledLinkLen++;
		return;
	}

	LinkedDataNode* oldLink = datalink_head;

	datalink_head = datalink_head->to_tail;
	datalink_head->to_head = nullptr;
	LinkLen--;

	pushToRecycledLink(oldLink);
	RecycledLinkLen++;
}

int SyncChannel::popData(char* Dst, int length){
	unsigned int* offset = &head_link_offset;
	if (*offset == LinkSize) {
		if (this->LinkLen == 1) {
			return 0;
		}
		else {
			this->PopAndRecycle();
			*offset = 0;
		}
	}

	unsigned int DstOffset = 0;

	int leftLength = length;

	int tempLength;
	while (leftLength != 0) {
		char* dataHeadPos = this->getDataPos(&tempLength);
		if (dataHeadPos == nullptr) break;
		
		int usableLength = tempLength - *offset;
		if (usableLength <= 0) break;

		if (leftLength < usableLength) {
			memcpy(Dst + DstOffset, dataHeadPos + *offset, leftLength);
			*offset += leftLength;
			leftLength = 0;
			break;
		}
		else if(leftLength >= usableLength) {
			memcpy(Dst + DstOffset, dataHeadPos + *offset, usableLength);

			*offset += usableLength; //일단 앞으로 뭘 하던지 간에 offset은 pop 한 길이 만큼 연장되어야 함.
			DstOffset += usableLength;
			leftLength -= usableLength; //남은 길이도 일단 쓴 만큼 감소.

			if (*offset == LinkSize) { //해당 링크의 끝까지 다 pop 한 경우.
				if (this->LinkLen == 1) { //이 링크는 다 썼지만, 링크가 하나라서 재활용하면 thread unsafe 할 수 있음. 따라서 보류
					//offset은 LinkSize와 같을 것임. 
					break;
				}
				else { //링크가 길어서 재활용 해야함.
					this->PopAndRecycle();
					*offset = 0;
				}
			}
			else {// 해당 링크가 끝까지 차 있지 않았던 경우
				break;
			}
		}
	}

	return length - leftLength;
}


void SyncChannel::setMaxLinkCount(int newLen) {
	if (newLen < LinkLen) {
		throw std::out_of_range("can't shrink max link len under currently occupied link length");
	}

	this->MaxLinkLen = newLen;
}

void SyncChannel::TestPrint() {
	std::cout << "-------------SyncObject Scanning------------\n";

	LinkedDataNode* currentLink = datalink_head;
	std::cout << "DataLinkLength: " << LinkLen << std::endl;
	while (currentLink != nullptr) {
		std::cout << "DataLink: " << currentLink->to_head << " -> " << currentLink << " -> " << currentLink->to_tail << std::endl;

		std::cout << "---->| ";
		for (int i = 0; i < currentLink->usedLength; i++) {
			std::cout << currentLink->linkStorage[i];
		}
		std::cout << std::endl;

		currentLink = currentLink->to_tail;
	}

	std::cout << "RecycledLinkLength: " << RecycledLinkLen << std::endl;
	currentLink = recycledLink_head;
	while (currentLink != nullptr) {
		std::cout << "RecycledLink: " << currentLink->to_head << " -> " << currentLink << " -> " << currentLink->to_tail << std::endl;

		currentLink = currentLink->to_tail;
	}
}

void WriteToWindow(LinkedDataNode* _window_head, unsigned int _winhead_seq, unsigned int packet_seq, char* data, int packet_length) {
	LinkedDataNode* currentNode = _window_head;
	int nodeOffsetCount = 0;
	while (currentNode != nullptr) {
		if ((int)(packet_seq - (_winhead_seq + LinkSize * nodeOffsetCount)) >= 0
			&& (int)((_winhead_seq + LinkSize * (nodeOffsetCount + 1)) - packet_seq) > 0) {

			if ((int)((packet_seq + packet_length) - (_winhead_seq + LinkSize * (nodeOffsetCount + 1))) > 0) {
        printf("   writeToWindow: 두 링크에 걸쳐 저장해야 하는 경우.\n");
				//두 링크에 걸쳐 저장해야 하는 경우.
				int packet_a_part = _winhead_seq + LinkSize * (nodeOffsetCount + 1) - (packet_seq + packet_length);
				memcpy(currentNode->linkStorage + (packet_seq - (_winhead_seq + LinkSize * nodeOffsetCount)), 
					data, packet_a_part);
				memcpy(currentNode->to_tail->linkStorage, data + packet_a_part, packet_length - packet_a_part);
			}
			else {
        printf("   writeToWindow: 앞 한 개의 링크를 안 넘는 경우.\n");
				//앞 한 개의 링크를 안 넘는 경우.
				memcpy(currentNode->linkStorage + (packet_seq - (_winhead_seq + LinkSize * nodeOffsetCount) ), 
					data, packet_length);
			}
			return;
		}

		currentNode = currentNode->to_tail;
		nodeOffsetCount++;
	}
	throw std::runtime_error("window manage failed");
}

PacketWindow::PacketWindow(SyncChannel* _target, unsigned int initial_seq) {
	targetChannel = _target;
	state = WindowState::EMPTY;
	lastSeq = initial_seq;

	UB_head = nullptr;
	UB_tail = nullptr;

	window_head = nullptr;
	window_tail = nullptr;
}

int PacketWindow::ReceivePacket(char* data, unsigned int seq, int length) {
  printf("\n");
	// std::cout << "before receiving packet\n";
	// if(targetChannel != nullptr){
	// 	this->targetChannel->TestPrint();
	// }else{
	// 	printf("target channel null\n");
	// }
	if (state == WindowState::EMPTY) {
		state = WindowState::RECEIVING;
		if (targetChannel->datalink_head == nullptr) {
			targetChannel->datalink_head = new LinkedDataNode();
			targetChannel->datalink_tail = targetChannel->datalink_head;
			targetChannel->LinkLen = 1;
		}
	} 
	else if (state == WindowState::CONGESTED) {
		printf("------ congested ------\n");
		return -1;
	}

	LinkedDataNode* targetLink = targetChannel->datalink_tail; 
	//현재 버퍼의 최종 링크.
	int emptyLength = LinkSize - targetLink->usedLength;
	//현재 윈도우의 targetChannel의 마지막 데이터링크의 여백. 
	//혹시, 이미 buffer로 넘어간 영역의 패킷이 뒤늦게 전송된 경우. drop 함.
	if ((int)(lastSeq - (seq + length)) >= 0) {
    printf("late dup packet\n");
    return 0;
  }

	//먼저, Unseq 블록 검색.
	UnseqBlock* prevUB = nullptr;
	UnseqBlock* currentUB = UB_head;

	int windowLinkCount = 0;
	while (currentUB != nullptr) {
		if ((int)(currentUB->headSeq - seq) > 0) {
			break;
		}
		else if (currentUB->headSeq == seq) {
			// mallang TEST
			printf("================= dup packet\n");
			// mallang TEST
			return 0; //dup packet.
		}
		prevUB = currentUB;
		currentUB = currentUB->to_tail;
	}

  LinkedDataNode* currentWindowLink = window_head;
  while(currentWindowLink != nullptr){
		windowLinkCount++;
    currentWindowLink = currentWindowLink->to_tail;
  }
  printf("window link count: %d\n", windowLinkCount);

	//** 중요 ** window가 가지는 블록도, targetChannel의 최대 링크 노드 갯수 제한을 잠재적으로 초과하지 않도록 합니다.
	//검색 결과 확인.
	if (currentUB == nullptr && prevUB == nullptr) {
		// mallang TEST
		//printf("================= 1\n");
		// mallang TEST
		//window에 unsequenced 블록이 없는 경우.

		if (int(lastSeq - seq) > 0) {
			printf("error num: %d", int(lastSeq - seq));
			throw std::runtime_error("weird packet detected");
		}

		//그 중에서도, loss 없이 바로 뒤에 붙은 경우.
		if (seq == lastSeq) { //지금 받은 패킷이 unsequenced가 아닌 경우.
			// mallang TEST
			//printf("================= 11\n");
			// mallang TEST

			//이 패킷을 받으면 버퍼가 초과되기 때문에, 더 받을 수 없는 경우(종료). 
			if (targetChannel->LinkLen == targetChannel->MaxLinkLen
				&& emptyLength < length) {
				state = WindowState::CONGESTED;
				return -1;
			}


			if (length <= emptyLength) {
				memcpy(targetLink->linkStorage + targetLink->usedLength,
					data, length);

				targetLink->usedLength += length;
				lastSeq += length;
				return 0;
			}

			// mallang TEST
			//printf("================= 112\n");
			// mallang TEST
			//패킷에서 저장할 데이터가 현재 링크의 여백보다 큰 경우( 새 링크 생성 필요 )
			//패킷의 크기는 *절대* 링크 1개를 초과하지 않으므로, 2번 이 작업을 수행할 경우는 X.
			memcpy(targetLink->linkStorage + targetLink->usedLength,
				data, emptyLength);

			LinkedDataNode* newNode;
			if (targetChannel->RecycledLinkLen == 0) { //재활용된 링크가 없음(생성 필요)
				newNode = new LinkedDataNode();
			}
			else { //재활용하여 사용.
				newNode = targetChannel->recycledLink_head;
				targetChannel->RecycledLinkLen--;
				if (targetChannel->RecycledLinkLen == 0) {
					targetChannel->recycledLink_head = nullptr;
					targetChannel->recycledLink_tail = nullptr;
				}
				else {
					targetChannel->recycledLink_head = newNode->to_tail;
					targetChannel->recycledLink_head->to_head = nullptr;
				}
				newNode->to_tail = nullptr;
			}

			//아까 복사한 이후 길이를 이어서 복사.
			memcpy(newNode->linkStorage, data + emptyLength, length - emptyLength);
			newNode->usedLength = length - emptyLength;

			lastSeq += length;

			//이제 pop하는 쪽에서 액세스할 수 있도록 조정.
			targetLink->usedLength = LinkSize;
			newNode->to_head = targetLink;
			targetLink->to_tail = newNode;
			targetChannel->LinkLen++;
			targetChannel->datalink_tail = newNode;
			return 0;
		}
		else {
			// mallang TEST
			printf("================= 12\n");
			// mallang TEST
			//loss 가 발견된 경우
			unsigned int overshoot_distance = seq + length - lastSeq; //버퍼 직후 빈 주소에서부터 지금 unsequenced된 데이터의 가장 마지막 바이트까지의 길이.

			//해당 패킷까지 링크 생성시 버퍼 초과일 경우 -1 반환.
			int required_new_link_N = (overshoot_distance - emptyLength + LinkSize - 1) / LinkSize;
			if (targetChannel->LinkLen + required_new_link_N
				> targetChannel->MaxLinkLen) {
				state = WindowState::CONGESTED;
				return -1;
			}

			//UB 블록을 생성한다.
			UnseqBlock* newUB = new UnseqBlock;
			newUB->headSeq = seq;
			newUB->tailSeq = seq + length;
			newUB->to_head = nullptr;
			newUB->to_tail = nullptr;

			UB_head = newUB;
			UB_tail = newUB;

			//해당 블럭의 데이터가 저장될 링크를 생성한다.
			for (int i = 0; i < required_new_link_N; i++) {

				LinkedDataNode* newNode;
				if (targetChannel->RecycledLinkLen == 0) { //재활용된 링크가 없음(생성 필요)
					newNode = new LinkedDataNode();
				}
				else { //재활용하여 사용.
					newNode = targetChannel->recycledLink_head;
					targetChannel->RecycledLinkLen--;
					if (targetChannel->RecycledLinkLen == 0) {
						targetChannel->recycledLink_head = nullptr;
						targetChannel->recycledLink_tail = nullptr;
					}
					else {
						targetChannel->recycledLink_head = newNode->to_tail;
						targetChannel->recycledLink_head->to_head = nullptr;
					}
					newNode->to_tail = nullptr;
				}

				if (window_head == nullptr) {
					window_head = newNode;
					window_tail = newNode;
				}
				else {
					window_tail->to_tail = newNode;
					newNode->to_head = window_tail;
					window_tail = newNode;
				}
			}

			
			//패킷 데이터 저장.
			if ((int)(seq - (lastSeq + emptyLength)) < 0) {
				// mallang TEST
				printf("================= 121\n");
				// mallang TEST
				//targetLink에 걸쳐 있는 경우.
				int packet_a_part = lastSeq + emptyLength - seq;
				if (packet_a_part < length) { //'완전' window 쪽 링크로 넘치는 경우.
          //printf("완전 window 쪽 link로 넘침\n");
					memcpy(targetLink->linkStorage + targetLink->usedLength,
						data, packet_a_part);
					memcpy(window_head->linkStorage, data, length - packet_a_part);
				}
				else {
          //printf("targetlink 안에서 끝남.\n");
					memcpy(targetLink->linkStorage + targetLink->usedLength,
						data, length);
				}
			}
			else {
				// mallang TEST
				printf("================= 122\n");
				// mallang TEST
				WriteToWindow(window_head, lastSeq + emptyLength, seq, data, length);
			}
			return 0;
		}
	}
	else if(currentUB == nullptr) {
		// mallang TEST
		printf("================= 2\n");
		// mallang TEST
		//현재 패킷이 윈도우의 가장 큰 seq 너머에 있는 경우
		//마지막 UB에 붙어 있을 수도, 아니면 그마저도 떨어져 있을 수도 있음.
		unsigned int overshoot_distance = seq + length - lastSeq;

		int required_new_link_N = (overshoot_distance - emptyLength + LinkSize - 1) / LinkSize;
		if (targetChannel->LinkLen + required_new_link_N
				> targetChannel->MaxLinkLen) {
			state = WindowState::CONGESTED;
			return -1;
		}

		LinkedDataNode* last_window_tail = window_tail;
		//마지막 window link 이후로 링크가 더 필요하면 링크 추가.
		if ((int)(seq + length - (lastSeq + emptyLength + LinkSize * windowLinkCount)) > 0) {
      printf("윈도우 링크가 추가됨\n");

      if(window_tail == nullptr){
			  LinkedDataNode* newNode = new LinkedDataNode();
        window_head = newNode;
			  window_tail = newNode;

        //이 경우는 targetLink에 작성하고 windowLink에 넘칠 수도 있기에, 여기에서 핸들링해 주는 것이 안전하다.
        int packet_t_part = lastSeq + emptyLength - seq;
        if (packet_t_part < length) { // window 쪽 링크로 넘치는 경우.
          printf("window 쪽 link로 넘침\n");
          memcpy(targetLink->linkStorage + targetLink->usedLength,
            data, packet_t_part);

          memcpy(window_head->linkStorage, data, length - packet_t_part);
        }
        else {
          printf("targetlink 안에서 끝남.\n");
          memcpy(targetLink->linkStorage + targetLink->usedLength,
            data, length);
        }
        return 0;
      }else{
        window_tail->to_tail = new LinkedDataNode(window_tail);
        window_tail = window_tail->to_tail;
      }

			//아직 windowLinkCount에는 반영되지 않았음.
		}

		if (prevUB->tailSeq == seq) {
			//마지막 UB에 붙어 있는 경우
			prevUB->tailSeq += length;
		}
		else {
			//마지막 UB 다음에 loss 있는 경우.
			UnseqBlock* newUB = new UnseqBlock;
			newUB->headSeq = seq;
			newUB->tailSeq = seq + length;
			newUB->to_head = UB_tail;
			newUB->to_tail = nullptr;
			UB_tail->to_tail = newUB;
			UB_tail = newUB;
		}

    //윈도우 링크가 존재하여, 윈도우 링크에 작성하는 경우.
    if(window_tail != nullptr){

      printf("윈도우 링크에 작성\n");
		  WriteToWindow(last_window_tail,
			  lastSeq + emptyLength + LinkSize * (windowLinkCount - 1), seq, data, length);
		  return 0;
    }

    //TODO!!: 현재 마지막 윈도우의 가장 큰 seq의 직후/ 혹은 거리를 두고 이후에 붙었으나, 그것이 여전히 targetLink 이내인 경우가 처리되지 않았음.
    int packet_targetLink_part = lastSeq + emptyLength - seq;
    if (packet_targetLink_part < length) { // window 쪽 링크로 넘치는 경우.
      printf("window 쪽 link로 넘침\n");
      memcpy(targetLink->linkStorage + targetLink->usedLength,
        data, packet_targetLink_part);

      //윈도우 쪽 링크를 만들어야 함.
      window_head = new LinkedDataNode();
      window_tail = window_head;
      memcpy(window_head->linkStorage, data, length - packet_targetLink_part);
    }
    else {
      printf("targetlink 안에서 끝남.\n");
      memcpy(targetLink->linkStorage + targetLink->usedLength,
        data, length);
    }
    return 0;
	}
	else {
		// mallang TEST
		printf("================= 3\n");
		// mallang TEST
		// 두 UB 사이에 패킷이 들어가는 경우, 혹은 버퍼 끝과 윈도우 사이에 패킷 들어오는 경우.
		if ((int)(lastSeq - seq) > 0) throw std::runtime_error("weird packet detected");

		//버퍼 뒤에 바로 붙은 경우.
		if (seq == lastSeq) {
			// mallang TEST
			printf("================= 31\n");
			// mallang TEST
			if (length <= emptyLength) {
				// mallang TEST
				printf("================= 32\n");
				// mallang TEST
				//먼저, 새 패킷이 targetLink를 넘지 않는 경우. 바로 데이터 집어넣기.
				memcpy(targetLink->linkStorage + targetLink->usedLength,
					data, length);

				targetLink->usedLength += length;
				lastSeq += length;
			}
			else {
				// mallang TEST
				printf("================= 33\n");
				// mallang TEST
				//새 패킷이 targetLink를 넘는 경우
				memcpy(targetLink->linkStorage + targetLink->usedLength,
					data, emptyLength);
				targetLink->usedLength = LinkSize;

				//아까 복사한 이후 길이를 이어서 복사.
				memcpy(window_head->linkStorage, data + emptyLength, length - emptyLength);

				//이제, window_head 를 targetLink로 만들어야 함.
				window_head->usedLength = length - emptyLength;
				window_head->to_tail = nullptr;
				window_head->to_head = targetLink;
				targetLink->to_tail = window_head;
				targetChannel->LinkLen++;
				targetChannel->datalink_tail = window_head;

				if (window_head->to_tail != nullptr) {
					window_head->to_tail->to_head = nullptr;
				}
				else {
					window_tail = nullptr;
				}

				targetLink = window_head;
				window_head = window_head->to_tail;
				lastSeq += length;
			}

			//여기서, 이 패킷의 끝이 바로 UB_head로 이어지는 경우.
			if ((int)(seq + length - UB_head->headSeq) >= 0) {
				// mallang TEST
				printf("================= 34\n");
				// mallang TEST
				int thisUBlength = UB_head->tailSeq - UB_head->headSeq;
				if (thisUBlength <= LinkSize - targetLink->usedLength) {
					//이 블럭이 tagetLink를 넘지 않는 경우.
					targetLink->usedLength += thisUBlength;
					lastSeq += thisUBlength;

					UnseqBlock* oldUB = UB_head;
					UB_head = oldUB->to_tail;
					if (UB_head == nullptr) {
						UB_tail = nullptr;
					}
					delete oldUB;
					return 0;
				}
				else {
					//이 블럭이 window Link에 걸쳐 있는 경우(Link 편입 필요)
					int targetSide_window_length = LinkSize - targetLink->usedLength;
					int pure_windowSide_length = thisUBlength - targetSide_window_length;

					int mergingWindowCount = (pure_windowSide_length + LinkSize - 1) / LinkSize;

					LinkedDataNode* movinghead = window_head;
					LinkedDataNode* movingtail = window_head;

					for (int i = 1; i < mergingWindowCount; i++) {
						movingtail->usedLength = LinkSize;
						movingtail = movingtail->to_tail;
					}

					//이거 movingtail null이었음.
					movingtail->usedLength = pure_windowSide_length - LinkSize * (mergingWindowCount - 1);

					if (movingtail->to_tail != nullptr) {
						movingtail->to_tail->to_head = nullptr;
					}
					else {
						window_tail = nullptr;
					}

					window_head = movingtail->to_tail;

					targetLink->usedLength = LinkSize;
					movingtail->to_tail = nullptr;
					movinghead->to_head = targetLink;
					targetLink->to_tail = movinghead;
					targetChannel->datalink_tail = movinghead;
					targetChannel->LinkLen += mergingWindowCount; //TODO

					UnseqBlock* oldUB = UB_head;
					UB_head = oldUB->to_tail;
					if (UB_head == nullptr) {
						UB_tail = nullptr;
					}
					delete oldUB;
					return 0;
				}
			}
			return 0;

			//여기까지 재확인 필요.
		}
		else if(prevUB != nullptr) {
			// 버퍼 뒤에 붙은 것이 아니라, UB 사이에 낀 것. 
			//WriteToWindow(window_head, lastSeq + emptyLength, seq, data, length);
		}

		return 0;
	}
}

void PacketWindow::InspectWindow() {
	std::cout << "-------------Window Scanning------------\n";

	LinkedDataNode* currentLink = window_head;
	while (currentLink != nullptr) {
		std::cout << "WindowLink: " << currentLink->to_head << " -> " << currentLink << " -> " << currentLink->to_tail << std::endl;

		std::cout << "---->| ";
		for (int i = 0; i < LinkSize; i++) {
			printf("%c", currentLink->linkStorage[i]);
		}
		std::cout << "|" << std::endl;

		currentLink = currentLink->to_tail;
	}

	UnseqBlock* currentUB = UB_head;
	while (currentUB != nullptr) {
		std::cout << "UB: " << currentUB->to_head << " -> " << currentUB << " -> " << currentUB->to_tail << std::endl;

		std::cout << currentUB->headSeq << "<~>" << currentUB->tailSeq << std::endl;
	
		currentUB = currentUB->to_tail;
	}
}

//여기까지 과제 3 번 코드

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
    printf("syscall: socket\n");
    break;
  case BIND:
    this->syscall_bind(
      syscallUUID, pid, std::get<int>(param.params[0]),
      static_cast<struct sockaddr *>(std::get<void *>(param.params[1])),
      (socklen_t)std::get<int>(param.params[2])
    );
    printf("syscall: bind\n");
    break;
  case LISTEN:
    this->syscall_listen(
      syscallUUID, pid, 
      std::get<int>(param.params[0]),
      std::get<int>(param.params[1])
    );
    printf("syscall: listen\n");
    break;
  case ACCEPT:
    this->syscall_accept(
      syscallUUID, pid, std::get<int>(param.params[0]),
      static_cast<struct sockaddr *>(std::get<void *>(param.params[1])),
      static_cast<socklen_t *>(std::get<void *>(param.params[2]))
    );
    printf("syscall: accept\n");
    break;
  case CONNECT:
    this->syscall_connect(
      syscallUUID, pid, std::get<int>(param.params[0]), 
      static_cast<struct sockaddr *>(std::get<void *>(param.params[1])),
      (socklen_t)std::get<int>(param.params[2])
    );
    printf("syscall: connect\n");
    break;
  case READ:
    printf("syscall: read\n");
    this->syscall_read(syscallUUID, pid, std::get<int>(param.params[0]),
                       (char*)std::get<void *>(param.params[1]),
                       std::get<int>(param.params[2]));
    break;
  case WRITE:
    printf("syscall: write\n");
    this->syscall_write(syscallUUID, pid, std::get<int>(param.params[0]),
                        (char*)std::get<void *>(param.params[1]),
                        std::get<int>(param.params[2]));
    break;
  case CLOSE:
    this->syscall_close(syscallUUID, pid, std::get<int>(param.params[0]));
    printf("syscall: close\n");
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
  s.rcvWindow = nullptr;
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
    this->addTimer(tp, 100000000U);
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
  //assert(this->socketMap.find(pid) != this->socketMap.end());
  //assert(this->socketMap[pid].find(fd) != this->socketMap[pid].end());
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
  uint16_t window = htons(51200), checksum = 0, urgent = 0, newChecksum;
  
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
  tp->connect_addr = addrPtr;
  tp->connect_addrLen = addrLen;
  this->socketMap[pid][fd].timerUUID = this->addTimer(tp, 100000000U);
  this->socketMap[pid][fd].syscallUUID = syscallUUID;

};

void TCPAssignment::syscall_read(UUID syscallUUID, int pid, int fd, char* dst, int size) {
  if(this->socketMap[pid].find(fd) == this->socketMap[pid].end()){
  	this->returnSystemCall(syscallUUID, -1);
  }

  int returnVal = this->socketMap[pid][fd].rcvBuffer.popData(dst, size);
  if(returnVal != 0){
	  printf("read %d bytes\n", returnVal);
  	this->returnSystemCall(syscallUUID, returnVal);
  }else{
	  printf("read hanging\n");
	timerPayload* tp = (timerPayload*) malloc(sizeof(timerPayload));
	tp->from = READ;
	tp->syscallUUID = syscallUUID;
  tp->pid = pid;
  tp->fd = fd;
  tp->read_dst = dst;
  tp->read_size = size;
	this->socketMap[pid][fd].timerUUID = this->addTimer(tp, 100000000U);
	// this->socketMap[pid][fd].syscallUUID = syscallUUID;
	// this->socketMap[pid][fd].read_dst = dst;
	// this->socketMap[pid][fd].read_size = size;
	// this->socketMap[pid][fd].readHanging = true;
  }
};

void TCPAssignment::syscall_write(UUID syscallUUID, int pid, int fd, char* src, int size) {
  uint32_t ipSrc = socketMap[pid][fd].localAddr.sin_addr.s_addr;
	uint16_t portSrc = socketMap[pid][fd].localAddr.sin_port;
	uint32_t ipDst = socketMap[pid][fd].remoteAddr.sin_addr.s_addr;
	uint16_t portDst = socketMap[pid][fd].remoteAddr.sin_port;

	uint16_t leftLength = size;
	int sentLength = 0;
	
	uint8_t HeadLen = 5 << 4;
	uint8_t flag = ACK;
	uint16_t window = htons(51200);
	uint16_t checksum = 0;
	uint16_t urgent = 0;

	while(leftLength != 0){
		uint32_t seq = htonl(socketMap[pid][fd].localseq);
		uint32_t ack = htonl(socketMap[pid][fd].remoteseq);

		uint16_t length;
		if(leftLength > 1480){
			length = 1500;
		}else{
			length = leftLength + 20;
		}

		uint16_t n_length = htons(length);
		Packet response(HANDSHAKE_PACKET_SIZE + length - 20);
		response.writeData(IP_START+2, &n_length, 2);
		response.writeData(IP_START+12, &ipSrc, 4);
		response.writeData(IP_START+16, &ipDst, 4);
		response.writeData(TCP_START+0, &portSrc, 2);
		response.writeData(TCP_START+2, &portDst, 2);
		response.writeData(TCP_START+4, &seq, 4);
		response.writeData(TCP_START+8, &ack, 4);
		response.writeData(TCP_START+12, &HeadLen, 1);
		response.writeData(TCP_START+13, &flag, 1);
		response.writeData(TCP_START+14, &window, 2);
		response.writeData(TCP_START+16, &checksum, 2);
		response.writeData(TCP_START+18, &urgent, 2);
		response.writeData(TCP_START+20, src+ sentLength, length-20);

		uint8_t buf[TCP_START + length];
		response.readData(0, buf, TCP_START + length);
		assert(buf[TCP_START + 16] == 0);
		assert(buf[TCP_START + 17] == 0);

		uint16_t newChecksum;
		newChecksum = NetworkUtil::tcp_sum(
			*(uint32_t *)&buf[IP_START+12], *(uint32_t *)&buf[IP_START+16],
			&buf[TCP_START], length
		);
		newChecksum = ~newChecksum;
		uint8_t newChecksum1 = (newChecksum & 0xff00) >> 8;
		uint8_t newChecksum2 = (newChecksum & 0x00ff);
		response.writeData(TCP_START + 16, &newChecksum1, 1);
		response.writeData(TCP_START + 17, &newChecksum2, 1);

		this->sendPacket("IPv4", std::move(response));
		socketMap[pid][fd].localseq += (length - 20);
		sentLength += length - 20;
		leftLength -= (length - 20);
	}
	this->returnSystemCall(syscallUUID, sentLength);
};

void TCPAssignment::syscall_close(UUID syscallUUID, int pid, int fd) {
  this->socketMap[pid].erase(fd);
  this->removeFileDescriptor(pid, fd);
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
  uint8_t seqBuf[4];
  uint8_t ackBuf[4];
  uint8_t headLen, flags;
  uint16_t window, checksum, urgent;

  int randSeq = rand();
  uint8_t newHeadLen, newFlags;
  uint16_t newWindow, newChecksum, newUrgent;

  //과제 3
  uint16_t packetLength;

  packet.readData(IP_START+2, &packetLength, 2);
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

  uint16_t templength = ntohs(packetLength);
  uint8_t tempbuf[TCP_START + templength];
  packet.readData(0, tempbuf, TCP_START + templength);
  tempbuf[TCP_START + 16] = 0;
  tempbuf[TCP_START + 17] = 0;

  uint16_t calculatedChecksum;
  calculatedChecksum = NetworkUtil::tcp_sum(
    *(uint32_t *)&tempbuf[IP_START+12], *(uint32_t *)&tempbuf[IP_START+16],
    &tempbuf[TCP_START], templength - 20
  );
  calculatedChecksum = ~calculatedChecksum;
  if(calculatedChecksum != ntohs(checksum)){
    return;
  }

  //checksum 확인 완료

  Packet p(HANDSHAKE_PACKET_SIZE);

  switch(flags) {
    case SYN:
    { 
      // packet에 dstIp, dstPort로 listening socket을 찾아
      // 새로운 socket 생성, 거기에 listening socket의 localAddr를 복사
      // packet의 srcIp, srcPort를 listening socket의 remoteAddr로 복사
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

      this->socketMap[pid][newfd].localAddr.sin_family = AF_INET;
      this->socketMap[pid][newfd].localAddr.sin_addr.s_addr = ipDst;
      this->socketMap[pid][newfd].localAddr.sin_port = portDst;

      //seqBuf[3] = seqBuf[3] + 1;
      uint32_t ackRes = htonl( ntohl( *( (uint32_t*)seqBuf ) ) + 1);
      newHeadLen = 5 << 4;
      newFlags = SYN | ACK;
      newWindow = htons(51200);
      newChecksum = 0;
      newUrgent = 0;
      p.writeData(IP_START+12, &ipDst, 4);
      p.writeData(IP_START+16, &ipSrc, 4);
      p.writeData(TCP_START+0, &portDst, 2);
      p.writeData(TCP_START+2, &portSrc, 2);
      p.writeData(TCP_START+4, &randSeq, 4);
      p.writeData(TCP_START+8, &ackRes, 4);
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

      //seqBuf[3] = seqBuf[3] + 1;
      uint32_t ackRes = htonl( ntohl( *( (uint32_t*)seqBuf ) ) + 1);
      uint32_t seqRes = *((uint32_t*)ackBuf);
      newHeadLen = 5 << 4;
      newFlags = ACK;
      newWindow = htons(51200);
      newChecksum = 0;
      newUrgent = 0;
      p.writeData(IP_START+12, &ipDst, 4);
      p.writeData(IP_START+16, &ipSrc, 4);
      p.writeData(TCP_START+0, &portDst, 2);
      p.writeData(TCP_START+2, &portSrc, 2);
      p.writeData(TCP_START+4, &seqRes, 4);
      p.writeData(TCP_START+8, &ackRes, 4);
      p.writeData(TCP_START+12, &headLen, 1);
      p.writeData(TCP_START+13, &newFlags, 1);
      p.writeData(TCP_START+14, &newWindow, 2);
      p.writeData(TCP_START+16, &newChecksum, 2);
      p.writeData(TCP_START+18, &newUrgent, 2);

      //과제 3 번
      this->socketMap[pid][fd].rcvWindow = nullptr;
      this->socketMap[pid][fd].localseq = ntohl(seqRes);
      this->socketMap[pid][fd].remoteseq = ntohl(ackRes);
      //여기까지.

      this->cancelTimer(this->socketMap[pid][fd].timerUUID);
      this->returnSystemCall(this->socketMap[pid][fd].syscallUUID, 0);

      break;
    }
    case ACK:
    {
      // 대응되는 socket에 이미 remoteAddr가 적혀있어
      // 그걸로 찾아 socket을, 그리고 accpet될 수 있도록 q에 넣어줘
      int pid, fd = -1;
      for (std::map<int, std::map<int, socket>>::iterator itPid = this->socketMap.begin(); itPid != this->socketMap.end(); itPid++) {
        for (std::map<int, socket>::iterator itFd = itPid->second.begin(); itFd != itPid->second.end(); itFd++) {
          if(itFd->second.state == TCP_ESTABLISHED && //과제 3
            itFd->second.remoteAddr.sin_addr.s_addr == ipSrc &&
            itFd->second.remoteAddr.sin_port == portSrc){
            pid = itPid->first;
            fd = itFd->first;

            if(htons(packetLength) == 40){ //write한 것의 ack 반환이 도착한 경우.
              return;
            }

            char rcvBuf[1500];
            packet.readData(TCP_START+20, rcvBuf, htons(packetLength) - 40);

            if(this->socketMap[pid][fd].rcvWindow == nullptr){
              printf("window not exist\n");
              this->socketMap[pid][fd].rcvWindow = 
                new PacketWindow(&(this->socketMap[pid][fd].rcvBuffer), ntohl(*((unsigned int*)seqBuf)));
            }
            
            this->socketMap[pid][fd].rcvWindow->ReceivePacket(rcvBuf, (unsigned int)(ntohl(*((int*)seqBuf))), htons(packetLength) - 40);
            printf("window packet received\n");
            //TODO: 여기 이 부분, remoteseq가 packetloss 시에 어떻게 고려되어야 할 지 따로 생각해야 함.
            this->socketMap[pid][fd].remoteseq = (unsigned int)(ntohl(*((int*)seqBuf))) + htons(packetLength) - 40;

            //ack 전송 필요.
            uint32_t ackResponse = htonl(this->socketMap[pid][fd].rcvWindow->lastSeq);
            //std::cout << "last seq: " << this->socketMap[pid][fd].rcvWindow->lastSeq << std::endl;
            newHeadLen = 5 << 4;
            newFlags = ACK;
            newWindow = htons(51299);
            newChecksum = 0;
            newUrgent = 0;

            Packet response(HANDSHAKE_PACKET_SIZE);
            response.writeData(IP_START+12, &ipDst, 4);
            response.writeData(IP_START+16, &ipSrc, 4);
            response.writeData(TCP_START+0, &portDst, 2);
            response.writeData(TCP_START+2, &portSrc, 2);
            response.writeData(TCP_START+4, &ackBuf, 4);
            response.writeData(TCP_START+8, &ackResponse, 4);
            response.writeData(TCP_START+12, &newHeadLen, 1);
            response.writeData(TCP_START+13, &newFlags, 1);
            response.writeData(TCP_START+14, &newWindow, 2);
            response.writeData(TCP_START+16, &newChecksum, 2);
            response.writeData(TCP_START+18, &newUrgent, 2);
            
            uint8_t buf[HANDSHAKE_PACKET_SIZE];
            response.readData(0, buf, 54);
            assert(buf[TCP_START + 16] == 0);
            assert(buf[TCP_START + 17] == 0);
            newChecksum = NetworkUtil::tcp_sum(
              *(uint32_t *)&buf[IP_START+12], *(uint32_t *)&buf[IP_START+16],
              &buf[TCP_START], 20
            );
            newChecksum = ~newChecksum;
            uint8_t newChecksum1 = (newChecksum & 0xff00) >> 8;
            uint8_t newChecksum2 = (newChecksum & 0x00ff);
            response.writeData(TCP_START + 16, &newChecksum1, 1);
            response.writeData(TCP_START + 17, &newChecksum2, 1);

              this->sendPacket("IPv4", std::move(response));
              
            return;
          }
          else if ( //원래 있던 case.
            // itFd->second.state == TCP_LISTEN && 
            itFd->second.remoteAddr.sin_addr.s_addr == ipSrc &&
            itFd->second.remoteAddr.sin_port == portSrc
          ) {
            printf("server received ack\n");
            pid = itPid->first;
            fd = itFd->first;
            break;
          }
        }
      }
      if (pid == -1 || fd == -1) return;

      this->backlogMap[pid].current--;
      this->backlogMap[pid].q.push(fd);

      this->socketMap[pid][fd].state = TCP_ESTABLISHED;

      //과제 3 번
      this->socketMap[pid][fd].rcvWindow = nullptr;
      this->socketMap[pid][fd].localseq = ntohl( *(uint32_t *)ackBuf);
      this->socketMap[pid][fd].remoteseq = ntohl( *(uint32_t *)seqBuf);
      //여기까지.

      if (this->socketMap[pid][fd].syscallUUID) {
        this->cancelTimer(this->socketMap[pid][fd].timerUUID);
        this->returnSystemCall(this->socketMap[pid][fd].syscallUUID, 0);
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
      //this->returnSystemCall(tp->syscallUUID, -1);
      this->syscall_connect(tp->syscallUUID, tp->pid, tp->fd, tp->connect_addr, tp->connect_addrLen);
      break;
    case CLOSE:
      // printf("timerCallback: CLOSE\n");
      // this->syscall_close(tp->syscallUUID, tp->pid, tp->fd);
      break;
    case READ:
      printf("timeout: read retry\n");
      this->syscall_read(tp->syscallUUID, tp->pid, tp->fd, tp->read_dst, tp->read_size);
      break;
    default:
      break;
  }
}

} // namespace E