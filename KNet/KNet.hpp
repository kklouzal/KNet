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
#include <mutex>
#include <atomic>
#include <deque>
#include <string>

#include "lz4.h"

namespace KNet
{
	RIO_EXTENSION_FUNCTION_TABLE g_RIO;
	const DWORD ADDR_SIZE = sizeof(SOCKADDR_INET);
	const DWORD MAX_PACKET_SIZE = 1472;
	const DWORD PENDING_SENDS = 1024;	//	Internal NetPoint send packets
	const DWORD PENDING_RECVS = 1024;	//	Internal NetPoint recv packets
	const DWORD GLOBAL_SENDS = 1024;	//	Global PeerNet send packets
	const DWORD GLOBAL_ADDRS = 1024;	//	Global PeerNet addresses
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
	//
	//	Internal Packet IDs
	enum class PacketID : unsigned char {
		Acknowledgement,
		Handshake
	};
}

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
			printf("\tWSAStartup Error: %i\n", (int)iResult);
		}
		else {
			//	Create a dummy socket long enough to get our RIO Function Table pointer
			SOCKET RioSocket = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, NULL, WSA_FLAG_REGISTERED_IO);
			GUID functionTableID = WSAID_MULTIPLE_RIO;
			DWORD dwBytes = 0;
			if (WSAIoctl(RioSocket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
				&functionTableID,
				sizeof(GUID),
				(void**)&g_RIO,
				sizeof(g_RIO),
				&dwBytes, 0, 0) == SOCKET_ERROR) {
				printf("RIO Failed(%i)\n", WSAGetLastError());
			}
			closesocket(RioSocket);
			SendPacketPool = new NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>(GLOBAL_SENDS);
			AddressPool = new NetPool<NetAddress, ADDR_SIZE>(GLOBAL_ADDRS);
			printf("Initialization Complete\n");
		}
	}

	//	Deinitialize the library
	void Deinitialize()
	{
		printf("Deinitialize KNet\n");
		delete AddressPool;
		delete SendPacketPool;
		WSACleanup();
		printf("Deinitialization Complete\n");
	}
}