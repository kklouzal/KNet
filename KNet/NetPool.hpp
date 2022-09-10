#pragma once

namespace KNet
{
	template <typename T, const uint16_t MaxSize> class NetPool
	{
	private:
		alignas(alignof(std::max_align_t)) char* const _Data;
		RIO_BUFFERID _BufferID;
		std::deque<T*> _Pool;
		std::deque<T*> _Free;
	public:
		NetPool(const uint32_t PoolSize, void* ParentObject) :
			_Data(new char[PoolSize * MaxSize]),
			_BufferID(g_RIO.RIORegisterBuffer(_Data, PoolSize* MaxSize))
		{
			KN_CHECK_RESULT(_BufferID, RIO_INVALID_BUFFERID);
			//
			//	Create our pool of objects
			for (uint32_t i = 0; i < PoolSize; i++) {
				T* Object = new T(_Data);
				Object->BufferId = _BufferID;
				Object->Length = MaxSize;
				Object->Offset = MaxSize * i;
				Object->Parent = ParentObject;
				_Pool.push_back(Object);
				_Free.push_back(Object);
			}
		}

		~NetPool()
		{
			printf("Cleaning Up %zu NetPool Objects\n", _Pool.size());
			for (auto& Object : _Pool) {
				delete Object;
			}
			g_RIO.RIODeregisterBuffer(_BufferID);
			delete[] _Data;
			printf("Done\n");
		}

		//
		//	Returns a reference to the entire pool of objects
		std::deque<T*>& GetAllObjects() noexcept
		{
			return _Pool;
		}

		size_t Size() noexcept
		{
			return _Free.size();
		}

		//
		//	Returns a pointer to a single unused object
		//	Otherwise returns nullptr if no free objects available
		T* GetFreeObject() noexcept
		{
			if (!_Free.empty())
			{
				T* Object = _Free.front();
				_Free.pop_front();
				return Object;
			}
			else {
				return nullptr;
			}
		}

		//
		//	Places an object back into the pool of free objects
		void ReturnUsedObject(T* Object)
		{
			bool Go = true;
			for (auto Elem : _Free)
			{
				if (Elem == Object)
				{
					printf("TRYING TO INSERT DUPLICATE INTO FREE POOL!! BAD!\n");
					Go = false;
				}
			}
			if (Go)
			{
				_Free.push_back(Object);
			}
		}
	};
}