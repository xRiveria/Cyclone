#pragma once
#include <functional>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <unordered_map>
// The RingBuffer and SpinLock classes have been phased out to allow for a more streamlined implementation.
#include "RingBuffer.h"

// The engine does not know about the concept of Jobs. It simply is concerned with adding tasks that need to be executed in parallel.
namespace Cyclone
{
    struct JobArguments
    {
        uint32_t m_JobIndex; 
        uint32_t m_GroupID;
        uint32_t m_GroupIndex;
        bool m_IsFirstJobInGroup;
        bool m_IsLastJobInGroup;
        void* m_SharedMemory;
    };

    enum class Priority
    {
        High,           // Default
        Low,            // Pool of low priority threads, useful for generic tasks that shouldn't interface with high priority tasks.
        Streaming,      // Single low priority thread for streaming resources.
        Count
    };

    // Defines a state of execution. This can consists of multiple jobs which can be waited on.
    struct Context
    {
        std::atomic<uint32_t> m_JobCounter = 0;
        Priority m_Priority = Priority::High;
    };

    uint32_t GetThreadCount(Priority priority = Priority::High);
    
    // Adds a task to execute asynchronously. Any idle thread can execute this.
    void Execute(Context& executionContext, const std::function<void(JobArguments)>& task);

    // Divides a task into multiple jobs and executes them in parallel.
    // JobCount     - How many jobs to generate for this task.
    // GroupSize    - How many jobs to execute per thread. Jobs inside a group execute serially. 
    // Task         - The task at hand. Receive a JobArguments parameter defining the tasks themselves.
    void Dispatch(Context& executionContext, uint32_t jobCount, uint32_t groupSize, const std::function<void(JobArguments)>& task, size_t sharedMemorySize = 0);

    // Returns the number of job groups that will be created for a set number of jobs and a group size.
    uint32_t GetDispatchGroupCount(uint32_t jobCount, uint32_t groupSize);

    // Checks if any threads in the context are currently working on jobs.
    bool IsBusy(const Context& executionContext);

    // Wait until all threads become idle. The current thread will become a worker thread and assist in executing jobs. 
    void Wait(const Context& executionContext);
}