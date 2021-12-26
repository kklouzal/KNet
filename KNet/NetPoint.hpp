#pragma once

namespace KNet
{
	class NetPoint
	{
	private:
		std::atomic<bool> bRunning;
		std::thread* SendThread;
		std::thread* RecvThread;

		NetAddress* SendAddress;
		NetAddress* RecvAddress;

		SOCKET _SocketSend;
		SOCKET _SocketRecv;

		RIO_RQ SendRequestQueue;
		RIO_RQ RecvRequestQueue;

		RIO_CQ SendQueue;
		RIO_CQ RecvQueue;

		//	Receive packets for the receive thread
		NetPool<NetPacket_Recv, ADDR_SIZE + MAX_PACKET_SIZE>* RecvPackets;
		//
		std::unordered_map<std::string, NetClient*> Clients;
		std::unordered_map<std::string, NetServer*> Servers;

		OVERLAPPED SendIOCPOverlap = {};
		OVERLAPPED RecvIOCPOverlap = {};
		HANDLE SendIOCP;
		HANDLE RecvIOCP;

		LPOVERLAPPED_ENTRY pEntries;
		ULONG pEntriesCount;
		HANDLE PointIOCP;

		enum class SendCompletion : ULONG_PTR {
			SendComplete,
			SendInitiate,
			SendShutdown
		};
		enum class RecvCompletion : ULONG_PTR {
			RecvComplete,
			RecvRelease,
			RecvShutdown
		};
		enum class PointCompletion : ULONG_PTR {
			SendRelease,
			RecvUnread,
			ClientUpdate,
			ServerUpdate
		};
	public:
		//	SendAddr - Sends packets out from this address
		//	RecvAddr - Receives packets in on this address
		NetPoint(NetAddress* SendAddr, NetAddress* RecvAddr) :
			bRunning(true), SendAddress(SendAddr), RecvAddress(RecvAddr),
			RecvPackets(new NetPool<NetPacket_Recv, ADDR_SIZE + MAX_PACKET_SIZE>(PENDING_RECVS)),
			pEntries(new OVERLAPPED_ENTRY[PENDING_SENDS + PENDING_RECVS]), pEntriesCount(0)
		{
			//
			//	Create this NetPoints IOCP handle
			PointIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
			if (PointIOCP == NULL) {
				printf("Create IO Completion Port - Point Error: (%i)\n", GetLastError());
			}
			//
			//	Create Send/Recv Sockets
			const DWORD flags = WSA_FLAG_REGISTERED_IO;
			_SocketSend = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, flags);
			if (_SocketSend == INVALID_SOCKET) {
				printf("WSA Socket Failed - Code: (%i)\n", GetLastError());
			}
			_SocketRecv = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, flags);
			if (_SocketRecv == INVALID_SOCKET) {
				printf("WSA Socket Failed - Code: (%i)\n", GetLastError());
			}
			//
			//	Create Send/Recv Completion Queue
			SendIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
			if (SendIOCP == NULL) {
				printf("Create IO Completion Port - Send Error: (%i)\n", GetLastError());
			}
			RecvIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
			if (RecvIOCP == NULL) {
				printf("Create IO Completion Port - Recv Error: (%i)\n", GetLastError());
			}
			RIO_NOTIFICATION_COMPLETION SendCompletion = {};
			SendCompletion.Type = RIO_IOCP_COMPLETION;
			SendCompletion.Iocp.IocpHandle = SendIOCP;
			SendCompletion.Iocp.CompletionKey = (void*)SendCompletion::SendComplete;
			SendCompletion.Iocp.Overlapped = &SendIOCPOverlap;
			RIO_NOTIFICATION_COMPLETION RecvCompletion = {};
			RecvCompletion.Type = RIO_IOCP_COMPLETION;
			RecvCompletion.Iocp.IocpHandle = RecvIOCP;
			RecvCompletion.Iocp.CompletionKey = (void*)RecvCompletion::RecvComplete;
			RecvCompletion.Iocp.Overlapped = &RecvIOCPOverlap;
			SendQueue = g_RIO.RIOCreateCompletionQueue(PENDING_SENDS, &SendCompletion);
			if (SendQueue == RIO_INVALID_CQ) {
				printf("RIO Create Completion Queue Failed - Send Error: (%i)\n", GetLastError());
			}
			RecvQueue = g_RIO.RIOCreateCompletionQueue(PENDING_RECVS, &RecvCompletion);
			if (RecvQueue == RIO_INVALID_CQ) {
				printf("RIO Create Completion Queue Failed - Recv Error: (%i)\n", GetLastError());
			}
			//
			//	Create Send/Recv Request Queue
			SendRequestQueue = g_RIO.RIOCreateRequestQueue(_SocketSend, 0, 1, PENDING_SENDS, 1, SendQueue, SendQueue, &SendCompletion);
			if (SendRequestQueue == RIO_INVALID_RQ) {
				printf("RIO Create Request Queue Failed - Send Error: (%i)\n", GetLastError());
			}
			RecvRequestQueue = g_RIO.RIOCreateRequestQueue(_SocketRecv, PENDING_RECVS, 1, 0, 1, RecvQueue, RecvQueue, &RecvCompletion);
			if (RecvRequestQueue == RIO_INVALID_RQ) {
				printf("RIO Create Request Queue Failed - Recv Error: (%i)\n", GetLastError());
			}
			//
			//	Post our receives
			//	Receive packets need their RIO_BUF objects adjusted to account for holding the receive address with the buffer
			for (auto Packet : RecvPackets->GetAllObjects()) {
				//
				//	BufferID
				Packet->Address->BufferId = Packet->BufferId;
				//
				//	Offsets
				Packet->Address->Offset = Packet->Offset;
				Packet->Offset = Packet->Offset + ADDR_SIZE;
				//
				//	Lengths
				Packet->Address->Length = ADDR_SIZE;
				Packet->Length = Packet->Length - ADDR_SIZE;
				//
				if (!g_RIO.RIOReceiveEx(RecvRequestQueue, Packet, 1, NULL, Packet->Address, NULL, 0, 0, Packet)) {
					printf("RIO Receive Failed - Code: (%i)\n", GetLastError());
				}
			}
			//
			//	Bind Send/Recv Socket
			if (SOCKET_ERROR == bind(_SocketSend, SendAddr->Results->ai_addr, (int)SendAddr->Results->ai_addrlen)) {
				printf("Bind Failed - Code: (%i)\n", GetLastError());
			}
			if (SOCKET_ERROR == bind(_SocketRecv, RecvAddr->Results->ai_addr, (int)RecvAddr->Results->ai_addrlen)) {
				printf("Bind Failed - Code: (%i)\n", GetLastError());
			}
			//
			//	Notify RIO that our send thread is ready for processing
			const INT SendNotifyResult = g_RIO.RIONotify(SendQueue);
			if (SendNotifyResult != ERROR_SUCCESS) {
				printf("RIO Notify Error - Send Error: (%i)\n", SendNotifyResult);
			}
			//
			//	Notify RIO that our recv thread is ready for processing
			const INT RecvNotifyResult = g_RIO.RIONotify(RecvQueue);
			if (RecvNotifyResult != ERROR_SUCCESS) {
				printf("RIO Notify Error - Recv Error: (%i)\n", RecvNotifyResult);
			}
			//
			//	Finally startup the processing threads
			SendThread = new std::thread(&NetPoint::Thread_Send, this);
			RecvThread = new std::thread(&NetPoint::Thread_Recv, this);
		}

		~NetPoint()
		{
			printf("Destroy NetPoint\n");
			//
			//	Shutdown Send/Recv threads
			bRunning = false;
			if (!PostQueuedCompletionStatus(SendIOCP, NULL, (ULONG_PTR)SendCompletion::SendShutdown, &SendIOCPOverlap)) {
				printf("Post Queued Completion Status - Send Error: %i\n", GetLastError());
			}
			SendThread->join();
			if (!PostQueuedCompletionStatus(RecvIOCP, NULL, (ULONG_PTR)RecvCompletion::RecvShutdown, &RecvIOCPOverlap)) {
				printf("Post Queued Completion Status - Recv Error: %i\n", GetLastError());
			}
			RecvThread->join();
			//
			//	Cleanup Client/Server Objects
			for (auto& _Client : Clients)
			{
				delete _Client.second;
			}
			for (auto& _Server : Servers)
			{
				delete _Server.second;
			}
			//
			//	Close our sockets
			if (SOCKET_ERROR == closesocket(_SocketSend)) {
				printf("Close Socket Failed - Error: (%i)\n", GetLastError());
			}
			if (SOCKET_ERROR == closesocket(_SocketRecv)) {
				printf("Close Socket Failed - Error: (%i)\n", GetLastError());
			}
			//
			//	Cleanup RIO
			g_RIO.RIOCloseCompletionQueue(SendQueue);
			g_RIO.RIOCloseCompletionQueue(RecvQueue);
			//
			//	Delete buffer allocations
			delete[] pEntries;
			//
			//	Cleanup object pools
			delete RecvPackets;
			//
			//	Close the NetPoints IOCP handle
			CloseHandle(PointIOCP);
			printf("NetPoint Destroyed\n");
		}

		void SendPacket(NetPacket_Send* Packet) {
			if (Packet)
			{
				//	Send the packet
				KN_CHECK_RESULT(PostQueuedCompletionStatus(SendIOCP, NULL, (ULONG_PTR)NetPoint::SendCompletion::SendInitiate, &Packet->Overlap), false);
			}
		}

		void ReleasePacket(NetPacket_Recv* Packet) {
			//	Release the packet
			KN_CHECK_RESULT(PostQueuedCompletionStatus(RecvIOCP, NULL, (ULONG_PTR)NetPoint::RecvCompletion::RecvRelease, &Packet->Overlap), false);
		}

		//
		//	Returns all packets waiting to be processed
		//	Packets are arranged in the order by which they were received
		std::pair<std::deque<NetPacket_Recv*>, std::deque<NetClient*>> GetPackets() {
			std::pair<std::deque<NetPacket_Recv*>, std::deque<NetClient*>> _Updates;
			std::deque<NetServer*> NewServers;
			if (KN_CHECK_RESULT2(GetQueuedCompletionStatusEx(PointIOCP, pEntries, PENDING_RECVS, &pEntriesCount, 0, false), false)) {
				return _Updates;
			}

			if (pEntriesCount == 0) { return _Updates; }

			for (unsigned int i = 0; i < pEntriesCount; i++) {
				//
				//	Release Send Packet Operation
				if (pEntries[i].lpCompletionKey == (ULONG_PTR)PointCompletion::SendRelease) {
					NetPacket_Send* Packet = reinterpret_cast<NetPacket_Send*>(pEntries[i].lpOverlapped->Pointer);
					KNet::SendPacketPool->ReturnUsedObject(Packet);
				}
				//
				//	Unread Packet Operation
				else if(pEntries[i].lpCompletionKey == (ULONG_PTR)PointCompletion::RecvUnread) {
					_Updates.first.push_back(reinterpret_cast<NetPacket_Recv*>(pEntries[i].lpOverlapped->Pointer));
				}
				//
				//	Client Update Operation
				else if (pEntries[i].lpCompletionKey == (ULONG_PTR)PointCompletion::ClientUpdate) {
					_Updates.second.push_back(reinterpret_cast<NetClient*>(pEntries[i].lpOverlapped));
					printf("ClientUpdate\n");
				}
				//
				//	Server Update Operation
				else if (pEntries[i].lpCompletionKey == (ULONG_PTR)PointCompletion::ServerUpdate) {
					//_Updates.??third??.push_back(reinterpret_cast<NetServer*>(pEntries[i].lpOverlapped->Pointer));
				}
			}
			return _Updates;
		}

		//
		//	Threadded Send Function
		void Thread_Send() {
			printf("Send Thread Started\n");
			DWORD numberOfBytes = 0;
			ULONG_PTR completionKey;
			OVERLAPPED* pOverlapped = 0;
			RIORESULT Result;
			ULONG numResults;
			while (bRunning.load()) {
				//
				//	Wait until we have a send event
				KN_CHECK_RESULT(GetQueuedCompletionStatus(SendIOCP, &numberOfBytes, &completionKey, &pOverlapped, INFINITE), false);
				//
				//	Send Completed Operation
				if (completionKey == (ULONG_PTR)SendCompletion::SendComplete) {
					//
					//	Dequeue A Send
					numResults = g_RIO.RIODequeueCompletion(SendQueue, &Result, 1);
					//
					//	WARN: should probably handle this..
					//	TODO: abort packet processing when corrupt
					if (KN_CHECK_RESULT2(RIO_CORRUPT_CQ, numResults))
					{ }
					if (numResults > 0) {
						//
						//	Cleanup the sent packet
						NetPacket_Send* Packet = reinterpret_cast<NetPacket_Send*>(Result.RequestContext);
						if (Packet->bChildPacket)
						{
							//
							//	Don't release the packet if it needs to wait for an ACK
							if (!Packet->bDontRelease) {
								//	WARN: This is a race condition, Send_Thread communicating with Recv_Thread directly..
								//	TODO: Use IOCP to return packets..
								
								((NetClient*)Packet->Child)->ReturnPacket(Packet);
							}
						}
						else {
							//
							//	Hand the packet over to the main thread to be stored back in the SendBufferPool
							KN_CHECK_RESULT(PostQueuedCompletionStatus(PointIOCP, NULL, (ULONG_PTR)PointCompletion::SendRelease, &Packet->Overlap), false);
						}
					}
					else { printf("Dequeued 0 Send Completions\n"); }
					//
					//	Notify RIO that we can process another request
					const INT notifyResult = g_RIO.RIONotify(SendQueue);
					if (notifyResult != ERROR_SUCCESS) {
						printf("RIO Notify Error - Send Error: (%i)\n", notifyResult);
					}
				}
				//
				//	Initiate Send Operation
				else if (completionKey == (ULONG_PTR)SendCompletion::SendInitiate) {
					NetPacket_Send* Packet = reinterpret_cast<NetPacket_Send*>(pOverlapped->Pointer);
					//
					//	Compress our packets raw binary data straight into the SendBuffer
					Packet->Compress();
					//
					//	Send the data to its destination
					g_RIO.RIOSendEx(SendRequestQueue, Packet, 1, NULL, Packet->Address, 0, 0, 0, Packet);
				}
				//
				//	Shutdown Thread Operation
				else { printf("Send Thread Shutdown Operation\n"); }
			}
			printf("Send Thread Ended\n");
		}

		//
		//	Threadded Receive Function
		void Thread_Recv() {
			printf("Recv Thread Started\n");
			DWORD numberOfBytes = 0;
			ULONG_PTR completionKey = 0;
			OVERLAPPED* pOverlapped = 0;
			RIORESULT Result;
			ULONG numResults;
			while (bRunning.load()) {
				//
				//	Wait until we have a receive event
				KN_CHECK_RESULT(GetQueuedCompletionStatus(RecvIOCP, &numberOfBytes, &completionKey, &pOverlapped, INFINITE), false);
				//
				//	Received New Packet Operation
				if (completionKey == (ULONG_PTR)RecvCompletion::RecvComplete) {
					//
					//	Dequeue A Receive
					numResults = g_RIO.RIODequeueCompletion(RecvQueue, &Result, 1);
					//
					//	WARN: should probably handle this..
					//	TODO: abort packet processing when corrupt
					if (KN_CHECK_RESULT2(RIO_CORRUPT_CQ, numResults))
					{ }
					if (numResults > 0) {
						//
						//	Grab the packet and decompress the data
						NetPacket_Recv* Packet = reinterpret_cast<NetPacket_Recv*>(Result.RequestContext);
						Packet->Decompress(Result.BytesTransferred);
						//
						//	Try to read Packet Header
						PacketID OpID = Packet->GetPID();
						ClientID ClID = Packet->GetCID();
						//
						//	Grab the source address information
						SOCKADDR_INET* Source = Packet->GetAddress();
						std::string IP(inet_ntoa(Source->Ipv4.sin_addr));
						u_short PORT = ntohs(Source->Ipv4.sin_port);
						std::string ID(IP + ":" + std::to_string(PORT));
						//
						//	Client logic
						if (ClID == ClientID::Client)
						{
							//
							//	Create a new NetClient if one does not already exist
							if (!Clients.count(ID))
							{
								Clients.emplace(ID, new NetClient(IP, PORT));
								KN_CHECK_RESULT(PostQueuedCompletionStatus(PointIOCP, NULL, (ULONG_PTR)PointCompletion::ClientUpdate, Clients[ID]), false);
							}
							//
							//	Grab our NetClient object
							NetClient* _Client = Clients[ID];
							if (OpID == PacketID::Acknowledgement) {
								_Client->ProcessPacket_Acknowledgement(Packet);
								Packet->bRecycle = true;
							}
							else if (OpID == PacketID::Handshake) {
								SendPacket(_Client->ProcessPacket_Handshake(Packet));
								Packet->bRecycle = true;
							}
							else if (OpID == PacketID::Data) {
								SendPacket(_Client->ProcessPacket_Data(Packet));
							}
						}
						//
						//	TODO: Server logic
						else if (ClID == ClientID::Server)
						{
							if (Servers.count(ID))
							{
								NetServer* _Server = Servers[ID];
							}
							else {
								Servers[ID] = new NetServer(IP, PORT);
								printf("\tNew Server\n");
							}
						}
						//
						//	OutOfBand logic
						else if (ClID == ClientID::OutOfBand) {
							//
							//	Hand the packet over to the main thread for user processing
							KN_CHECK_RESULT(PostQueuedCompletionStatus(PointIOCP, NULL, (ULONG_PTR)PointCompletion::RecvUnread, &Packet->Overlap), false);
						}
						//
						//	Immediately recycle the packet by using it to queue up a new receive operation
						if (Packet->bRecycle) {
							KN_CHECK_RESULT(g_RIO.RIOReceiveEx(RecvRequestQueue, Packet, 1, NULL, Packet->Address, NULL, 0, 0, Packet), false);
						}
					}
					else { printf("Dequeued 0 Recv Completions\n"); }
					//
					//	Notify RIO that we can process another request
					const INT notifyResult = g_RIO.RIONotify(RecvQueue);
					if (notifyResult != ERROR_SUCCESS) {
						printf("RIO Notify Error - Recv Error: (%i)\n", notifyResult);
					}
				}
				//
				//	Release Recv Packet Operation
				else if (completionKey == (ULONG_PTR)RecvCompletion::RecvRelease) {
					NetPacket_Recv* Packet = reinterpret_cast<NetPacket_Recv*>(pOverlapped->Pointer);
					//
					//	Queue up a new receive
					KN_CHECK_RESULT(g_RIO.RIOReceiveEx(RecvRequestQueue, Packet, 1, NULL, Packet->Address, NULL, 0, 0, Packet), false);
				}
				//
				//	Shutdown Thread Operation
				else {
					printf("Recv Thread Shutdown Operation\n");
				}
			}
			printf("Recv Thread Ended\n");
		}
	};
}