#pragma once

namespace KNet
{
	class Unreliable_Any_Channel : public Channel
	{
		std::atomic<uintmax_t> OUT_NextID = 1;	//	Outgoing UniqueID
	public:
		inline Unreliable_Any_Channel(uint8_t OPID) noexcept : Channel(ChannelID::Unreliable_Any, OPID) {}

		//	Initialize and return a new packet for sending
		inline void StampPacket(NetPacket_Send* Packet) noexcept
		{
			Packet->bDontRelease = false;	//	Doesn't need ACK
			Packet->SetUID(OUT_NextID++);	//	Write and increment the current UniqueID
		}
	};
}
