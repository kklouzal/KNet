#pragma once

namespace KNet
{
	class Reliable_Ordered_Channel : public Channel
	{
		//
		//	Record the UniqueID of our most recent incoming packet and the UniqueID of our next outgoing packet
		uintmax_t IN_NextID = 1;	//	Next Incoming UniqueID to process
		uintmax_t OUT_NextID = 1;	//	Next Outgoing UniqueID
		std::unordered_map<uintmax_t, NetPacket_Send*> OUT_Packets = {};	//	Unacknowledged outgoing packets
		std::unordered_map<uintmax_t, NetPacket_Recv*> IN_Packets = {};	//	Unprocessed incoming packets

	public:
		inline Reliable_Ordered_Channel(uint8_t OPID) noexcept : Channel(ChannelID::Reliable_Ordered, OPID) {}

		//	Initialize and return a new packet for sending
		inline void StampPacket(NetPacket_Send* Packet) override
		{
			const uintmax_t UniqueID = OUT_NextID++;	//	Store and increment our UniqueID
			Packet->SetUID(UniqueID);					//	Write the UniqueID
			Packet->bDontRelease = true;				//	Needs to wait for an ACK
			OUT_Packets[UniqueID] = Packet;				//	Store this packet until it gets ACK'd
		}

		inline NetPacket_Send* TryACK(const uintmax_t& UniqueID) override
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
		inline std::deque<NetPacket_Recv*> TryReceive(NetPacket_Recv* const Packet, const uintmax_t& UniqueID)
		{
			std::deque<NetPacket_Recv*> PacketBacklog;
			//
			//	Drop packets with a UniqueID lower than our next expected ID
			if (UniqueID < IN_NextID)
			{
				//
				//	Drop this packet
				Packet->bRecycle = true;
			}
			//
			//	Process packets with a UniqueID equal to our next expected ID
			else if (UniqueID == IN_NextID)
			{
				//
				//	Add this packet into our return packets deque
				PacketBacklog.push_back(Packet);
				//
				//	Loop any waiting to process packets
				while (IN_Packets.count(++IN_NextID))
				{
					PacketBacklog.push_back(IN_Packets[IN_NextID]);
					IN_Packets.erase(IN_NextID);
				}
				//
				//	Keep this packet
				Packet->bRecycle = false;
			}
			//
			//	Store packets with a UniqueID greater than our next expected ID
			else if (UniqueID > IN_NextID)
			{
				//
				//	Only store a packet once
				if (!IN_Packets.count(UniqueID))
				{
					printf("Ordered STORE %ju (waiting %ju)\n", UniqueID, IN_NextID);
					IN_Packets[UniqueID] = Packet;
				}
				//
				//	Recycle if already stored
				else {
					Packet->bRecycle = true;
				}
			}
			return PacketBacklog;
		}

		inline void GetUnacknowledgedPackets(std::deque<NetPacket_Send*>& Packets_, const std::chrono::time_point<std::chrono::steady_clock>& Now) override
		{
			/*if (OUT_Packets.size() > 1)
			{
				printf("Ordered Unacknowledged: %zi\n", OUT_Packets.size());
			}*/
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