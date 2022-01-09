#pragma once

namespace KNet
{
	class Unreliable_Any_Channel
	{
	public:
		inline Unreliable_Any_Channel() noexcept {}

		//	Initialize and return a new packet for sending
		inline void StampPacket(NetPacket_Send* Packet) noexcept
		{
			Packet->bDontRelease = false;	//	Doesn't need ACK
			Packet->write<ChannelID>(ChannelID::Unreliable_Any);	//	Write the ChannelID
		}
	};
}