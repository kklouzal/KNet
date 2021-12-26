#pragma once

namespace KNet
{
	class Reliable_Latest_Channel
	{
		//
		//	Record the UniqueID of our most recent incoming packet and the UniqueID of our next outgoing packet
		std::atomic<uintmax_t> IN_LastID = 0;	//	Incoming UniqueID
		std::atomic<uintmax_t> OUT_NextID = 1;	//	Outgoing UniqueID
		std::unordered_map<unsigned long, NetPacket_Send*> OUT_Packets;	//	Unacknowledged outgoing packets

	public:
		inline Reliable_Latest_Channel() {}

		//	Initialize and return a new packet for sending
		inline void StampPacket(NetPacket_Send* Packet)
		{
			uintmax_t UniqueID = OUT_NextID++;						//	Store and increment our UniqueID
			Packet->write<ChannelID>(ChannelID::Reliable_Latest);	//	Write the ChannelID
			Packet->write<uintmax_t>(UniqueID);						//	Write the UniqueID
			//
			//	WARN: The packet could potentially gets sent before the user intends to send it..
			//	TODO: Store the OUT_Packet during Point->SendPacket()..
			Packet->bDontRelease = true;							//	Needs to wait for an ACK
			OUT_Packets[UniqueID] = Packet;							//	Store this packet until it gets ACK'd
		}

		//	Receives a packet
		inline const bool TryReceive(NetPacket_Recv* const Packet, uintmax_t& UniqueID)
		{
			if (UniqueID <= IN_LastID)
			{
				//
				//	Drop this packet
				Packet->bRecycle = true;
				return false;
			}
			else {
				IN_LastID.store(UniqueID);
				return true;
			}
		}
	};
}