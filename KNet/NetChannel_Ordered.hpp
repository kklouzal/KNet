#pragma once

namespace KNet
{
	class Reliable_Ordered_Channel
	{
		//
		//	Record the UniqueID of our most recent incoming packet and the UniqueID of our next outgoing packet
		std::atomic<uintmax_t> IN_LastID = 0;	//	Latest Incoming UniqueID
		std::atomic<uintmax_t> IN_NextID = 1;	//	Next Incoming UniqueID to process
		std::atomic<uintmax_t> OUT_NextID = 1;	//	Next Outgoing UniqueID
		std::unordered_map<uintmax_t, NetPacket_Send*> OUT_Packets;	//	Unacknowledged outgoing packets
		std::unordered_map<uintmax_t, NetPacket_Recv*> IN_Packets;	//	Unprocessed incoming packets

	public:
		inline Reliable_Ordered_Channel() {}

		//	Initialize and return a new packet for sending
		inline void StampPacket(NetPacket_Send* Packet)
		{
			uintmax_t UniqueID = OUT_NextID++;						//	Store and increment our UniqueID
			Packet->write<ChannelID>(ChannelID::Reliable_Ordered);	//	Write the ChannelID
			Packet->write<uintmax_t>(UniqueID);						//	Write the UniqueID
			//
			//	WARN: The packet could potentially gets sent before the user intends to send it..
			//	TODO: Store the OUT_Packet during Point->SendPacket()..
			Packet->bDontRelease = true;							//	Needs to wait for an ACK
			OUT_Packets[UniqueID] = Packet;							//	Store this packet until it gets ACK'd
		}

		inline NetPacket_Send* TryACK(uintmax_t UniqueID)
		{
			//
			//	If we have an outgoing packet waiting to be acknowledged
			if (OUT_Packets.count(UniqueID))
			{
				NetPacket_Send* AcknowledgedPacket = OUT_Packets[UniqueID];
				OUT_Packets[UniqueID] = nullptr;
				AcknowledgedPacket->bDontRelease = false;
				return AcknowledgedPacket;
			}
			//
			//	No waiting packet, return nullptr
			return nullptr;
		}

		//	Receives a packet
		inline std::deque<NetPacket_Recv*> TryReceive(NetPacket_Recv* const Packet, uintmax_t& UniqueID)
		{
			std::deque<NetPacket_Recv*> PacketBacklog;
			//
			//	Drop packets with a UniqueID older than the highest received ID
			if (UniqueID <= IN_LastID)
			{
				//
				//	Drop this packet
				Packet->bRecycle = true;
				return PacketBacklog;
			}
			else {
				IN_LastID.store(UniqueID);
				//
				//	Process the packet if its UniqueID is the next one we need
				if (UniqueID == IN_NextID)
				{
					//
					//	Add this packet into our return packets deque
					PacketBacklog.push_back(Packet);
					//
					//	Loop through stored packets while it has the next expected UniqueIDs
					while (IN_Packets.count(++IN_NextID))
					{
						//
						//	Push the stored packet into our backlog and remove it from the container
						PacketBacklog.push_back(IN_Packets[IN_NextID]);
						IN_Packets[IN_NextID] = nullptr;
					}
					return PacketBacklog;
				}
				//
				//	Store packets with a UniqueID greater than the one we need to process next
				else {
					IN_Packets[UniqueID] = Packet;
					return PacketBacklog;
				}
			}
		}
	};
}