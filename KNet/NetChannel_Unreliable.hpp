//#pragma once
//
//namespace KNet
//{
//	struct UnreliableOperation
//	{
//		//
//		//	Record the UniqueID of our most recent incoming packet and the UniqueID of our next outgoing packet
//		std::atomic<uintmax_t> IN_LastID = 0;	//	Incoming UniqueID
//		std::atomic<uintmax_t> OUT_NextID = 1;	//	Outgoing UniqueID
//	};
//	class UnreliableChannel
//	{
//		NetAddress* const Address;
//		const PacketType ChannelID;
//
//		std::deque<SendPacket*> OUT_Packets;	//	Packets that need to be deleted
//
//		std::unordered_map<unsigned long, UnreliableOperation> Operations;
//
//		std::deque<ReceivePacket*> NeedsProcessed;	//	Packets that need to be processed
//
//	public:
//		inline UnreliableChannel(NetAddress* const Addr, const PacketType ChanID)
//			: Address(Addr), ChannelID(ChanID),	NeedsProcessed() {}
//
//		//	Initialize and return a new packet for sending
//		inline SendPacket* NewPacket(const unsigned long& OP)
//		{
//			SendPacket* Packet = new SendPacket(Operations[OP].OUT_NextID++, ChannelID, OP, Address, true);
//			OUT_Packets.push_back(Packet);
//			return Packet;
//		}
//
//		inline void DeleteUsed()
//		{
//			auto Packet = OUT_Packets.begin();
//			while (Packet != OUT_Packets.end())
//			{
//				if ((*Packet)->NeedsDelete == 1) {
//					delete (*Packet);
//					Packet = OUT_Packets.erase(Packet);
//				}
//				else {
//					++Packet;
//				}
//			}
//		}
//
//		//	Receives a packet
//		inline void Receive(ReceivePacket* const IN_Packet)
//		{
//			if (IN_Packet->GetPacketID() <= Operations[IN_Packet->GetOperationID()].IN_LastID.load()) { delete IN_Packet; return; }
//			Operations[IN_Packet->GetOperationID()].IN_LastID.store(IN_Packet->GetPacketID());
//			NeedsProcessed.push_back(IN_Packet);
//		}
//	};
//}