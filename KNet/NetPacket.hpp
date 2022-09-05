#pragma once

namespace KNet {
	//
	//	Send Packet
	class NetPacket_Send : public RIO_BUF {
		size_t m_StartPos;
	public:
		OVERLAPPED Overlap;
		alignas(alignof(std::max_align_t)) char* const DataBuffer;
		alignas(alignof(std::max_align_t)) char* const BinaryData;
		size_t m_write;
		//
		//	Packet Header
		PacketID* const PID;	//	PacketID
		ClientID* const CID;	//	ClientID
		uint8_t* const OID;		//	OperationID
		uintmax_t* const UID;	//	UniqueID
		uintmax_t* const Timestamp;

	public:
		PRIO_BUF Address = nullptr;
		void* Parent = nullptr;
		bool bDontRelease;

		NetPacket_Send(char* const Buffer) :
			RIO_BUF(),
			Overlap(OVERLAPPED()),
			DataBuffer(Buffer),
			BinaryData(new char[MAX_PACKET_SIZE]),
			//
			//	Map our packet header variables
			PID((PacketID*)&BinaryData[0]),
			CID((ClientID*)&BinaryData[sizeof(PacketID)]),
			OID((uint8_t*)&BinaryData[sizeof(PacketID) + sizeof(ClientID)]),
			UID((uintmax_t*)&BinaryData[sizeof(PacketID) + sizeof(ClientID) + sizeof(uint8_t)]),
			Timestamp((uintmax_t*)&BinaryData[sizeof(PacketID) + sizeof(ClientID) + sizeof(uint8_t) + sizeof(uintmax_t)]),
			//
			//	Offset the m_write position by the size of our header
			m_write(sizeof(PacketID) + sizeof(ClientID) + sizeof(uint8_t) + sizeof(uintmax_t) + sizeof(uintmax_t)),
			m_StartPos(sizeof(PacketID) + sizeof(ClientID) + sizeof(uint8_t) + sizeof(uintmax_t) + sizeof(uintmax_t)),
			//
			bDontRelease(false)
		{
			Overlap.Pointer = this;
		}

		~NetPacket_Send() {
			delete[] BinaryData;
		}

		inline void Compress(ZSTD_CCtx* cctx) noexcept
		{
			//memcpy(&DataBuffer[Offset], BinaryData, m_write);
			//if (!LZ4_compress_default(&BinaryData[0], &DataBuffer[Offset], (int)m_write, MAX_PACKET_SIZE)) {
			//	printf("Packet Compression Failed\n");
			//}

			ZSTD_compressCCtx(cctx, &DataBuffer[Offset], MAX_PACKET_SIZE, BinaryData, m_write, 1);
			m_write = m_StartPos;
		}

		//	TODO: Handle multiple destinations using vector of addresses:
		//		  Add Address into vector of destination addresses.
		void AddDestination(PRIO_BUF Addr) noexcept {
			Address = Addr;
		}

		void SetPID(PacketID ID) noexcept
		{
			std::memcpy(PID, &ID, sizeof(PacketID));
		}

		PacketID GetPID() noexcept
		{
			return *PID;
		}

		void SetCID(ClientID ID) noexcept
		{
			std::memcpy(CID, &ID, sizeof(ClientID));
		}

		ClientID GetCID() noexcept
		{
			return *CID;
		}

		void SetOID(uint8_t ID) noexcept
		{
			std::memcpy(OID, &ID, sizeof(uint8_t));
		}

		uint8_t GetOID() noexcept
		{
			return *OID;
		}

		void SetUID(uintmax_t ID) noexcept
		{
			std::memcpy(UID, &ID, sizeof(uintmax_t));
		}

		uintmax_t GetUID() noexcept
		{
			return *UID;
		}

		void SetTimestamp(uintmax_t TS) noexcept
		{
			std::memcpy(Timestamp, &TS, sizeof(uintmax_t));
		}

		uintmax_t GetTimestamp() noexcept
		{
			return *Timestamp;
		}

		//
		//	Writes the next value in our packet
		//	Writes MUST be read in the same order they were written!
		template <typename T> const bool write(T value) noexcept {
			//static_assert(std::is_trivial_v<T>);
			//
			//	Get the size of this write
			constexpr std::size_t bytes = sizeof(T);
			//
			//	Ensure we don't write past the end of our data buffer
			if (m_write + bytes >= MAX_PACKET_SIZE) { return false; }
			//
			//	Copy the data from our variable to the data buffer
			std::memcpy(&BinaryData[m_write], &value, bytes);
			//
			//	Advance our write position
			m_write += bytes;
			return true;
		}

		//
		//	Specialized
		template <> const bool write(const char* value) noexcept {
			const size_t Len = strlen(value);
			//
			//	Get the size of this write
			constexpr std::size_t bytes = sizeof(size_t);
			//
			//	Ensure we don't write past the end of our data buffer
			if (m_write + bytes + Len >= MAX_PACKET_SIZE) { return false; }
			//
			//	Copy the data from our variable to the data buffer
			std::memcpy(&BinaryData[m_write], &Len, bytes);
			//
			//	Advance our write position
			m_write += bytes;
			//
			//	Write the individual characters from our char buffer
			strncpy(&BinaryData[m_write], value, Len);
			//
			//	Advance our write position
			m_write += Len;
			return true;
		}
	};
	//
	//	Recv Packet
	class NetPacket_Recv : public RIO_BUF {
		size_t m_StartPos;
	public:
		OVERLAPPED Overlap;
		alignas(alignof(std::max_align_t)) char* const DataBuffer;
		alignas(alignof(std::max_align_t)) char* const BinaryData;
		size_t m_read;
		bool bRecycle;
		//
		//	Packet Header
		PacketID* const PID;
		ClientID* const CID;
		uint8_t* const OID;
		uintmax_t* const UID;
		uintmax_t* const Timestamp;

	public:
		PRIO_BUF Address = nullptr;
		void* Parent = nullptr;

		NetPacket_Recv(char* const Buffer) :
			RIO_BUF(),
			Overlap(OVERLAPPED()),
			DataBuffer(Buffer),
			Address(new RIO_BUF),
			BinaryData(new char[MAX_PACKET_SIZE]),
			//
			//	Map our packet header variables
			PID((PacketID*)&BinaryData[0]),
			CID((ClientID*)&BinaryData[sizeof(PacketID)]),
			OID((uint8_t*)&BinaryData[sizeof(PacketID) + sizeof(ClientID)]),
			UID((uintmax_t*)&BinaryData[sizeof(PacketID) + sizeof(ClientID) + sizeof(uint8_t)]),
			Timestamp((uintmax_t*)&BinaryData[sizeof(PacketID) + sizeof(ClientID) + sizeof(uint8_t) + sizeof(uintmax_t)]),
			//
			//	Offset the m_read position by the size of our header
			m_read(sizeof(PacketID) + sizeof(ClientID) + sizeof(uint8_t) + sizeof(uintmax_t) + sizeof(uintmax_t)),
			m_StartPos(sizeof(PacketID) + sizeof(ClientID) + sizeof(uint8_t) + sizeof(uintmax_t) + sizeof(uintmax_t)),
			//
			bRecycle(false)
		{
			Overlap.Pointer = this;
		}

		~NetPacket_Recv() {
			delete Address;
			delete[] BinaryData;
		}

		//
		//	Decompress our raw incoming data
		//	Also reset the packet to it's initial state
		inline void Decompress(ZSTD_DCtx* dctx, const ULONG Size) noexcept
		{
			//memcpy(&BinaryData[0], &DataBuffer[Offset], Size);
			//if (LZ4_decompress_safe(&DataBuffer[Offset], &BinaryData[0], Size, MAX_PACKET_SIZE) < 0)
			//{
				//printf("DECOMPRESS ERROR\n");
			//}
			ZSTD_decompressDCtx(dctx, &BinaryData[0], MAX_PACKET_SIZE, &DataBuffer[Offset], Size);
			m_read = m_StartPos;
			bRecycle = false;
		}

		SOCKADDR_INET* GetAddress() noexcept
		{
			return reinterpret_cast<SOCKADDR_INET*>(&DataBuffer[Address->Offset]);
		}

		PacketID GetPID() noexcept
		{
			return *PID;
		}

		ClientID GetCID() noexcept
		{
			return *CID;
		}

		uint8_t GetOID() noexcept
		{
			return *OID;
		}

		uintmax_t GetUID() noexcept
		{
			return *UID;
		}

		uintmax_t GetTimestamp() noexcept
		{
			return *Timestamp;
		}

		//
		//	Reads and returns the next value in our packet.
		//	Reads MUST occur in the same order they were written!
		template <typename T> const bool read(T& Var) noexcept {
			//static_assert(std::is_trivial_v<T>);
			//
			//	Get the size of this read
			constexpr std::size_t bytes = sizeof(T);
			//
			//	Ensure we don't read past the end of our data buffer
			if (m_read + bytes >= MAX_PACKET_SIZE) { return false; }
			//
			//	Copy the data from the data buffer to our variable
			std::memcpy(&Var, &BinaryData[m_read], bytes);
			//
			//	Advance our read position
			m_read += bytes;
			return true;
		}

		//
		//	Specialized
		template <>	const bool read(char& Var) noexcept {
			//static_assert(std::is_trivial_v<T>);
			//
			//	Get the size of this read
			constexpr std::size_t bytes = sizeof(size_t);
			//
			//	Ensure we don't read past the end of our data buffer
			if (m_read + bytes >= MAX_PACKET_SIZE) { return false; }
			//
			//	Copy the data from the data buffer to our variable
			size_t charSize;
			std::memcpy(&charSize, &BinaryData[m_read], bytes);
			//
			//	Advance our read position
			m_read += bytes;
			//
			//	Ensure we don't read past the end of our data buffer
			if (m_read + charSize >= MAX_PACKET_SIZE) { return false; }
			//
			//	Copy the data from the data buffer to our variable
			strncat(&Var, &BinaryData[m_read], charSize);
			//
			//	Advance our read position
			m_read += charSize;
			return Var;
		}
	};
}