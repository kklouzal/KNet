#pragma once

namespace KNet
{
	class Reliable_Any_Channel : public Channel
	{
		//
		//	Record the UniqueID of our next outgoing packet
		uintmax_t OUT_NextID = 1;	//	Outgoing UniqueID
		std::unordered_map<uintmax_t, NetPacket_Send*> OUT_Packets = {};	//	Unacknowledged outgoing packets

	public:
		inline Reliable_Any_Channel(uint8_t OPID) noexcept : Channel(ChannelID::Reliable_Any, OPID) {}

		//	Initialize and return a new packet for sending
		inline void StampPacket(NetPacket_Send* Packet)
		{
			const uintmax_t UniqueID = OUT_NextID++;	//	Store and increment our UniqueID
			Packet->SetUID(UniqueID);					//	Write the UniqueID
			Packet->bDontRelease = true;				//	Needs to wait for an ACK
			OUT_Packets[UniqueID] = Packet;				//	Store this packet until it gets ACK'd
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

		inline std::deque<NetPacket_Send*> GetUnacknowledgedPackets(std::chrono::time_point<std::chrono::steady_clock> TimeThreshold)
		{
			uintmax_t TimeThreshold_ = (TimeThreshold - std::chrono::milliseconds(300)).time_since_epoch().count();
			uintmax_t NewTime_ = TimeThreshold.time_since_epoch().count();
			std::deque<NetPacket_Send*> Packets;
			for (auto& WaitingPackets : OUT_Packets)
			{
				if (WaitingPackets.second->GetTimestamp() <= TimeThreshold_)
				{
					//
					//	Reset our timestamp
					WaitingPackets.second->SetTimestamp(NewTime_);
					//
					//	Add it into our packet deque
					Packets.push_back(WaitingPackets.second);
				}
			}
			return Packets;
		}
	};
}