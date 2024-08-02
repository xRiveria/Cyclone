#pragma once
#include <functional>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <unordered_map>
#include "RingBuffer.h"

/* == Threading ==

    A task is defined as any function or workload the engine/user wishes to execute asychronously. This could be input polling, UI updates, system initialization or even rendering. Jobs
    are generated to fulfill a task, which are then grouped up and executed in parallel in our threads.

    We currently supported singular (function) and loop based parallization.

    When notify_all() is called, our threads are woken up and will proceed to look for tasks serially, picking up and executing jobs found. If no jobs are found, they return to a perpertual waiting state.
*/

namespace JobSystem
{
    struct JobInformation
    {
        uint32_t m_JobIndex;      // Job index relative to dispatch.
        uint32_t m_GroupID;       // Group index relative to dispatch.
        uint32_t m_GroupIndex;    // Job index relative to group.
        bool m_IsFirstJobInGroup; // Is the current job the first one in the group?
        bool m_IsLastJobInGroup;  // Is the current job the last one in the group?
    };

    struct Job
    {
        std::function<void(JobInformation)> m_Job;
        uint32_t m_GroupID = 0;
        uint32_t m_GroupJobOffset = 0;
        uint32_t m_GroupJobEnd = 0;
    };

    class JobSystem
    {
    public:
        bool Initialize(); // Creates our internal resources such as worker threads.

        /* == Dispatch ==
        *
            Divides a single task into multiple jobs and execute them in parallel.

            - Job Count: The amount of jobs to generate for this thread.
            - Group Size: How many jobs to execute per thread. Jobs inside a group execute serially. Any idle thread will execute this job.
            - Job Information: Receives a JobInformation struct as a parameter.
        */

        void Dispatch(uint32_t jobCount, uint32_t groupSize, const std::function<void(JobInformation)>& jobInformation);
        void Execute(const std::function<void(JobInformation)>& jobInformation); // Adds a task to execute asynchronously. Any idle thread will execute this job.

        bool IsBusy(); // Allows the main thread to check if any worker threads are busy executing jobs.
        void Wait();   // Wait until all threads become idle.

        uint32_t GetThreadCount() const { return m_ThreadCountTotal; }
        uint32_t GetThreadCountSupported() const { return m_ThreadCountSupported; }
        uint32_t GetThreadCountAvaliable();

        uint32_t GetQueuedTasksCount() const { return m_Counter.load(); }

        bool IsMainThreadUtilitizedForTasks() const { return m_UseMainThreadForTasks; }
        void UseMainThreadForTasks(bool value) { m_UseMainThreadForTasks = value; } // Not recommended as it can affect your current program.

    private:
        bool TaskLoop();
        uint32_t CalculateDispatchJobCount(uint32_t jobCount, uint32_t groupSize);

    public:
        std::unordered_map<std::thread::id, std::string> m_ThreadNames;

    private:
        bool m_UseMainThreadForTasks = false;

        uint32_t m_ThreadCountTotal = 0;         // Total thread count for our system.
        uint32_t m_ThreadCountSupported = 0;     // -1 for the main thread. We can also reserve certain threads for certain systems.

        std::condition_variable m_WakeCondition; // Used with g_WakeMutex. Worker threads simply sleep when there are no jobs and can be waken up with this condition.
        std::mutex m_WakeMutex;                  // As above.

        std::atomic<uint32_t> m_Counter{ 0 };    // Defines a state of execution. Can be waited on. This tells us how many threads must finish their tasks for the threading library to become idle.
        RingBuffer<Job, 256> m_JobQueue;         // A thread safe queue to put pending jobs onto. This has a capacity of 256 jobs. A worker thread can grab a job from the end of the queue, and new tasks are appended to the start.
    };
}

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