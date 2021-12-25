#pragma once

namespace KNet
{
	class NetClient : private OVERLAPPED
	{
		NetAddress* _ADDR_RECV;
		std::string _IP_RECV;
		u_short _PORT_RECV;
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

		void ProcessPacket_Acknowledgement(NetPacket_Recv* Packet)
		{
			printf("\tProcessACK\n");
		}
		NetPacket_Send* ProcessPacket_Handshake(NetPacket_Recv* Packet)
		{
			//
			//	Push the received packet into this client
			if (!PostQueuedCompletionStatus(IOCP, NULL, (ULONG_PTR)Completions::RecvUnread, &Packet->Overlap)) {
				printf("Post Queued Completion Status - Send Error: %i\n", GetLastError());
			}
			//
			//	Formulate an acknowledgement
			NetPacket_Send* ACK = SendPacketPool->GetFreeObject();
			ACK->bChildPacket = true;	// packet will get returned to us
			ACK->Child = this;
			if (ACK)
			{
				ACK->AddDestination(_ADDR_RECV);
				ACK->write<PacketID>(PacketID::Acknowledgement);
				ACK->write<ClientID>(ClientID::Client);
				//	TODO: add more data..like actual packet ids
			}
			//
			//	Return the acknowledgement to be sent from the calling NetPoint
			return ACK;
		}
		NetPacket_Send* ProcessPacket_Data(NetPacket_Recv* Packet)
		{
			//
			//	Push the received packet into this client
			if (!PostQueuedCompletionStatus(IOCP, NULL, (ULONG_PTR)Completions::RecvUnread, &Packet->Overlap)) {
				printf("Post Queued Completion Status - Send Error: %i\n", GetLastError());
			}
			//
			//	Formulate an acknowledgement
			NetPacket_Send* ACK = SendPacketPool->GetFreeObject();
			ACK->bChildPacket = true;	// packet will get returned to us
			ACK->Child = this;
			if (ACK)
			{
				ACK->AddDestination(_ADDR_RECV);
				ACK->write<PacketID>(PacketID::Acknowledgement);
				ACK->write<ClientID>(ClientID::Client);
				//	TODO: add more data..like actual packet ids
			}
			//
			//	Return the acknowledgement to be sent from the calling NetPoint
			return ACK;
		}

		void ReturnPacket(NetPacket_Send* Packet)
		{
			printf("->ReturnACK\n");
			SendPacketPool->ReturnUsedObject(Packet);
		}

		//
		//	Returns all packets waiting to be processed
		//	Packets are arranged in the order by which they were received
		std::deque<NetPacket_Recv*> GetPackets() {
			std::deque<NetPacket_Recv*> _Packets;
			if (!GetQueuedCompletionStatusEx(IOCP, pEntries, PENDING_RECVS, &pEntriesCount, 0, false)) {
				printf("Get Queued Completion Status - Client Error: %i\n", GetLastError());
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