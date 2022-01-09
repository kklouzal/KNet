#pragma once

namespace KNet
{
	template <typename T, int MaxSize> class NetPool
	{
	private:
		alignas(alignof(std::max_align_t)) char* const _Data;
		RIO_BUFFERID _BufferID;
		std::deque<T*> _Pool;
		std::deque<T*> _Free;
	public:
		NetPool(const DWORD PoolSize, unsigned int Internal, void* ParentObject) :
			_Data(new char[PoolSize * MaxSize]),
			_BufferID(g_RIO.RIORegisterBuffer(_Data, PoolSize* MaxSize))
		{
			KN_CHECK_RESULT(_BufferID, RIO_INVALID_BUFFERID);
			//
			//	Create our pool of objects
			for (uint32_t i = 0; i < PoolSize; i++) {
				T* Object = new T(_Data);
				((PRIO_BUF)Object)->BufferId = _BufferID;
				((PRIO_BUF)Object)->Length = MaxSize;
				((PRIO_BUF)Object)->Offset = MaxSize * i;
				Object->Parent = ParentObject;
				_Pool.push_back(Object);
				_Free.push_back(Object);
				Object->InternalID = Internal;
				Object->InternalUniqueID = i;
			}
		}

		~NetPool()
		{
			printf("Cleaning Up %i NetPool Objects\n", (int)_Pool.size());
			for (auto Object : _Pool) {
				delete Object;
			}
			g_RIO.RIODeregisterBuffer(_BufferID);
			delete[] _Data;
			printf("Done\n");
		}

		//
		//	Returns a reference to the entire pool of objects
		std::deque<T*>& GetAllObjects()
		{
			return _Pool;
		}

		//
		//	Returns a pointer to a single unused object
		//	Otherwise returns nullptr if no free objects available
		T* GetFreeObject()
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
			_Free.push_back(Object);
		}
	};
}