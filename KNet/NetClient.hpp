#pragma once

namespace KNet
{
	class NetClient : public OVERLAPPED
	{
		NetAddress* _ADDR_RECV;
		const std::string _IP_RECV;		//	IP Address to bind to
		const u_short _PORT_RECV;		//	Port this client receives on
		const u_short _PORT_SEND;		//	Port this client sends on
		const std::string Client_ID;		//	IP/Port Identifier
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
			Client_ID(IP + ":" + std::to_string(PORT)),
			pEntries(new OVERLAPPED_ENTRY[PENDING_SENDS + PENDING_RECVS]), pEntriesCount(0),
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

		std::string GetClientID()
		{
			return Client_ID;
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
				Packet->SetClientID(Client_ID);
			}
			return Packet;
		}

		inline void ProcessPacket_Acknowledgement(NetPacket_Recv* Packet)
		{
			PacketID PID;
			Packet->read<PacketID>(PID);
			if (PID == PacketID::Data)
			{
				uint8_t OPID = Packet->GetOID();
				uintmax_t UniqueID = Packet->GetUID();
				ChannelID CH_ID = Net_Channels[OPID]->GetChannelID();
				NetPacket_Send* AcknowledgedPacket = Net_Channels[OPID]->TryACK(UniqueID);
				if (AcknowledgedPacket) {
					const std::chrono::microseconds AckTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::nanoseconds(Packet->GetTimestamp() - AcknowledgedPacket->GetTimestamp()));
					const std::chrono::microseconds RttTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::nanoseconds(std::chrono::high_resolution_clock::now().time_since_epoch().count() - AcknowledgedPacket->GetTimestamp()));
					printf("\tRecv_ACK\tPID:%u UID:%ju OPID:%i ACK:%.3fms RTT:%.3fms\n", PID, UniqueID, OPID, AckTime.count() * 0.001f, RttTime.count() * 0.001f);
					ReturnPacket(AcknowledgedPacket);
				}
			}
			else if (PID == PacketID::Handshake)
			{
				//printf("\tRecv_Handshake_ACK %fms\n", ms.count() * 0.001f);
			}
		}

		inline NetPacket_Send* ProcessPacket_Handshake(NetPacket_Recv* Packet) noexcept
		{
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
			}
			//
			//	Return the acknowledgement to be sent from the calling NetPoint
			return ACK;
		}

		inline NetPacket_Send* ProcessPacket_Data(NetPacket_Recv* Packet)
		{
			uint8_t OPID = Packet->GetOID();
			ChannelID CH_ID = Net_Channels[OPID]->GetChannelID();

			if (CH_ID == ChannelID::Unreliable_Any)
			{
				//
				//	Push the received packet into this client
				KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, static_cast<ULONG_PTR>(Completions::RecvUnread), &Packet->Overlap), false);
				return nullptr;
			}

			uintmax_t UniqueID = Packet->GetUID();

			if (CH_ID == ChannelID::Unreliable_Latest)
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
				//
				//	Values to acknowledge
				ACK->SetOID(OPID);
				ACK->SetUID(UniqueID);
				ACK->write<PacketID>(PacketID::Data);
			}
			switch (CH_ID)
			{
				case ChannelID::Reliable_Any:
				{
					//
					//	Push the received packet into this client
					KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, static_cast<ULONG_PTR>(Completions::RecvUnread), &Packet->Overlap), false);

				}
				break;
				case ChannelID::Reliable_Latest:
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
				break;
				case ChannelID::Reliable_Ordered:
				{
					//
					//	Loop through any packets returned and give them to this client
					for (auto _Packet : static_cast<Reliable_Ordered_Channel*>(Net_Channels[OPID])->TryReceive(Packet, UniqueID))
					{
						KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, static_cast<ULONG_PTR>(Completions::RecvUnread), &_Packet->Overlap), false);
					}
				}
				break;
			}
			//
			//	Return the acknowledgement to be sent from the calling NetPoint
			return ACK;
		}

		void ReturnPacket(NetPacket_Send* Packet) noexcept
		{
			if (Packet->GetPID() == PacketID::Acknowledgement) {
				KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, static_cast<ULONG_PTR>(Completions::ReleaseACK), &Packet->Overlap), false);
			}
			else {
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
				switch (pEntries[i].lpCompletionKey)
				{
					case static_cast<ULONG_PTR>(Completions::RecvUnread):
					{
						_Packets.push_back(static_cast<NetPacket_Recv*>(pEntries[i].lpOverlapped->Pointer));
					}
					break;
					case static_cast<ULONG_PTR>(Completions::ReleaseACK):
					{
						ACKPacketPool->ReturnUsedObject(static_cast<NetPacket_Send*>(pEntries[i].lpOverlapped->Pointer));
					}
					break;
					case static_cast<ULONG_PTR>(Completions::ReleaseSEND):
					{
						SendPacketPool->ReturnUsedObject(static_cast<NetPacket_Send*>(pEntries[i].lpOverlapped->Pointer));
					}
					break;
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