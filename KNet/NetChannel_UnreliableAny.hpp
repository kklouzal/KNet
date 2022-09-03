#pragma once

namespace KNet
{
	class Unreliable_Any_Channel : public Channel
	{
	public:
		inline Unreliable_Any_Channel(uint8_t OPID) noexcept : Channel(ChannelID::Unreliable_Any, OPID) {}

		//	Initialize and return a new packet for sending
		inline void StampPacket(NetPacket_Send* Packet) noexcept
		{
			Packet->bDontRelease = false;					//	Doesn't need ACK
			Packet->write<ChannelID>(GetChannelID());	//	Write the ChannelID
		}
	};
}
