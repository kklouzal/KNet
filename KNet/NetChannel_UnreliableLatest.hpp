#pragma once

namespace KNet
{
	class Unreliable_Latest_Channel : public Channel
	{
		//
		//	Record the UniqueID of our most recent incoming packet and the UniqueID of our next outgoing packet
		uintmax_t IN_LastID = 0;	//	Incoming UniqueID
		uintmax_t OUT_NextID = 1;	//	Outgoing UniqueID

	public:
		inline Unreliable_Latest_Channel(uint8_t OPID) noexcept : Channel(ChannelID::Unreliable_Latest, OPID) {}

		//	Initialize and return a new packet for sending
		inline void StampPacket(NetPacket_Send*const Packet) noexcept override
		{
			Packet->bDontRelease = false;	//	Doesn't need ACK
			OUT_NextID++;
			Packet->SetUID(OUT_NextID);	//	Write and increment the current UniqueID
		}

		//	Receives a packet
		inline const bool TryReceive(NetPacket_Recv*const Packet, const uintmax_t& UniqueID) noexcept
		{
			if (UniqueID <= IN_LastID)
			{
				//
				//	Drop this packet
				Packet->bRecycle = true;
				return false;
			}
			else {
				IN_LastID = UniqueID;
				return true;
			}
		}

		inline void GetUnacknowledgedPackets(std::deque<NetPacket_Send*>& Packets_, const std::chrono::time_point<std::chrono::steady_clock>& Now) override
		{ }
	};
}