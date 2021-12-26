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
			RecvUnread
		};
	public:

		//	WARN: may be incorrect port_recv
		//	TODO: get the recv port from the remote client somehow..
		NetClient(std::string IP, u_short PORT)
			: OVERLAPPED(), _IP_RECV(IP), _PORT_RECV(PORT + 1),
			pEntries(new OVERLAPPED_ENTRY[PENDING_SENDS + PENDING_RECVS]), pEntriesCount(0)
		{
			//	WARN: can run out of free objects
			//	TODO: find another way to store the address in this object..
			_ADDR_RECV = AddressPool->GetFreeObject();
			_ADDR_RECV->Resolve(_IP_RECV, _PORT_RECV);
			//
			ACKPacketPool = new NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>(GLOBAL_SENDS);
			SendPacketPool = new NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>(GLOBAL_SENDS);
			//
			//	Create the IOCP handle
			IOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
			if (IOCP == NULL) {
				printf("Create IO Completion Port - Client Error: (%i)\n", GetLastError());
			}
		}

		~NetClient()
		{
			delete[] pEntries;
			CloseHandle(IOCP);
		}

		template <ChannelID CHID> NetPacket_Send* GetFreePacket()
		{
			NetPacket_Send* Packet = SendPacketPool->GetFreeObject();
			if (Packet)
			{
				Packet->bChildPacket = true;
				Packet->Child = this;
				//
				Packet->write<PacketID>(PacketID::Data);	//	This is a Data Packet
				Packet->write<ClientID>(ClientID::Client);	//	Going to this NetClient
				Packet->AddDestination(_ADDR_RECV);			//	Using it's receive address
				Packet->write<ChannelID>(CHID);				//	On the specified ChannelID
				Packet->write<uintmax_t>(12345);			//	With this UniqueID
				Packet->write<bool>(false);					//	Ignoring old packets?
			}
			return Packet;
		}

		void ProcessPacket_Acknowledgement(NetPacket_Recv* Packet)
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
				// / std::chrono::high_resolution_clock::period::den
				//printf("\tRecv_Handshake_ACK %llu\n", ns.count());
			}
			else if (PID == PacketID::Data)
			{
				printf("\tRecv_Data_ACK %fms\n", ms.count()*0.001f);
			}
		}
		NetPacket_Send* ProcessPacket_Handshake(NetPacket_Recv* Packet)
		{
			//
			//	Push the received packet into this client
			//KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, (ULONG_PTR)Completions::RecvUnread, &Packet->Overlap), false);
			//
			//	Formulate an acknowledgement
			NetPacket_Send* ACK = ACKPacketPool->GetFreeObject();
			ACK->bChildPacket = true;	// packet will get returned to us
			ACK->Child = this;
			ACK->bAckPacket = true;		// packet returns to correct pool
			if (ACK)
			{
				ACK->AddDestination(_ADDR_RECV);
				ACK->write<PacketID>(PacketID::Acknowledgement);
				ACK->write<ClientID>(ClientID::Client);
				ACK->write<long long>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
				ACK->write<PacketID>(PacketID::Handshake);
				//	TODO: add more data..like actual packet ids
			}
			//
			//	Return the acknowledgement to be sent from the calling NetPoint
			return ACK;
		}
		NetPacket_Send* ProcessPacket_Data(NetPacket_Recv* Packet)
		{
			ChannelID CHID;
			uintmax_t UniqueID;
			bool bLatest;
			Packet->read<ChannelID>(CHID);
			Packet->read<uintmax_t>(UniqueID);
			Packet->read<bool>(bLatest);

			//
			//	Push the received packet into this client
			KN_CHECK_RESULT(PostQueuedCompletionStatus(IOCP, NULL, (ULONG_PTR)Completions::RecvUnread, &Packet->Overlap), false);
			//
			//	Formulate an acknowledgement
			NetPacket_Send* ACK = ACKPacketPool->GetFreeObject();
			ACK->bChildPacket = true;	// packet will get returned to us
			ACK->Child = this;
			ACK->bAckPacket = true;		// packet returns to correct pool
			if (ACK)
			{
				ACK->AddDestination(_ADDR_RECV);
				ACK->write<PacketID>(PacketID::Acknowledgement);
				ACK->write<ClientID>(ClientID::Client);
				ACK->write<long long>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
				ACK->write<PacketID>(PacketID::Data);
				//	TODO: add more data..like actual packet ids
			}
			//
			//	Return the acknowledgement to be sent from the calling NetPoint
			return ACK;
		}

		//
		//	WARN: This is obsolete.
		//	TODO: Return packets using IOCP.
		void ReturnPacket(NetPacket_Send* Packet)
		{
			if (Packet->bAckPacket)
			{
				//printf("->ReturnACK\n");
				ACKPacketPool->ReturnUsedObject(Packet);
			}
			else
			{
				printf("->ReturnSEND\n");
				SendPacketPool->ReturnUsedObject(Packet);
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
				//if (pEntries[i].lpCompletionKey == (ULONG_PTR)PointCompletion::SendRelease) {
				//	NetPacket_Send* Packet = reinterpret_cast<NetPacket_Send*>(pEntries[i].lpOverlapped->Pointer);
				//	Packet->m_write = 0;
				//	KNet::SendPacketPool->ReturnUsedObject(Packet);
				//}
				////
				////	Unread Packet Operation
				//else if (pEntries[i].lpCompletionKey == (ULONG_PTR)Completions::RecvUnread) {
				_Packets.push_back(reinterpret_cast<NetPacket_Recv*>(pEntries[i].lpOverlapped->Pointer));
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