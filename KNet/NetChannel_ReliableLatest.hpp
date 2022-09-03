#pragma once

namespace KNet
{
	class Reliable_Latest_Channel : public Channel
	{
		//
		//	Record the UniqueID of our most recent incoming packet and the UniqueID of our next outgoing packet
		std::atomic<uintmax_t> IN_LastID = 0;	//	Incoming UniqueID
		std::atomic<uintmax_t> OUT_NextID = 1;	//	Outgoing UniqueID
		std::unordered_map<uintmax_t, NetPacket_Send*> OUT_Packets;	//	Unacknowledged outgoing packets

	public:
		inline Reliable_Latest_Channel(uint8_t OPID) noexcept : Channel(ChannelID::Reliable_Latest, OPID) {}

		//	Initialize and return a new packet for sending
		inline void StampPacket(NetPacket_Send* Packet)
		{
			const uintmax_t UniqueID = OUT_NextID++;			//	Store and increment our UniqueID
			Packet->write<uintmax_t>(UniqueID);			//	Write the UniqueID
			//
			//	WARN: The packet could potentially gets sent before the user intends to send it..
			//	TODO: Store the OUT_Packet during Point->SendPacket()..
			Packet->bDontRelease = true;						//	Needs to wait for an ACK
			OUT_Packets[UniqueID] = Packet;						//	Store this packet until it gets ACK'd
		}

		inline NetPacket_Send* TryACK(const uintmax_t& UniqueID)
		{
			//
			//	If we have an outgoing packet waiting to be acknowledged
			if (OUT_Packets.count(UniqueID))
			{
				NetPacket_Send* AcknowledgedPacket = OUT_Packets[UniqueID];
				OUT_Packets.erase(UniqueID);
				AcknowledgedPacket->bDontRelease = false;
				return AcknowledgedPacket;
			}
			//
			//	No waiting packet, return nullptr
			return nullptr;
		}

		//	Receives a packet
		inline const bool TryReceive(NetPacket_Recv* const Packet, const uintmax_t& UniqueID) noexcept
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