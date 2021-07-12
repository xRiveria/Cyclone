#pragma once
#include <functional>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include "RingBuffer.h"

/* Contextual Information

    - A task is defined as any function or workload the engine/user wishes to execute asynchronously. This could be input polling, UI updates, system initialization or even rendering.
    - Jobs are generated to fulfill a task, which are then grouped up and executed in parallel in our threads.
    - When notify_all() is called, all our threads are woken up and will proceed to look for tasks serially, picking up and executing jobs found. If no jobs are found, they return to sleep promptly.
*/

namespace JobSystem
{
    struct JobInformation
    {
        uint32_t m_JobIndex;        // Job index relative to dispatch.
        uint32_t m_GroupID;         // Group index relative to dispatch.
        uint32_t m_GroupIndex;      // Job index relative to group.
        bool m_IsFirstJobInGroup;   // Is the current job the first one in the group?
        bool m_IsLastJobInGroup;    // Is the current job the last one in the group?
    };

    struct Job
    {
        std::function<void(JobInformation)> m_Job;
        uint32_t m_GroupID;
        uint32_t m_GroupJobOffset;
        uint32_t m_GroupJobEnd;
    };

    class JobSystem
    {
    public:
        void Initialize(); // Create internal resources such as worker threads.

        /*
            Divide a single task into multiple jobs and execute them in parallel.
            - Job Count: The amount of jobs to generate for this task.
            - Group Size: How many jobs to execute per thread. Jobs inside a group execute serially. For small jobs, it might be worth it to increase this number.
            - Job Information: Receives a JobInformation struct as a parameter.
        */
        void Dispatch(uint32_t jobCount, uint32_t groupSize, const std::function<void(JobInformation)>& jobInformation);
        void Execute(const std::function<void(JobInformation)>& jobInformation); // Adds a task to execute asynchronously. Any idle thread will execute this job.

        bool IsBusy(); // Allows the main thread to check if any worker threads are busy executing jobs.
        void Wait();   // Wait until all threads become idle.

        uint32_t GetThreadCount() const { return m_ThreadCountTotal; }
        uint32_t GetThreadCountSupported() const { return m_ThreadCountSupported; }
        uint32_t GetThreadCountAvaliable();

    private:
        bool TaskLoop();

    private:
        uint32_t m_ThreadCountTotal = 0;            // Total thread count.
        uint32_t m_ThreadCountSupported = 0;        // -1 for the main thread.

        std::condition_variable m_WakeCondition;    // Used with g_WakeMutex. Worker threads simply sleep when there are no jobs. The main thread can wake them up by alerting this condition.
        std::mutex m_WakeMutex;                     // Used in conjunction with g_WakeCondition.
        RingBuffer<Job, 256> m_JobQueue;            // A thread safe queue to put pending jobs onto the end. This has a capicity of 256 jobs. A worker thread can grab a job from the beginning of the queue.
        std::atomic<uint32_t> m_Counter{ 0 };       // Defines a state of execution. Can be waited on. This tells us how many threads must finish their tasks for the Job System to become idle. 
    };
}

/// Retrieve thread job information and display them in editor.