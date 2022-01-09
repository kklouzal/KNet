#pragma once

namespace KNet
{
	class NetClient : private OVERLAPPED
	{
		NetAddress* _ADDR_RECV;
		std::string _IP_RECV;
		u_short _PORT_RECV;
		NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>* ACKPacketPool = nullptr;
		NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>* SendPacketPool = nullptr;
		//
		LPOVERLAPPED_ENTRY pEntries;
		ULONG pEntriesCount;
		HANDLE IOCP;
		enum class Completions : ULONG_PTR {
			RecvUnread,
			ReleaseACK,
			ReleaseSEND
		};

		//
		//	NetChannels
		Unreliable_Any_Channel* Unreliable_Any;
		Unreliable_Latest_Channel* Unreliable_Latest;
		Reliable_Any_Channel* Reliable_Any;
		Reliable_Latest_Channel* Reliable_Latest;
		Reliable_Ordered_Channel* Reliable_Ordered;
	public:

		//	WARN: may be incorrect port_recv
		//	TODO: get the recv port from the remote client somehow..
		NetClient(std::string IP, u_short PORT)
			: OVERLAPPED(), _IP_RECV(IP), _PORT_RECV(PORT + 1),
			pEntries(new OVERLAPPED_ENTRY[PENDING_SENDS + PENDING_RECVS]), pEntriesCount(0),
			Unreliable_Any(new Unreliable_Any_Channel()),
			Unreliable_Latest(new Unreliable_Latest_Channel()),
			Reliable_Any(new Reliable_Any_Channel()),
			Reliable_Latest(new Reliable_Latest_Channel()),
			Reliable_Ordered(new Reliable_Ordered_Channel())
		{
			//	WARN: can run out of free objects
			//	TODO: find another way to store the address in this object..
			_ADDR_RECV = AddressPool->GetFreeObject();
			_ADDR_RECV->Resolve(_IP_RECV, _PORT_RECV);
			//
			ACKPacketPool = new NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>(GLOBAL_SENDS, 0, this);
			SendPacketPool = new NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>(GLOBAL_SENDS, 1, this);
			//
			//	Create the IOCP handle
			IOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
			if (IOCP == NULL) {
				printf("Create IO Completion Port - Client Error: (%i)\n", GetLastError());
			}
		}

		~NetClient()
		{
			delete Reliable_Ordered;
			delete Reliable_Latest;
			delete Reliable_Any;
			delete Unreliable_Latest;
			delete Unreliable_Any;
			delete[] pEntries;
			CloseHandle(IOCP);
		}

		template <ChannelID CHID> NetPacket_Send* GetFreePacket()
		{
			NetPacket_Send* Packet = SendPacketPool->GetFreeObject();
			if (Packet)
			{
				Packet->AddDestination(_ADDR_RECV);
				Packet->SetPID(PacketID::Data);
				Packet->SetCID(ClientID::Client);
				//
				//	Stamp Channel Information
				//
				//	WARN: Assumes all created packets will be sent and eventually received..
				//	TODO: Stamp packets just before the Send_Thread actually sends it off..
				if (CHID == ChannelID::Unreliable_Any) {
					Unreliable_Any->StampPacket(Packet);
				} else if (CHID == ChannelID::Unreliable_Latest) {
					Unreliable_Latest->StampPacket(Packet);
				} else if (CHID == ChannelID::Reliable_Any) {
					Reliable_Any->StampPacket(Packet);
				} else if (CHID == ChannelID::Reliable_Latest) {
					Reliable_Latest->StampPacket(Packet);
				} else if (CHID == ChannelID::Reliable_Ordered) {
					Reliable_Ordered->StampPacket(Packet);
				}
			}
			return Packet;
		}

		inline void ProcessPacket_Acknowledgement(NetPacket_Recv* Packet)
		{
			long long SentTime;
			Packet->read<long long>(SentTime);
			std::chrono::nanoseconds ns(std::chrono::high_resolution_clock::now().time_since_epoch().count() - SentTime);
			std::chrono::microseconds ms = std::chrono::duration_cast<std::chrono::microseconds>(ns);
			//
			PacketID PID;
			Packet->read<PacketID>(PID);
			if (PID == PacketID::Handshake)
			{
				//printf("\tRecv_Handshake_ACK %fms\n", ms.count() * 0.001f);
			}
			else if (PID == PacketID::Data)
			{
				ChannelID CHID;
				uintmax_t UniqueID;
				Packet->read<ChannelID>(CHID);
				Packet->read<uintmax_t>(UniqueID);
				if (CHID == ChannelID::Reliable_Any) {
					printf("\tRecv_ANY_ACK PID: %i UID: %ju %fms\n", PID, UniqueID, ms.count() * 0.001f);
					NetPacket_Send* AcknowledgedPacket = Reliable_Any->TryACK(UniqueID);
					//
					//	If a packet was acknowledged, return it to the main thread to be placed back in its available packet pool
					if (AcknowledgedPacket) {
						//printf("Return ANY Packet From ACK InternalID %i InternalUniqueID %i\n", AcknowledgedPacket->InternalID, AcknowledgedPacket->InternalUniqueID);
						ReturnPacket(AcknowledgedPacket);
					}
				}
				else if (CHID == ChannelID::Reliable_Latest) {
					printf("\tRecv_LATEST_ACK PID: %i UID: %ju %fms\n", PID, UniqueID, ms.count() * 0.001f);
					NetPacket_Send* AcknowledgedPacket = Reliable_Latest->TryACK(UniqueID);
					//
					//	If a packet was acknowledged, return it to the main thread to be placed back in its available packet pool
					if (AcknowledgedPacket) {
						//printf("Return LATEST Packet From ACK InternalID %i InternalUniqueID %i\n", AcknowledgedPacket->InternalID, AcknowledgedPacket->InternalUniqueID);
						ReturnPacket(AcknowledgedPacket);
					}
				}
				else if (CHID == ChannelID::Reliable_Ordered) {
					printf("\tRecv_ORDERED_ACK PID: %i UID: %ju %fms\n", PID, UniqueID, ms.count() * 0.001f);
					NetPacket_Send* AcknowledgedPacket = Reliable_Ordered->TryACK(UniqueID);
					//
					//	If a packet was acknowledged, return it to the main thread to be placed back in its available packet pool
					if (AcknowledgedPacket) {
						//printf("Return ORDERED Packet From ACK InternalID %i InternalUniqueID %i\n", AcknowledgedPacket->InternalID, AcknowledgedPacket->InternalUniqueID);
						ReturnPacket(AcknowledgedPacket);
					}
				}
			}
		}
		inline NetPacket_Send* ProcessPacket_Handshake(NetPacket_Recv* Packet)
		{
			//
			//	Push the received packet into this client
			//KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, (ULONG_PTR)Completions::RecvUnread, &Packet->Overlap), false);
			//
			//	Formulate an acknowledgement
			NetPacket_Send* ACK = ACKPacketPool->GetFreeObject();
			if (ACK)
			{
				ACK->AddDestination(_ADDR_RECV);
				ACK->SetPID(PacketID::Acknowledgement);
				ACK->SetCID(ClientID::Client);
				ACK->write<long long>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
				ACK->write<PacketID>(PacketID::Handshake);
				//	TODO: add more data..like actual packet ids
			}
			//
			//	Return the acknowledgement to be sent from the calling NetPoint
			return ACK;
		}
		inline NetPacket_Send* ProcessPacket_Data(NetPacket_Recv* Packet)
		{
			ChannelID CHID;
			Packet->read<ChannelID>(CHID);

			if (CHID == ChannelID::Unreliable_Any)
			{
				//
				//	Push the received packet into this client
				KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, (ULONG_PTR)Completions::RecvUnread, &Packet->Overlap), false);
				return nullptr;
			}
			uintmax_t UniqueID;
			Packet->read<uintmax_t>(UniqueID);
			if (CHID == ChannelID::Unreliable_Latest)
			{
				if (Unreliable_Latest->TryReceive(Packet, UniqueID))
				{
					//
					//	Push the received packet into this client
					KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, (ULONG_PTR)Completions::RecvUnread, &Packet->Overlap), false);
				}
				return nullptr;
			}
			//
			//	Formulate an acknowledgement
			NetPacket_Send* ACK = ACKPacketPool->GetFreeObject();
			if (ACK)
			{
				ACK->AddDestination(_ADDR_RECV);
				ACK->SetPID(PacketID::Acknowledgement);
				ACK->SetCID(ClientID::Client);
				ACK->write<long long>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
				ACK->write<PacketID>(PacketID::Data);
				ACK->write<ChannelID>(CHID);
				ACK->write<uintmax_t>(UniqueID);
				//	TODO: add more data..?
			}
			if (CHID == ChannelID::Reliable_Any)
			{
				//
				//	Push the received packet into this client
				KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, (ULONG_PTR)Completions::RecvUnread, &Packet->Overlap), false);

			}
			else if (CHID == ChannelID::Reliable_Latest)
			{
				//
				//	LATEST class packets only process new uniqueIDs
				if (Reliable_Latest->TryReceive(Packet, UniqueID))
				{
					//
					//	Push the received packet into this client
					KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, (ULONG_PTR)Completions::RecvUnread, &Packet->Overlap), false);
				}
			}
			else if (CHID == ChannelID::Reliable_Ordered)
			{
				//
				//	Loop through any packets returned and give them to this client
				for (auto _Packet : Reliable_Ordered->TryReceive(Packet, UniqueID))
				{
					KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, (ULONG_PTR)Completions::RecvUnread, &_Packet->Overlap), false);
				}
			}
			//
			//	Return the acknowledgement to be sent from the calling NetPoint
			return ACK;
		}

		void ReturnPacket(NetPacket_Send* Packet)
		{
			
			if (Packet->GetPID() == PacketID::Acknowledgement)
			{
				//printf("->ReturnACK\n");
				KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, (ULONG_PTR)Completions::ReleaseACK, &Packet->Overlap), false);
			}
			else
			{
				//printf("->ReturnSEND\n");
				KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, (ULONG_PTR)Completions::ReleaseSEND, &Packet->Overlap), false);
			}
		}

		//
		//	Returns all packets waiting to be processed
		//	Packets are arranged in the order by which they were received
		std::deque<NetPacket_Recv*> GetPackets() {
			std::deque<NetPacket_Recv*> _Packets;
			if (KN_CHECK_RESULT2(GetQueuedCompletionStatusEx(IOCP, pEntries, PENDING_RECVS, &pEntriesCount, 0, false), false)) {
				return _Packets;
			}

			if (pEntriesCount == 0) { return _Packets; }

			for (unsigned int i = 0; i < pEntriesCount; i++) {
				//
				//	Release Send Packet Operation
				if (pEntries[i].lpCompletionKey == (ULONG_PTR)Completions::RecvUnread) {
					_Packets.push_back(static_cast<NetPacket_Recv*>(pEntries[i].lpOverlapped->Pointer));
				}
				else if (pEntries[i].lpCompletionKey == (ULONG_PTR)Completions::ReleaseACK) {
					ACKPacketPool->ReturnUsedObject(static_cast<NetPacket_Send*>(pEntries[i].lpOverlapped->Pointer));
				}
				else if (pEntries[i].lpCompletionKey == (ULONG_PTR)Completions::ReleaseSEND) {
					SendPacketPool->ReturnUsedObject(static_cast<NetPacket_Send*>(pEntries[i].lpOverlapped->Pointer));
				}
			}
			return _Packets;
		}

		/*bool operator==(const SOCKADDR_INET& other) const {
			return _ADDR.Ipv4.sin_addr.S_un.S_addr == other.Ipv4.sin_addr.S_un.S_addr
				&& _ADDR.Ipv4.sin_port == other.Ipv4.sin_port
				&& _ADDR.Ipv4.sin_family == other.Ipv4.sin_family;
		}*/

		//
		//	So we can hide our OVERLAPPED variables
		friend class NetPoint;
	};
}

//namespace std {
//	template<> struct hash<SOCKADDR_INET> {
//		size_t operator()(SOCKADDR_INET const& ADDR) const {
//			return ((hash<ULONG>()(ADDR.Ipv4.sin_addr.S_un.S_addr) ^
//				(hash<USHORT>()(ADDR.Ipv4.sin_port) << 1)) >> 1) ^
//				(hash<ADDRESS_FAMILY>()(ADDR.Ipv4.sin_family) << 1);
//		}
//	};
//}