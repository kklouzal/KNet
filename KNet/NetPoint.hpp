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

		ZSTD_CCtx* cctx;
		ZSTD_DCtx* dctx;

		enum class SendCompletion : ULONG_PTR {
			SendComplete,
			SendInitiate,
			SendShutdown
		};
		enum class RecvCompletion : ULONG_PTR {
			RecvComplete,
			RecvRelease,
			RecvClientDelete,
			RecvCheckClientTimeouts,
			RecvShutdown
		};
		enum class PointCompletion : ULONG_PTR {
			SendRelease,
			RecvUnread,
			ClientConnected,
			ClientDisconnect,
			ServerUpdate
		};
	public:

		struct Result
		{
			std::deque<NetPacket_Recv*> Packets;
			std::deque<NetClient*> Clients_Connected;
			std::deque<NetClient*> Clients_Disconnected;
		};

		//	SendAddr - Sends packets out from this address
		//	RecvAddr - Receives packets in on this address
		NetPoint(NetAddress* SendAddr, NetAddress* RecvAddr) :
			bRunning(true), SendAddress(SendAddr), RecvAddress(RecvAddr),
			RecvPackets(new NetPool<NetPacket_Recv, ADDR_SIZE + MAX_PACKET_SIZE>(PENDING_RECVS, this)),
			pEntries(new OVERLAPPED_ENTRY[PENDING_SENDS + PENDING_RECVS]), pEntriesCount(0),
			cctx(ZSTD_createCCtx()), dctx(ZSTD_createDCtx())
		{
			//
			//	Create this NetPoints IOCP handle
			PointIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
			if (PointIOCP == nullptr) {
				printf("Create IO Completion Port - Point Error: (%lu)\n", GetLastError());
			}
			//
			//	Create Send/Recv Sockets
			constexpr DWORD flags = WSA_FLAG_REGISTERED_IO;
			_SocketSend = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, flags);
			if (_SocketSend == INVALID_SOCKET) {
				printf("WSA Socket Failed - Code: (%lu)\n", GetLastError());
			}
			_SocketRecv = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, flags);
			if (_SocketRecv == INVALID_SOCKET) {
				printf("WSA Socket Failed - Code: (%lu)\n", GetLastError());
			}
			//
			//	Create Send/Recv Completion Queue
			SendIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
			if (SendIOCP == nullptr) {
				printf("Create IO Completion Port - Send Error: (%lu)\n", GetLastError());
			}
			RecvIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
			if (RecvIOCP == nullptr) {
				printf("Create IO Completion Port - Recv Error: (%lu)\n", GetLastError());
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
			SendQueue = g_RIO.RIOCreateCompletionQueue(PENDING_SENDS + GLOBAL_SENDS, &SendCompletion);
			if (SendQueue == RIO_INVALID_CQ) {
				printf("RIO Create Completion Queue Failed - Send Error: (%lu)\n", GetLastError());
			}
			RecvQueue = g_RIO.RIOCreateCompletionQueue(PENDING_RECVS, &RecvCompletion);
			if (RecvQueue == RIO_INVALID_CQ) {
				printf("RIO Create Completion Queue Failed - Recv Error: (%lu)\n", GetLastError());
			}
			//
			//	Create Send/Recv Request Queue
			SendRequestQueue = g_RIO.RIOCreateRequestQueue(_SocketSend, 0, 1, PENDING_SENDS + GLOBAL_SENDS, 1, SendQueue, SendQueue, nullptr);
			if (SendRequestQueue == RIO_INVALID_RQ) {
				printf("RIO Create Request Queue Failed - Send Error: (%lu)\n", GetLastError());
			}
			RecvRequestQueue = g_RIO.RIOCreateRequestQueue(_SocketRecv, PENDING_RECVS, 1, 0, 1, RecvQueue, RecvQueue, nullptr);
			if (RecvRequestQueue == RIO_INVALID_RQ) {
				printf("RIO Create Request Queue Failed - Recv Error: (%lu)\n", GetLastError());
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
				Packet->Offset += ADDR_SIZE;
				//
				//	Lengths
				Packet->Address->Length = ADDR_SIZE;
				Packet->Length -= ADDR_SIZE;
				//
				if (!g_RIO.RIOReceiveEx(RecvRequestQueue, Packet, 1, NULL, Packet->Address, NULL, 0, 0, Packet)) {
					printf("RIO Receive Failed - Code: (%lu)\n", GetLastError());
				}
			}
			//
			//	Bind Send/Recv Socket
			if (SOCKET_ERROR == bind(_SocketSend, SendAddr->Results->ai_addr, static_cast<int>(SendAddr->Results->ai_addrlen))) {
				printf("Bind Failed - Code: (%lu)\n", GetLastError());
			}
			if (SOCKET_ERROR == bind(_SocketRecv, RecvAddr->Results->ai_addr, static_cast<int>(RecvAddr->Results->ai_addrlen))) {
				printf("Bind Failed - Code: (%lu)\n", GetLastError());
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
			if (!PostQueuedCompletionStatus(SendIOCP, NULL, static_cast<ULONG_PTR>(SendCompletion::SendShutdown), &SendIOCPOverlap)) {
				printf("Post Queued Completion Status - Send Error: %lu\n", GetLastError());
			}
			SendThread->join();
			if (!PostQueuedCompletionStatus(RecvIOCP, NULL, static_cast<ULONG_PTR>(RecvCompletion::RecvShutdown), &RecvIOCPOverlap)) {
				printf("Post Queued Completion Status - Recv Error: %lu\n", GetLastError());
			}
			RecvThread->join();
			//
			//	Cleaup ZSTD
			ZSTD_freeDCtx(dctx);
			ZSTD_freeCCtx(cctx);
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
				printf("Close Socket Failed - Error: (%lu)\n", GetLastError());
			}
			if (SOCKET_ERROR == closesocket(_SocketRecv)) {
				printf("Close Socket Failed - Error: (%lu)\n", GetLastError());
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

		//
		//	Release a client back to the NetPoint for cleanup and deletion.
		void ReleaseClient(NetClient* Client)
		{
			printf("RELEASE FUNC\n");
			KN_CHECK_RESULT(PostQueuedCompletionStatus(RecvIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::RecvCompletion::RecvClientDelete), Client), false);
		}

		//
		//	Check for client timeouts
		//	NOTE:	This operation is performed inside the Receive Thread
		//			And loops through all clients, don't run it too often.
		void CheckForTimeouts()
		{
			KN_CHECK_RESULT(PostQueuedCompletionStatus(RecvIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::RecvCompletion::RecvCheckClientTimeouts), NULL), false);
		}

		void SendPacket(NetPacket_Send* Packet) noexcept {
			if (Packet)
			{
				//
				//	Set our Timestamp
				Packet->SetTimestamp(std::chrono::high_resolution_clock::now().time_since_epoch().count());
				//	Send the packet
				KN_CHECK_RESULT(PostQueuedCompletionStatus(SendIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::SendCompletion::SendInitiate), &Packet->Overlap), false);
			}
		}

		void ReleasePacket(NetPacket_Recv* Packet) noexcept {
			//	Release the packet
			KN_CHECK_RESULT(PostQueuedCompletionStatus(RecvIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::RecvCompletion::RecvRelease), &Packet->Overlap), false);
		}

		//
		//	Returns all packets waiting to be processed
		//	Packets are arranged in the order by which they were received
		//std::pair<std::deque<NetPacket_Recv*>, std::deque<NetClient*>> GetPackets() {
		KNet::NetPoint::Result GetPackets() {
			std::pair<std::deque<NetPacket_Recv*>, std::deque<NetClient*>> _Updates;
			//
			KNet::NetPoint::Result _Result;
			//
			std::deque<NetServer*> NewServers;
			if (KN_CHECK_RESULT2(GetQueuedCompletionStatusEx(PointIOCP, pEntries, PENDING_RECVS, &pEntriesCount, 0, false), false)) {
				//return _Updates;
				return _Result;
			}

			if (pEntriesCount == 0) { /*return _Updates;*/ return _Result; }

			for (unsigned int i = 0; i < pEntriesCount; i++) {
				//
				//	Release Send Packet Operation
				if (pEntries[i].lpCompletionKey == static_cast<ULONG_PTR>(PointCompletion::SendRelease)) {
					NetPacket_Send* Packet = static_cast<NetPacket_Send*>(pEntries[i].lpOverlapped->Pointer);
					KNet::SendPacketPool->ReturnUsedObject(Packet);
				}
				//
				//	Unread Packet Operation
				else if(pEntries[i].lpCompletionKey == static_cast<ULONG_PTR>(PointCompletion::RecvUnread)) {
					//_Updates.first.push_back(static_cast<NetPacket_Recv*>(pEntries[i].lpOverlapped->Pointer));
					_Result.Packets.push_back(static_cast<NetPacket_Recv*>(pEntries[i].lpOverlapped->Pointer));
				}
				//
				//	Client Connected Operation
				else if (pEntries[i].lpCompletionKey == static_cast<ULONG_PTR>(PointCompletion::ClientConnected)) {
					//_Updates.second.push_back(static_cast<NetClient*>(pEntries[i].lpOverlapped));
					_Result.Clients_Connected.push_back(static_cast<NetClient*>(pEntries[i].lpOverlapped));
				}
				//
				//	Client Disconnected Operation
				else if (pEntries[i].lpCompletionKey == static_cast<ULONG_PTR>(PointCompletion::ClientDisconnect)) {
					//_Updates.second.push_back(static_cast<NetClient*>(pEntries[i].lpOverlapped));
					_Result.Clients_Disconnected.push_back(static_cast<NetClient*>(pEntries[i].lpOverlapped));
				}
				//
				//	Server Update Operation
				else if (pEntries[i].lpCompletionKey == static_cast<ULONG_PTR>(PointCompletion::ServerUpdate)) {
					//_Updates.??third??.push_back(static_cast<NetServer*>(pEntries[i].lpOverlapped->Pointer));
				}
			}
			//return _Updates;
			return _Result;
		}

		//
		//	Threadded Send Function
		void Thread_Send() noexcept {
			printf("Send Thread Started\n");
			DWORD numberOfBytes = 0;
			ULONG_PTR completionKey;
			OVERLAPPED* pOverlapped = nullptr;
			RIORESULT Result;
			ULONG numResults;
			while (bRunning.load()) {
				//
				//	Wait until we have a send event
				KN_CHECK_RESULT(GetQueuedCompletionStatus(SendIOCP, &numberOfBytes, &completionKey, &pOverlapped, INFINITE), false);
				//
				//	Send Completed Operation
				if (completionKey == static_cast<ULONG_PTR>(SendCompletion::SendComplete)) {
					//
					//	Dequeue A Send
					numResults = g_RIO.RIODequeueCompletion(SendQueue, &Result, 1);
					//
					//	WARN: should probably handle this..
					//	TODO: abort packet processing when corrupt
					if (KN_CHECK_RESULT2(RIO_CORRUPT_CQ, numResults))
					{
						printf("Corrupt SEND Results!\n");
					}
					if (numResults > 0) {
						//
						//	Cleanup the sent packet
						NetPacket_Send* Packet = reinterpret_cast<NetPacket_Send*>(Result.RequestContext);
						if (Packet->Parent)
						{
							//
							//	Don't release the packet if it needs to wait for an ACK
							if (!Packet->bDontRelease) {
								static_cast<NetClient*>(Packet->Parent)->ReturnPacket(Packet);
							}
						}
						else {
							//
							//	Hand the packet over to the main thread to be stored back in the SendBufferPool
							KN_CHECK_RESULT(PostQueuedCompletionStatus(PointIOCP, NULL, static_cast<ULONG_PTR>(PointCompletion::SendRelease), &Packet->Overlap), false);
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
				else if (completionKey == static_cast<ULONG_PTR>(SendCompletion::SendInitiate)) {
					NetPacket_Send* Packet = static_cast<NetPacket_Send*>(pOverlapped->Pointer);
					//
					//	Compress our packets raw binary data straight into the SendBuffer
					Packet->Compress(cctx);
					//
					//	Send the data to its destination
					g_RIO.RIOSendEx(SendRequestQueue, Packet, 1, NULL, Packet->Address, 0, 0, 0, pOverlapped->Pointer);
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
			RIORESULT Result;
			ULONG numResults;
			while (bRunning.load()) {
				OVERLAPPED* pOverlapped = nullptr;
				//
				//	Wait until we have a receive event
				KN_CHECK_RESULT(GetQueuedCompletionStatus(RecvIOCP, &numberOfBytes, &completionKey, &pOverlapped, INFINITE), false);
				//
				//	Received New Packet Operation
				if (completionKey == static_cast<ULONG_PTR>(RecvCompletion::RecvComplete)) {
					//
					//	Dequeue A Receive
					numResults = g_RIO.RIODequeueCompletion(RecvQueue, &Result, 1);
					//
					//	WARN: should probably handle this..
					//	TODO: abort packet processing when corrupt
					if (KN_CHECK_RESULT2(RIO_CORRUPT_CQ, numResults))
					{
						printf("Corrupt RECV Results!\n");
					}
					if (numResults > 0) {
						//
						//	Grab the packet and decompress the data
						NetPacket_Recv* Packet = reinterpret_cast<NetPacket_Recv*>(Result.RequestContext);
						Packet->Decompress(dctx, Result.BytesTransferred);
						//
						//	Try to read Packet Header
						const PacketID OpID = Packet->GetPID();
						const ClientID ClID = Packet->GetCID();
						//
						//	Grab the source address information
						const SOCKADDR_INET* Source = Packet->GetAddress();
						const std::string IP(inet_ntoa(Source->Ipv4.sin_addr));
						const u_short PORT = ntohs(Source->Ipv4.sin_port);
						const std::string ID(IP + ":" + std::to_string(PORT));
						//
						//	Client logic
						if (ClID == ClientID::Client)
						{
							//
							//	Create a new NetClient if one does not already exist
							if (!Clients.count(ID))
							{
								Clients.emplace(ID, new NetClient(IP, PORT));
								KN_CHECK_RESULT(PostQueuedCompletionStatus(PointIOCP, NULL, static_cast<ULONG_PTR>(PointCompletion::ClientConnected), Clients[ID]), false);
							}
							//
							//	Grab our NetClient object
							NetClient* _Client = Clients[ID];
							_Client->LastPacketTime = std::chrono::high_resolution_clock::now();
							if (OpID == PacketID::Acknowledgement) {
								_Client->ProcessPacket_Acknowledgement(Packet);
								Packet->bRecycle = true;	//	Recycle the incoming ACK packet
							}
							else if (OpID == PacketID::Handshake) {
								SendPacket(_Client->ProcessPacket_Handshake(Packet));
								Packet->bRecycle = true;	//	Recycle the incoming handshake packet
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
								const NetServer* _Server = Servers[ID];
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
							KN_CHECK_RESULT(PostQueuedCompletionStatus(PointIOCP, NULL, static_cast<ULONG_PTR>(PointCompletion::RecvUnread), &Packet->Overlap), false);
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
				else if (completionKey == static_cast<ULONG_PTR>(RecvCompletion::RecvRelease)) {
					NetPacket_Recv* Packet = static_cast<NetPacket_Recv*>(pOverlapped->Pointer);
					//
					//	Queue up a new receive
					KN_CHECK_RESULT(g_RIO.RIOReceiveEx(RecvRequestQueue, Packet, 1, NULL, Packet->Address, NULL, 0, 0, pOverlapped->Pointer), false);
				}
				//
				//	Client Delete Operation
				else if (completionKey == static_cast<ULONG_PTR>(RecvCompletion::RecvClientDelete)) {
					printf("RECV| Delete\n");
					
					//
					//	Formulate the client ID
					//
					// TODO: THIS SHIT RIGHT HERE DONT WORK FOR SOME FRIGGEN REASON.
					//NetClient* _Client = static_cast<NetClient*>(pOverlapped->Pointer);
					//const std::string ID(_Client->_IP_RECV + ":" + std::to_string(_Client->_PORT_RECV));
					const std::string ID2(static_cast<NetClient*>(pOverlapped->Pointer)->_IP_RECV + ":" + std::to_string(static_cast<NetClient*>(pOverlapped->Pointer)->_PORT_SEND));
					//
					//	If they exist
					if (Clients.count(ID2))
					{
						printf("RECV| Exists\n");
						//
						//	Delete and remove them from the Clients container
						delete Clients[ID2];
						Clients.erase(ID2);
					}
				}
				//
				//	Check Client Timeouts Operation
				else if (completionKey == static_cast<ULONG_PTR>(RecvCompletion::RecvCheckClientTimeouts)) {
					std::chrono::high_resolution_clock::time_point TimePoint = std::chrono::high_resolution_clock::now();
					//
					//	Loop through the entire clients container checking their LastPacketReceived time
					for (auto& _Client : Clients)
					{
						if (_Client.second->LastPacketTime + _Client.second->TimeoutPeriod < TimePoint)
						{
							KN_CHECK_RESULT(PostQueuedCompletionStatus(PointIOCP, NULL, static_cast<ULONG_PTR>(PointCompletion::ClientDisconnect), _Client.second), false);
						}
					}
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