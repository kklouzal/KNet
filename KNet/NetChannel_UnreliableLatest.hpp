#pragma once

namespace KNet
{
	class Unreliable_Latest_Channel
	{
		//
		//	Record the UniqueID of our most recent incoming packet and the UniqueID of our next outgoing packet
		std::atomic<uintmax_t> IN_LastID = 0;	//	Incoming UniqueID
		std::atomic<uintmax_t> OUT_NextID = 1;	//	Outgoing UniqueID

	public:
		inline Unreliable_Latest_Channel() noexcept {}

		//	Initialize and return a new packet for sending
		inline void StampPacket(NetPacket_Send* Packet) noexcept
		{
			Packet->bDontRelease = false;	//	Doesn't need ACK
			Packet->write<ChannelID>(ChannelID::Unreliable_Latest);	//	Write the ChannelID
			Packet->write<uintmax_t>(OUT_NextID++);					//	Write and increment the current UniqueID
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