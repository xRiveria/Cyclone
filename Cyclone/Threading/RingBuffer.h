#pragma once
#include "Spinlock.h"

namespace JobSystem
{
    template<typename T, size_t capacity>
    class RingBuffer
    {
    public:
        // Pushes an item to the end of the buffer if there is free space. Returns true if successful, returns false if there is a lack of space.
        inline bool push_back(const T& item)
        {
            bool result = false;

            m_Lock.Lock();
            size_t nextIndex = (m_Head + 1) % capacity;
            if (nextIndex != m_Tail)
            {
                m_Data[m_Head] = item;
                m_Head = nextIndex;
                result = true;
            }
            m_Lock.Unlock();

            return result;
        }

        // Retrieves an item if there are any. Returns true if successful, returns false if there are no such items.
        inline bool pop_front(T& item)
        {
            bool result = false;

            m_Lock.Lock();
            if (m_Tail != m_Head)
            {
                item = m_Data[m_Tail];
                m_Tail = (m_Tail + 1) % capacity;
                result = true;
            }
            m_Lock.Unlock();

            return result;
        }

    private:
        T m_Data[capacity];
        size_t m_Head = 0;
        size_t m_Tail = 0;  // Current location on our m_Data buffer. Its index is the current amount of tasks in our buffer.
        Spinlock m_Lock;
    };
}