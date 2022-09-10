#pragma once

namespace KNet
{
	class NetPoint
	{
	private:
		std::atomic<bool> bRunning;
		std::thread* SendThread;
		std::thread* RecvThread;
		std::thread* ProcThread;

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
		NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>* ACKPackets;
		//
		std::unordered_map<std::string, NetClient*> Clients;
		std::unordered_map<std::string, NetServer*> Servers;

		OVERLAPPED SendIOCPOverlap = {};
		OVERLAPPED RecvIOCPOverlap = {};
		HANDLE SendIOCP;
		HANDLE RecvIOCP;
		HANDLE ProcIOCP;

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
			RecvShutdown
		};
		enum class ProcCompletion : ULONG_PTR {
			ProcSend,
			ProcRecv,
			ProcTimeouts,
			ProcReleaseClient,
			ProcReleaseACK,
			ProcShutdown
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
			ACKPackets(new NetPool<NetPacket_Send, ADDR_SIZE + MAX_PACKET_SIZE>(POINT_ACKS, this)),
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
			ProcIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
			if (ProcIOCP == nullptr) {
				printf("Create IO Completion Port - Proc Error: (%lu)\n", GetLastError());
			}
			//
			//
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
			for (auto& Packet : RecvPackets->GetAllObjects()) {
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
			ProcThread = new std::thread(&NetPoint::Thread_Proc, this);
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
			if (!PostQueuedCompletionStatus(ProcIOCP, NULL, static_cast<ULONG_PTR>(ProcCompletion::ProcShutdown), nullptr)) {
				printf("Post Queued Completion Status - Recv Error: %lu\n", GetLastError());
			}
			ProcThread->join();
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
		void ReleaseClient(NetClient* Client) noexcept
		{
			KN_CHECK_RESULT(PostQueuedCompletionStatus(ProcIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::ProcCompletion::ProcReleaseClient), Client), false);
		}

		//
		//	Check for client timeouts
		void CheckForTimeouts() noexcept
		{
			KN_CHECK_RESULT(PostQueuedCompletionStatus(ProcIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::ProcCompletion::ProcTimeouts), nullptr), false);
		}

		void SendPacket(NetPacket_Send* Packet) noexcept {
			if (Packet)	//	why would this be null???!
			{
				//
				//	Set our Timestamp
				const std::chrono::time_point<std::chrono::steady_clock> Now = std::chrono::high_resolution_clock::now();
				Packet->NextTransmit = Now + std::chrono::milliseconds(300);
				Packet->SetTimestamp(Now.time_since_epoch().count());
				//
				//	Send the packet
				if (Packet->GetCID() == ClientID::Client)
				{
					//
					//	Proc thread, stamp the packet
					KN_CHECK_RESULT(PostQueuedCompletionStatus(ProcIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::ProcCompletion::ProcSend), Packet->Overlap), false);
				}
				else if (Packet->GetCID() == ClientID::OutOfBand)
				{
					//
					//	Send thread, immediately send the packet
					KN_CHECK_RESULT(PostQueuedCompletionStatus(SendIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::SendCompletion::SendInitiate), Packet->Overlap), false);
				}
			}
		}

		void ReleasePacket(NetPacket_Recv* Packet) noexcept {
			//	Release the packet
			KN_CHECK_RESULT(PostQueuedCompletionStatus(RecvIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::RecvCompletion::RecvRelease), Packet->Overlap), false);
		}

		//
		//	Returns all packets waiting to be processed
		//	Packets are arranged in the order by which they were received
		//std::pair<std::deque<NetPacket_Recv*>, std::deque<NetClient*>> GetPackets() {
		KNet::NetPoint::Result GetPackets() {
			//
			KNet::NetPoint::Result _Result;
			//
			if (KN_CHECK_RESULT2(GetQueuedCompletionStatusEx(PointIOCP, pEntries, PENDING_RECVS, &pEntriesCount, 0, false), false)) {
				//return _Updates;
				return _Result;
			}

			if (pEntriesCount == 0) { return _Result; }

			for (unsigned int i = 0; i < pEntriesCount; i++) {
				switch (pEntries[i].lpCompletionKey)
				{
					//
					//	Release Send Packet Operation
					case static_cast<ULONG_PTR>(PointCompletion::SendRelease):
					{
						NetPacket_Send* Packet = static_cast<NetPacket_Send*>(pEntries[i].lpOverlapped->Pointer);
						KNet::SendPacketPool->ReturnUsedObject(Packet);
					}
					break;
					//
					//	Unread Packet Operation
					case static_cast<ULONG_PTR>(PointCompletion::RecvUnread):
					{
						//_Updates.first.push_back(static_cast<NetPacket_Recv*>(pEntries[i].lpOverlapped->Pointer));
						_Result.Packets.push_back(static_cast<NetPacket_Recv*>(pEntries[i].lpOverlapped->Pointer));
					}
					break;
					//
					//	Client Connected Operation
					case static_cast<ULONG_PTR>(PointCompletion::ClientConnected):
					{
						//_Updates.second.push_back(static_cast<NetClient*>(pEntries[i].lpOverlapped));
						_Result.Clients_Connected.push_back(static_cast<NetClient*>(pEntries[i].lpOverlapped));
					}
					break;
					//
					//	Client Disconnected Operation
					case static_cast<ULONG_PTR>(PointCompletion::ClientDisconnect):
					{
						//_Updates.second.push_back(static_cast<NetClient*>(pEntries[i].lpOverlapped));
						_Result.Clients_Disconnected.push_back(static_cast<NetClient*>(pEntries[i].lpOverlapped));
					}
					break;
					//
					//	Server Update Operation
					case static_cast<ULONG_PTR>(PointCompletion::ServerUpdate):
					{
						//_Updates.??third??.push_back(static_cast<NetServer*>(pEntries[i].lpOverlapped->Pointer));
					}
					break;
				}
			}
			return _Result;
		}

		void Thread_Proc()
		{
			printf("Proc Thread Started\n");
			DWORD numberOfBytes = 0;
			ULONG_PTR completionKey = 0;
			OVERLAPPED* pOverlapped = nullptr;
			while (bRunning.load())
			{
				//
				//	Wait until we have a proc event
				KN_CHECK_RESULT(GetQueuedCompletionStatus(ProcIOCP, &numberOfBytes, &completionKey, &pOverlapped, INFINITE), false);
				switch (completionKey)
				{
					//
					//	Process Send Packet Operation
					case static_cast<ULONG_PTR>(ProcCompletion::ProcSend):
					{
						NetPacket_Send* Packet = static_cast<NetPacket_Send*>(pOverlapped->Pointer);
						std::string CLID = Packet->GetClientID();
						if (Clients.count(CLID))
						{
							NetClient* Client = Clients[CLID];
							Client->Net_Channels[Packet->GetOID()]->StampPacket(Packet);
						}
						//
						//	No client, initial handshake/ack packet.
						KN_CHECK_RESULT(PostQueuedCompletionStatus(SendIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::SendCompletion::SendInitiate), pOverlapped), false);
					}
					break;
					//
					//	Process Receive Packet Operation
					case static_cast<ULONG_PTR>(ProcCompletion::ProcRecv):
					{
						NetPacket_Recv* Packet = static_cast<NetPacket_Recv*>(pOverlapped->Pointer);
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
						Packet->SetClientID(ID);
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
							//
							//	Reset our clients LastPacketTime
							_Client->LastPacketTime = std::chrono::high_resolution_clock::now();
							//
							//	Handle packet per ID
							switch (OpID)
							{
								case PacketID::Acknowledgement:
								{
									_Client->ProcessPacket_Acknowledgement(Packet);
									//
									//	Immediately recycle acknowledgements
									KN_CHECK_RESULT(PostQueuedCompletionStatus(RecvIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::RecvCompletion::RecvRelease), pOverlapped), false);
								}
								break;
								case PacketID::Handshake:
								{
									//
									//	Formulate an acknowledgement
									NetPacket_Send* ACK = ACKPackets->GetFreeObject();
									if (ACK)
									{
										ACK->AddDestination(_Client->_ADDR_RECV);
										ACK->SetPID(PacketID::Acknowledgement);
										ACK->SetCID(ClientID::Client);
										ACK->SetTimestamp(std::chrono::high_resolution_clock::now().time_since_epoch().count());
										ACK->write<PacketID>(PacketID::Handshake);
										//
										//	Immediately send the ACK
										KN_CHECK_RESULT(PostQueuedCompletionStatus(SendIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::SendCompletion::SendInitiate), ACK->Overlap), false);
									}
									else { printf("[POINT:PROC HANDSHAKE] ERROR: No Free ACK Available!\n"); }
									//_Client->ProcessPacket_Handshake(Packet);
									//
									//	Immediately recycle handshakes
									KN_CHECK_RESULT(PostQueuedCompletionStatus(RecvIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::RecvCompletion::RecvRelease), pOverlapped), false);
								}
								break;
								case PacketID::Data:
								{
									//
									//	Formulate an acknowledgement
									NetPacket_Send* ACK = ACKPackets->GetFreeObject();
									//printf("FREE ACKS: %zi\n", ACKPackets->Size());
									if (ACK)
									{
										ACK->AddDestination(_Client->_ADDR_RECV);
										ACK->SetPID(PacketID::Acknowledgement);
										ACK->SetCID(ClientID::Client);
										ACK->SetTimestamp(std::chrono::high_resolution_clock::now().time_since_epoch().count());
										//
										//	Values to acknowledge
										ACK->SetOID(Packet->GetOID());
										ACK->SetUID(Packet->GetUID());
										ACK->write<PacketID>(OpID);
										//
										//	Immediately send the ACK
										KN_CHECK_RESULT(PostQueuedCompletionStatus(SendIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::SendCompletion::SendInitiate), ACK->Overlap), false);
									}
									else { printf("[POINT:PROC DATA] (UID: %ju) ERROR: No Free ACK Available!\n", Packet->GetUID()); }
									//
									//	Process the packet
									_Client->ProcessPacket_Data(Packet);
									//
									//	Recycle marked packets
									if (Packet->bRecycle)
									{
										KN_CHECK_RESULT(PostQueuedCompletionStatus(RecvIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::RecvCompletion::RecvRelease), pOverlapped), false);
									}
								}
								break;
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
							KN_CHECK_RESULT(PostQueuedCompletionStatus(PointIOCP, NULL, static_cast<ULONG_PTR>(PointCompletion::RecvUnread), pOverlapped), false);
						}
					}
					break;
					//
					//	Check Client Timeouts Operation
					case static_cast<ULONG_PTR>(ProcCompletion::ProcTimeouts):
					{
						//
						//	Loop through the entire clients container checking their LastPacketReceived time
						for (auto& _Client : Clients)
						{
							const std::chrono::steady_clock::time_point Now = std::chrono::steady_clock::now();
							//
							//	Timed-Out Clients
							if (_Client.second->LastPacketTime + _Client.second->TimeoutPeriod <= Now)
							{
								KN_CHECK_RESULT(PostQueuedCompletionStatus(PointIOCP, NULL, static_cast<ULONG_PTR>(PointCompletion::ClientDisconnect), _Client.second), false);
							}
							//
							//	Unacknowledged Packets
							if (_Client.second->LastResendTime + _Client.second->ResendPeriod <= Now)
							{
								std::deque<NetPacket_Send*> Packets_;
								//
								//	Check each channel
								for (auto& Channel_ : _Client.second->Net_Channels)
								{
									Channel_.second->GetUnacknowledgedPackets(Packets_, Now);
								}
								//
								//	Resend Unacknowledged Packets
								for (auto& Packet_ : Packets_)
								{
									KN_CHECK_RESULT(PostQueuedCompletionStatus(SendIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::SendCompletion::SendInitiate), Packet_->Overlap), false);
									printf("RESEND PACKET OpID: %i UID: %ju (ACKs Remaining: %zi)\n", Packet_->GetOID(), Packet_->GetUID(), ACKPackets->Size());
								}
							}
						}
					}
					break;
					//
					//	Release Client Operation
					case static_cast<ULONG_PTR>(ProcCompletion::ProcReleaseClient):
					{
						NetClient* Client = static_cast<NetClient*>(pOverlapped->Pointer);
						//
						//	Formulate the client ID
						const std::string Client_ID = Client->GetClientID();
						//
						//	If they exist
						if (Clients.count(Client_ID))
						{
							//
							//	Delete and remove them from the Clients container
							delete Clients[Client_ID];
							Clients.erase(Client_ID);
						}
					}
					break;
					//
					//	Release ACK Operation
					case static_cast<ULONG_PTR>(ProcCompletion::ProcReleaseACK):
					{
						ACKPackets->ReturnUsedObject(static_cast<NetPacket_Send*>(pOverlapped->Pointer));
						//printf("ACKs Remaining %zi\n", ACKPackets->Size());
					}
					break;
				}
			}
			printf("Proc Thread Ended\n");
		}

		//
		//	Threadded Send Function
		void Thread_Send() noexcept {
			printf("Send Thread Started\n");
			DWORD numberOfBytes = 0;
			ULONG_PTR completionKey = 0;
			OVERLAPPED* pOverlapped = nullptr;
			RIORESULT Result = {};
			ULONG numResults = 0;
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
						if (Packet->GetPID() == PacketID::Acknowledgement)
						{
							KN_CHECK_RESULT(PostQueuedCompletionStatus(ProcIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::ProcCompletion::ProcReleaseACK), Packet->Overlap), false);
						}
						else
						{
							//	Parent == Client Owned
							if (Packet->Parent)
							{
								//
								//	Don't release the packet if it needs to wait for an ACK
								if (!Packet->bDontRelease) {
									static_cast<NetClient*>(Packet->Parent)->ReturnPacket(Packet);
								}
							}
							//	!Parent == Globally Owned
							else {
								//
								//	Hand the packet over to the main thread to be stored back in the SendBufferPool
								KN_CHECK_RESULT(PostQueuedCompletionStatus(PointIOCP, NULL, static_cast<ULONG_PTR>(PointCompletion::SendRelease), Packet->Overlap), false);
							}
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
		void Thread_Recv() noexcept {
			printf("Recv Thread Started\n");
			DWORD numberOfBytes = 0;
			ULONG_PTR completionKey = 0;
			OVERLAPPED* pOverlapped = nullptr;
			RIORESULT Result = {};
			ULONG numResults = 0;
			while (bRunning.load()) {
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
						//	Send to proc thread for processing
						KN_CHECK_RESULT(PostQueuedCompletionStatus(ProcIOCP, NULL, static_cast<ULONG_PTR>(NetPoint::ProcCompletion::ProcRecv), Packet->Overlap), false);
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
				//	Shutdown Thread Operation
				else {
					printf("Recv Thread Shutdown Operation\n");
				}
			}
			printf("Recv Thread Ended\n");
		}
	};
}