#pragma once

namespace KNet
{
	class Unreliable_Any_Channel : public Channel
	{
		uintmax_t OUT_NextID = 1;	//	Outgoing UniqueID
	public:
		inline Unreliable_Any_Channel(uint8_t OPID) noexcept : Channel(ChannelID::Unreliable_Any, OPID) {}

		//	Initialize and return a new packet for sending
		inline void StampPacket(NetPacket_Send*const Packet) noexcept override
		{
			Packet->bDontRelease = false;	//	Doesn't need ACK
			OUT_NextID++;
			Packet->SetUID(OUT_NextID);	//	Write and increment the current UniqueID
		}

		inline void GetUnacknowledgedPackets(std::deque<NetPacket_Send*>& Packets_, const std::chrono::time_point<std::chrono::steady_clock>& Now) override
		{ }
	};
}
