#pragma once

namespace KNet
{
	struct NetAddress : public RIO_BUF
	{
	public:
		alignas(alignof(std::max_align_t)) char* const DataBuffer;
		std::string Address;
		unsigned int Port;

	public:
		addrinfo* Results = {};
		void* Parent = nullptr;
		unsigned int InternalID;
		uintmax_t InternalUniqueID;

		NetAddress(char* const Buffer)
			: DataBuffer(Buffer), Address(), Port(0), RIO_BUF()
		{}

		~NetAddress()
		{
			freeaddrinfo(Results);
		}

		//	Resolve this NetAddress to a specific IP address and port number
		void Resolve(std::string StrHost, const unsigned int StrPort)
		{
			//	Hint for what information we need getaddrinfo to return (AF_INET/AF_INET6)
			addrinfo Hint = {};
			Hint.ai_family = AF_INET;
			//	Resolve the End Hosts addrinfo
			KN_CHECK_RESULT((getaddrinfo(StrHost.c_str(), std::to_string(StrPort).c_str(), &Hint, &Results)), (INT)true);

			//	Create our formatted address
			if (Results->ai_family == AF_INET)
			{
				char* const ResolvedIP = new char[16];
				inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(Results->ai_addr)->sin_addr, ResolvedIP, 16);
				Address = std::string(ResolvedIP) + std::string(":") + std::to_string(StrPort);
				delete[] ResolvedIP;
				printf("NetAddress Resolution %s\n", Address.c_str());
			}

			std::memcpy(&DataBuffer[(size_t)Offset], Results->ai_addr, sizeof(SOCKADDR_INET));
		}
	};
}