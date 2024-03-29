#pragma once

namespace KNet
{
	class Reliable_Latest_Channel : public Channel
	{
		//
		//	Record the UniqueID of our most recent incoming packet and the UniqueID of our next outgoing packet
		uintmax_t IN_LastID = 0;	//	Incoming UniqueID
		uintmax_t OUT_NextID = 1;	//	Outgoing UniqueID
		std::unordered_map<uintmax_t, NetPacket_Send*const> OUT_Packets = {};	//	Unacknowledged outgoing packets

	public:
		inline Reliable_Latest_Channel(uint8_t OPID) noexcept : Channel(ChannelID::Reliable_Latest, OPID) {}

		//	Initialize and return a new packet for sending
		inline void StampPacket(NetPacket_Send* Packet) override
		{
			const uintmax_t UniqueID = OUT_NextID++;	//	Store and increment our UniqueID
			Packet->SetUID(UniqueID);					//	Write the UniqueID
			Packet->bDontRelease = true;				//	Needs to wait for an ACK
			OUT_Packets.emplace(UniqueID, Packet);				//	Store this packet until it gets ACK'd
		}

		inline NetPacket_Send*const TryACK(const uintmax_t& UniqueID) override
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
		{
			for (auto& WaitingPackets : OUT_Packets)
			{
				if (WaitingPackets.second->NextTransmit <= Now)
				{
					//
					//	Set our NextTransmit time
					WaitingPackets.second->NextTransmit = Now + std::chrono::milliseconds(300);
					//
					//	Add it into our packet deque
					Packets_.push_back(WaitingPackets.second);
				}
			}
		}
	};
}