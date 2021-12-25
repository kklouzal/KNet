#pragma once

namespace KNet
{
	class NetClient
	{
		NetAddress* _ADDR_RECV;
		std::string _IP_RECV;
		u_short _PORT_RECV;
		NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>* SendPacketPool = nullptr;
	public:

		//	WARN: may be incorrect port_recv
		//	TODO: get the recv port from the remote client somehow..
		NetClient(std::string IP, u_short PORT)
			: _IP_RECV(IP), _PORT_RECV(PORT + 1)
		{
			//	WARN: can run out of free objects
			//	TODO: find another way to store the address in this object..
			_ADDR_RECV = AddressPool->GetFreeObject();
			_ADDR_RECV->Resolve(_IP_RECV, _PORT_RECV);
			SendPacketPool = new NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>(GLOBAL_SENDS);
		}

		~NetClient()
		{}

		void ProcessPacket_Acknowledgement(NetPacket_Recv* Packet)
		{
			printf("ProcessACK\n");
		}
		NetPacket_Send* ProcessPacket_Handshake(NetPacket_Recv* Packet)
		{
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
			return ACK;
		}
		NetPacket_Send* ProcessPacket_Data(NetPacket_Recv* Packet)
		{
			return nullptr;
		}

		void ReturnPacket(NetPacket_Send* Packet)
		{
			printf("ReturnUsedObject\n");
			SendPacketPool->ReturnUsedObject(Packet);
		}

		/*bool operator==(const SOCKADDR_INET& other) const {
			return _ADDR.Ipv4.sin_addr.S_un.S_addr == other.Ipv4.sin_addr.S_un.S_addr
				&& _ADDR.Ipv4.sin_port == other.Ipv4.sin_port
				&& _ADDR.Ipv4.sin_family == other.Ipv4.sin_family;
		}*/
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