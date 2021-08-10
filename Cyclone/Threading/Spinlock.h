#pragma once
#include <atomic>

/*
    Spinlocks allow threads to simply wait in a loop while repeatedly checking if a lock is released. As the thread remains active and is simply not performing tasks, we're practically
    "busy waiting". The difference to a mutex is that this doesn't let the thread yield, but instead spin (loop) on an atomic flag until the spinlock can be released, during which execution
    will proceed.

    For more information: https://stackoverflow.com/questions/1957398/what-exactly-are-spin-locks
*/

namespace JobSystem
{
    class Spinlock
    {
    public:
        void Lock()
        {
            while (!Try_Lock()) {}
        }

        bool Try_Lock()
        {
            return !m_Lock.test_and_set(std::memory_order_acquire);
        }

        void Unlock()
        {
            m_Lock.clear(std::memory_order_release);
        }

    private:
        std::atomic_flag m_Lock = ATOMIC_FLAG_INIT;
    };
}
