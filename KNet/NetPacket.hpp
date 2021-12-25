#pragma once

namespace KNet {
	//
	//	Send Packet
	class NetPacket_Send : public RIO_BUF {
	public:
		alignas(alignof(std::max_align_t)) char* const DataBuffer;
		OVERLAPPED Overlap;
		alignas(alignof(std::max_align_t)) char* const BinaryData;
		size_t m_write;

	public:
		PRIO_BUF Address = nullptr;

		NetPacket_Send(char* const Buffer) :
			DataBuffer(Buffer),
			Overlap(OVERLAPPED()),
			BinaryData(new char[MAX_PACKET_SIZE]),
			m_write(0)
		{
			Overlap.Pointer = this;
		}

		~NetPacket_Send() {
			delete[] BinaryData;
		}

		void Compress()
		{
			//memcpy(&SendBuffer[Packet->Offset], Packet->BinaryData, Packet->size());
			if (!LZ4_compress_default(&BinaryData[0], &DataBuffer[Offset], (int)m_write, MAX_PACKET_SIZE)) {
				printf("Packet Compression Failed\n");
			}
		}

		//	TODO: Handle multiple destinations using vector of addresses:
		//		  Add Address into vector of destination addresses.
		void AddDestination(PRIO_BUF Addr) {
			Address = Addr;
		}

		//
		//	Writes the next value in our packet
		//	Writes MUST be read in the same order they were written!
		template <typename T> const bool write(T value) {
			static_assert(std::is_trivial_v<T>);
			//
			//	Get the size of this write
			const std::size_t bytes = sizeof(T);
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
	};
	//
	//	Recv Packet
	//	TODO: get rid of extra 'Address RIO_BUFF' and calculate offset for using single buffer [(address)(packet)]
	class NetPacket_Recv : public RIO_BUF {
	public:
		alignas(alignof(std::max_align_t)) char* const DataBuffer;
		OVERLAPPED Overlap;
		alignas(alignof(std::max_align_t)) char* const BinaryData;
		size_t m_read;

	public:
		PRIO_BUF Address = nullptr;

		NetPacket_Recv(char* const Buffer) :
			DataBuffer(Buffer),
			Overlap(OVERLAPPED()),
			Address(new RIO_BUF),
			BinaryData(new char[MAX_PACKET_SIZE]),
			m_read(0)
		{
			Overlap.Pointer = this;
		}

		~NetPacket_Recv() {
			delete Address;
			delete[] BinaryData;
		}

		void Decompress(const ULONG Size)
		{
			//memcpy(&Packet->BinaryData[0], &RecvBuffer[Packet->Offset], Bytes);
			LZ4_decompress_safe(&DataBuffer[Offset], &BinaryData[0], Size, MAX_PACKET_SIZE);
			m_read = 0;
		}

		SOCKADDR_INET* GetAddress()
		{
			return reinterpret_cast<SOCKADDR_INET*>(&DataBuffer[Address->Offset]);
		}

		//
		//	Reads and returns the next value in our packet.
		//	Reads MUST occur in the same order they were written!
		template <typename T> const bool read(T& Var) {
			static_assert(std::is_trivial_v<T>);
			//
			//	Get the size of this read
			const std::size_t bytes = sizeof(T);
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
	};
}