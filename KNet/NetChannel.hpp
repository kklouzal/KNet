#pragma once

namespace KNet
{
	class Channel
	{
		ChannelID Channel_ID;
		uint8_t Operation_ID;
	public:
		Channel(ChannelID Channel_ID, uint8_t Operation_ID) :
			Channel_ID(Channel_ID), Operation_ID(Operation_ID)
		{}

		virtual ~Channel()
		{}

		const ChannelID GetChannelID() const
		{
			return Channel_ID;
		}

		const uint8_t GetOperationID() const
		{
			return Operation_ID;
		}

		virtual NetPacket_Send* TryACK(const uintmax_t& UniqueID) { return nullptr; }
		virtual void StampPacket(NetPacket_Send* Packet) = 0;
		virtual std::deque<NetPacket_Send*> GetUnacknowledgedPackets(std::chrono::time_point<std::chrono::steady_clock> TimeThreshold) { return {}; }
	};
}

#include "NetChannel_UnreliableAny.hpp"
#include "NetChannel_UnreliableLatest.hpp"
#include "NetChannel_ReliableAny.hpp"
#include "NetChannel_ReliableLatest.hpp"
#include "NetChannel_Ordered.hpp"