#pragma once
#include <atomic>

/*
    Spinlocks allow threads to simply wait in a loop while repeated checking if the lock is avaliable. As the thread remains active and is simply not performing tasks, we're practically "busy waiting".
    The difference to a mutex is that this doesn't let the thread yield, but instead spin on an atomic flag until the spinlock can be locked.

    For more information: https://stackoverflow.com/questions/1957398/what-exactly-are-spin-locks
*/

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