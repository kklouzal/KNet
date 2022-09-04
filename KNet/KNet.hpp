#pragma once

//	Winsock2 Headers
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <MSWSock.h>
#pragma comment(lib, "ws2_32.lib")

//	STL Headers
#include <thread>
#include <atomic>
#include <chrono>
#include <deque>
#include <string>
#include <unordered_map>

#include "zstd.h"

#include "ErrorHandling.hpp"

namespace KNet
{
	RIO_EXTENSION_FUNCTION_TABLE g_RIO;
	constexpr DWORD ADDR_SIZE = sizeof(SOCKADDR_INET);
	constexpr DWORD MAX_PACKET_SIZE = 1472;
	constexpr DWORD PENDING_SENDS = 10240;	//	Internal NetPoint send packets
	constexpr DWORD PENDING_RECVS = 10240;	//	Internal NetPoint recv packets
	constexpr DWORD GLOBAL_SENDS = 64;	//	Global PeerNet send packets
	constexpr DWORD GLOBAL_ADDRS = 1024;	//	Global PeerNet addresses
	//
	//	Internal Packet IDs
	enum class PacketID : uint8_t {
		Acknowledgement,
		Handshake,
		Data
	};
	enum class ClientID : uint8_t {
		OutOfBand,
		Server,
		Client
	};
	enum class ChannelID : uint8_t {
		//
		//	Unreliable_Any:
		//	No guarentee all packets will be received
		//	Packets may arrive out of order
		//	All received packets will be processed
		Unreliable_Any,
		//
		//	Unreliable_Latest:
		//	No guarentee all packets will be received
		//	Packets may arrive out of order
		//	Out of order packets received with an old UniqueID are dropped
		Unreliable_Latest,
		//
		//	Reliable_Any:
		//	Guarentees all packets will be received
		//	Packets may arrive out of order
		//	All received packets will be processed
		Reliable_Any,
		//
		//	Reliable_Latest:
		//	Guarentees all packets will be received
		//	Packets may arrive out of order
		//	Out of order packets received with an old UniqueID are dropped
		Reliable_Latest,
		//
		//	Reliable_Ordered:
		//	Guarentees all packets will be received and processed in the order they were sent
		Reliable_Ordered
	};
}

#include "NetAddress.hpp"
#include "NetPacket.hpp"
#include "NetPool.hpp"

namespace KNet
{
	//
	//	Packets hold their data along with an address [(Address)+(Data)]=MaxSize
	NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>* SendPacketPool = nullptr;
	//
	NetPool<NetAddress, ADDR_SIZE>* AddressPool = nullptr;
	class NetPoint;
}
//
#include "NetChannel.hpp"
//
#include "NetClient.hpp"
#include "NetServer.hpp"
//
#include "NetPoint.hpp"

namespace KNet
{
	//
	//	Initialize the library
	void Initialize()
	{
		printf("Initialize KNet\n");
		WSADATA wsadata;
		const size_t iResult = WSAStartup(MAKEWORD(2, 2), &wsadata);
		if (iResult != 0) {
			printf("\tWSAStartup Error: %zu\n", iResult);
		}
		else {
			//	Create a dummy socket long enough to get our RIO Function Table pointer
			const SOCKET RioSocket = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, NULL, WSA_FLAG_REGISTERED_IO);
			GUID functionTableID = WSAID_MULTIPLE_RIO;
			DWORD dwBytes = 0;
			KN_CHECK_RESULT(WSAIoctl(RioSocket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
				&functionTableID,
				sizeof(GUID),
				(void**)&g_RIO,
				sizeof(g_RIO),
				&dwBytes, 0, 0), SOCKET_ERROR);
			closesocket(RioSocket);
			SendPacketPool = new NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>(GLOBAL_SENDS, nullptr);
			AddressPool = new NetPool<NetAddress, ADDR_SIZE>(GLOBAL_ADDRS, nullptr);
			printf("Initialization Complete\n");
		}
	}

	//	Deinitialize the library
	void Deinitialize() noexcept
	{
		printf("Deinitialize KNet\n");
		delete AddressPool;
		delete SendPacketPool;
		WSACleanup();
		printf("Deinitialization Complete\n");
	}
}