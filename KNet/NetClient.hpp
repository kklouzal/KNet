#pragma once

namespace KNet
{
	class NetClient : public OVERLAPPED
	{
		NetAddress* _ADDR_RECV;
		std::string _IP_RECV;
		u_short _PORT_RECV;		//	Port this client receives on
		u_short _PORT_SEND;		//	Port this client sends on
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
		std::unordered_map<uint8_t, Channel*> Net_Channels;
		//
		std::unordered_map<uint8_t, Unreliable_Any_Channel*> Unreliable_Any;
		//std::unordered_map<uint8_t, Unreliable_Latest_Channel*> Unreliable_Latest;
		//std::unordered_map<uint8_t, Reliable_Any_Channel*> Reliable_Any;
		//std::unordered_map<uint8_t, Reliable_Latest_Channel*> Reliable_Latest;
		//std::unordered_map<uint8_t, Reliable_Ordered_Channel*> Reliable_Ordered;
		//
		//	When was our last packet received
		std::chrono::time_point<std::chrono::steady_clock> LastPacketTime;
		//
		//	Timeout Period in seconds
		std::chrono::seconds TimeoutPeriod;
	public:
		//	WARN: may be incorrect port_recv
		//	TODO: get the recv port from the remote client somehow..
		NetClient(std::string IP, u_short PORT)
			: OVERLAPPED(), _IP_RECV(IP), _PORT_RECV(PORT + 1), _PORT_SEND(PORT),
			pEntries(new OVERLAPPED_ENTRY[PENDING_SENDS + PENDING_RECVS]), pEntriesCount(0),
			//Unreliable_Any(new Unreliable_Any_Channel()),
			//Unreliable_Latest(new Unreliable_Latest_Channel()),
			//Reliable_Any(new Reliable_Any_Channel()),
			//Reliable_Latest(new Reliable_Latest_Channel()),
			//Reliable_Ordered(new Reliable_Ordered_Channel()),
			LastPacketTime(std::chrono::high_resolution_clock::now()),
			TimeoutPeriod(30)
		{
			//	WARN: can run out of free objects
			//	TODO: find another way to store the address in this object..
			_ADDR_RECV = AddressPool->GetFreeObject();
			_ADDR_RECV->Resolve(_IP_RECV, _PORT_RECV);
			//
			ACKPacketPool = new NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>(GLOBAL_SENDS, this);
			SendPacketPool = new NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>(GLOBAL_SENDS, this);
			//
			//	Create the IOCP handle
			IOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
			if (IOCP == nullptr) {
				printf("Create IO Completion Port - Client Error: (%lu)\n", GetLastError());
			}
			Pointer = this; 
		}

		~NetClient()
		{
			for (auto& Channel_ : Net_Channels)
			{
				delete Channel_.second;
			}
			//
			/*for (auto& Channel_ : Reliable_Ordered)
			{
				delete Channel_.second;
			}
			for (auto& Channel_ : Reliable_Latest)
			{
				delete Channel_.second;
			}
			for (auto& Channel_ : Reliable_Any)
			{
				delete Channel_.second;
			}
			for (auto& Channel_ : Unreliable_Latest)
			{
				delete Channel_.second;
			}
			for (auto& Channel_ : Unreliable_Any)
			{
				delete Channel_.second;
			}*/
			delete[] pEntries;
			AddressPool->ReturnUsedObject(_ADDR_RECV);
			CloseHandle(IOCP);
			printf("DELETE THE CLIENT\n");
		}

		template <ChannelID T> void RegisterChannel(uint8_t OPID)
		{
			if (!Net_Channels.count(OPID))
			{
				if (T == ChannelID::Unreliable_Any)
				{
					Net_Channels[OPID] = new Unreliable_Any_Channel(OPID);
				}
				else if (T == ChannelID::Unreliable_Latest)
				{
					Net_Channels[OPID] = new Unreliable_Latest_Channel(OPID);
				}
				else if (T == ChannelID::Reliable_Any)
				{
					Net_Channels[OPID] = new Reliable_Any_Channel(OPID);
				}
				else if (T == ChannelID::Reliable_Latest)
				{
					Net_Channels[OPID] = new Reliable_Latest_Channel(OPID);
				}
				else if (T == ChannelID::Reliable_Ordered)
				{
					Net_Channels[OPID] = new Reliable_Ordered_Channel(OPID);
				}
			}
		}

		NetPacket_Send* GetFreePacket(uint8_t OperationID)
		{
			NetPacket_Send* Packet = SendPacketPool->GetFreeObject();
			if (Packet)
			{
				Packet->AddDestination(_ADDR_RECV);
				Packet->SetPID(PacketID::Data);
				Packet->SetCID(ClientID::Client);
				Packet->SetOID(OperationID);
				//
				//	Stamp Channel Information
				//
				//	WARN: Assumes all created packets will be sent and eventually received..
				//	TODO: Stamp packets just before the Send_Thread actually sends it off..
				Net_Channels[OperationID]->StampPacket(Packet);
			}
			return Packet;
		}

		inline void ProcessPacket_Acknowledgement(NetPacket_Recv* Packet)
		{
			PacketID PID;
			Packet->read<PacketID>(PID);
			if (PID == PacketID::Handshake)
			{
				//printf("\tRecv_Handshake_ACK %fms\n", ms.count() * 0.001f);
			}
			else if (PID == PacketID::Data)
			{
				uintmax_t UniqueID;
				uint8_t OPID;
				Packet->read<uintmax_t>(UniqueID);
				Packet->read<uint8_t>(OPID);
				ChannelID CH_ID = Net_Channels[OPID]->GetChannelID();
				if (CH_ID == ChannelID::Reliable_Any) {
					NetPacket_Send* AcknowledgedPacket = static_cast<Reliable_Any_Channel*>(Net_Channels[OPID])->TryACK(UniqueID);
					//
					//	If a packet was acknowledged, return it to the main thread to be placed back in its available packet pool
					if (AcknowledgedPacket) {
						const std::chrono::microseconds AckTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::nanoseconds(Packet->GetTimestamp() - AcknowledgedPacket->GetTimestamp()));
						const std::chrono::microseconds RttTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::nanoseconds(std::chrono::high_resolution_clock::now().time_since_epoch().count() - AcknowledgedPacket->GetTimestamp()));
						printf("\tRecv_ANY_ACK\t\tPID:%u UID:%ju ACK:%.3fms RTT:%.3fms\n", PID, UniqueID, AckTime.count() * 0.001f, RttTime.count() * 0.001f);
						//printf("Return ANY Packet From ACK InternalID %i InternalUniqueID %i\n", AcknowledgedPacket->InternalID, AcknowledgedPacket->InternalUniqueID);
						ReturnPacket(AcknowledgedPacket);
					}
				}
				else if (CH_ID == ChannelID::Reliable_Latest) {
					NetPacket_Send* AcknowledgedPacket = static_cast<Reliable_Latest_Channel*>(Net_Channels[OPID])->TryACK(UniqueID);
					//
					//	If a packet was acknowledged, return it to the main thread to be placed back in its available packet pool
					if (AcknowledgedPacket) {
						const std::chrono::microseconds AckTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::nanoseconds(Packet->GetTimestamp() - AcknowledgedPacket->GetTimestamp()));
						const std::chrono::microseconds RttTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::nanoseconds(std::chrono::high_resolution_clock::now().time_since_epoch().count() - AcknowledgedPacket->GetTimestamp()));
						printf("\tRecv_LATEST_ACK\t\tPID:%u UID:%ju ACK:%.3fms RTT:%.3fms\n", PID, UniqueID, AckTime.count() * 0.001f, RttTime.count() * 0.001f);
						//printf("Return LATEST Packet From ACK InternalID %i InternalUniqueID %i\n", AcknowledgedPacket->InternalID, AcknowledgedPacket->InternalUniqueID);
						ReturnPacket(AcknowledgedPacket);
					}
				}
				else if (CH_ID == ChannelID::Reliable_Ordered) {
					NetPacket_Send* AcknowledgedPacket = static_cast<Reliable_Ordered_Channel*>(Net_Channels[OPID])->TryACK(UniqueID);
					//
					//	If a packet was acknowledged, return it to the main thread to be placed back in its available packet pool
					if (AcknowledgedPacket) {
						const std::chrono::microseconds AckTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::nanoseconds(Packet->GetTimestamp() - AcknowledgedPacket->GetTimestamp()));
						const std::chrono::microseconds RttTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::nanoseconds(std::chrono::high_resolution_clock::now().time_since_epoch().count() - AcknowledgedPacket->GetTimestamp()));
						printf("\tRecv_ORDERED_ACK\tPID:%u UID:%ju ACK:%.3fms RTT:%.3fms\n", PID, UniqueID, AckTime.count() * 0.001f, RttTime.count() * 0.001f);
						//printf("Return ORDERED Packet From ACK InternalID %i InternalUniqueID %i\n", AcknowledgedPacket->InternalID, AcknowledgedPacket->InternalUniqueID);
						ReturnPacket(AcknowledgedPacket);
					}
				}
			}
		}

		inline NetPacket_Send* ProcessPacket_Handshake(NetPacket_Recv* Packet) noexcept
		{
			//
			//	Push the received packet into this client
			//KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, static_cast<ULONG_PTR>(Completions::RecvUnread), &Packet->Overlap), false);
			//
			//	Formulate an acknowledgement
			NetPacket_Send* ACK = ACKPacketPool->GetFreeObject();
			if (ACK)
			{
				ACK->AddDestination(_ADDR_RECV);
				ACK->SetPID(PacketID::Acknowledgement);
				ACK->SetCID(ClientID::Client);
				ACK->SetTimestamp(std::chrono::high_resolution_clock::now().time_since_epoch().count());
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
				KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, static_cast<ULONG_PTR>(Completions::RecvUnread), &Packet->Overlap), false);
				return nullptr;
			}

			uintmax_t UniqueID;
			Packet->read<uintmax_t>(UniqueID);
			uint8_t OPID = Packet->GetOID();
			//Packet->read<uint8_t>(OPID);

			if (CHID == ChannelID::Unreliable_Latest)
			{
				if (static_cast<Unreliable_Latest_Channel*>(Net_Channels[OPID])->TryReceive(Packet, UniqueID))
				{
					//
					//	Push the received packet into this client
					KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, static_cast<ULONG_PTR>(Completions::RecvUnread), &Packet->Overlap), false);
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
				ACK->SetTimestamp(std::chrono::high_resolution_clock::now().time_since_epoch().count());
				ACK->write<PacketID>(PacketID::Data);
				ACK->write<uintmax_t>(UniqueID);
				ACK->write<uint8_t>(OPID);
				//	TODO: add more data..?
			}
			if (CHID == ChannelID::Reliable_Any)
			{
				//
				//	Push the received packet into this client
				KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, static_cast<ULONG_PTR>(Completions::RecvUnread), &Packet->Overlap), false);

			}
			else if (CHID == ChannelID::Reliable_Latest)
			{
				//
				//	LATEST class packets only process new uniqueIDs
				if (static_cast<Reliable_Latest_Channel*>(Net_Channels[OPID])->TryReceive(Packet, UniqueID))
				{
					//
					//	Push the received packet into this client
					KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, static_cast<ULONG_PTR>(Completions::RecvUnread), &Packet->Overlap), false);
				}
			}
			else if (CHID == ChannelID::Reliable_Ordered)
			{
				//
				//	Loop through any packets returned and give them to this client
				for (auto _Packet : static_cast<Reliable_Ordered_Channel*>(Net_Channels[OPID])->TryReceive(Packet, UniqueID))
				{
					KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, static_cast<ULONG_PTR>(Completions::RecvUnread), &_Packet->Overlap), false);
				}
			}
			//
			//	Return the acknowledgement to be sent from the calling NetPoint
			return ACK;
		}

		void ReturnPacket(NetPacket_Send* Packet) noexcept
		{
			
			if (Packet->GetPID() == PacketID::Acknowledgement)
			{
				//printf("->ReturnACK\n");
				KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, static_cast<ULONG_PTR>(Completions::ReleaseACK), &Packet->Overlap), false);
			}
			else
			{
				//printf("->ReturnSEND\n");
				KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, static_cast<ULONG_PTR>(Completions::ReleaseSEND), &Packet->Overlap), false);
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
				if (pEntries[i].lpCompletionKey == static_cast<ULONG_PTR>(Completions::RecvUnread)) {
					_Packets.push_back(static_cast<NetPacket_Recv*>(pEntries[i].lpOverlapped->Pointer));
				}
				else if (pEntries[i].lpCompletionKey == static_cast<ULONG_PTR>(Completions::ReleaseACK)) {
					ACKPacketPool->ReturnUsedObject(static_cast<NetPacket_Send*>(pEntries[i].lpOverlapped->Pointer));
				}
				else if (pEntries[i].lpCompletionKey == static_cast<ULONG_PTR>(Completions::ReleaseSEND)) {
					SendPacketPool->ReturnUsedObject(static_cast<NetPacket_Send*>(pEntries[i].lpOverlapped->Pointer));
				}
			}
			return _Packets;
		}

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