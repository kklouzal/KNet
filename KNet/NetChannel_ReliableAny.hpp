#pragma once

namespace KNet
{
	class Reliable_Any_Channel
	{
		//
		//	Record the UniqueID of our next outgoing packet
		std::atomic<uintmax_t> OUT_NextID = 1;	//	Outgoing UniqueID
		std::unordered_map<uintmax_t, NetPacket_Send*> OUT_Packets;	//	Unacknowledged outgoing packets

	public:
		inline Reliable_Any_Channel() {}

		//	Initialize and return a new packet for sending
		inline void StampPacket(NetPacket_Send* Packet)
		{
			uintmax_t UniqueID = OUT_NextID++;						//	Store and increment our UniqueID
			Packet->write<ChannelID>(ChannelID::Reliable_Any);	//	Write the ChannelID
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
	};
}