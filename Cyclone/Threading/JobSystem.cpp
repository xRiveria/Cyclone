#include "JobSystem.h"
#include <thread>
#define NOMINMAX
#include <windows.h>
#include <sstream>
#include <assert.h>
#include <winerror.h>
#include <deque>
#include <iostream>

namespace Cyclone
{
    // Denotes a single task (or function call) submitted by the user either with Execution() or Dispatch() as part of a larger work group.
    struct Job
    {
        std::function<void(JobArguments)> m_Task;
        Context* m_Context; // The execution context which the job belongs to.
        uint32_t m_GroupID;
        uint32_t m_GroupJobOffset;
        uint32_t m_GroupJobEnd;
        uint32_t m_SharedMemorySize;

        void Execute()
        {
            JobArguments jobArguments;
            jobArguments.m_GroupID = m_GroupID;
            if (m_SharedMemorySize > 0)
            {
                thread_local static std::vector<uint8_t> sharedAllocationData;
                sharedAllocationData.reserve(m_SharedMemorySize);
                jobArguments.m_SharedMemory = sharedAllocationData.data();
            }
            else
            {
                jobArguments.m_SharedMemory = nullptr;
            }

            for (uint32_t i = m_GroupJobOffset; i < m_GroupJobEnd; i++)
            {
                jobArguments.m_JobIndex = i;
                jobArguments.m_JobGroupIndex = i - m_GroupJobOffset;
                jobArguments.m_IsFirstJobInGroup = (i == m_GroupJobOffset);
                jobArguments.m_IsLastJobInGroup = (i == (m_GroupJobEnd - 1));
                m_Task(jobArguments);
            }

            m_Context->m_JobCounter.fetch_sub(1); // Decrement job count.
        }
    };

    struct JobQueue
    {
        std::deque<Job> m_Queue;
        std::mutex m_QueueLock;

        void PushBack(const Job& newJob)
        {
            std::scoped_lock lock(m_QueueLock);
            m_Queue.push_back(newJob);
        }

        bool PopFront(Job& existingJob)
        {
            std::scoped_lock lock(m_QueueLock);
            if (m_Queue.empty())
            {
                return false;
            }
            existingJob = std::move(m_Queue.front());
            m_Queue.pop_front();
            return true;
        }
    };

    struct PriorityResources
    {
        uint32_t m_ThreadCount = 0;
        std::vector<std::thread> m_Threads;
        std::unique_ptr<JobQueue[]> m_JobQueuesPerThread;
        std::atomic<uint32_t> m_NextQueueIndex = 0;
        std::condition_variable m_WakeCondition;
        std::mutex m_WakeMutex;

        // Starts working on a job queue. After the job queue is finished, it can switch to another queue and steal jobs from there.
        void Work(uint32_t startingQueueIndex)
        {
            Job existingJob;
            for (uint32_t i = 0; i < m_ThreadCount; i++)
            {
                JobQueue& jobQueue = m_JobQueuesPerThread[startingQueueIndex % m_ThreadCount];
                while (jobQueue.PopFront(existingJob))
                {
                    existingJob.Execute();
                }

                startingQueueIndex++; // Head to the next queue and steal jobs.
            }
        }
    };

    // Once destroyed, worker threads will be woken up and end their loops.
    struct InternalState
    {
        uint32_t m_CoreCount = 0;
        PriorityResources m_Resources[int(Priority::Count)];
        std::atomic_bool m_IsAlive = true; // Denotes if new jobs can be addded to the scheduler.

        void Shutdown()
        {
            m_IsAlive.store(false); // New jobs cannot be added from this point.
            bool runWakingLoop = true;
            std::thread wakingThread([&]
            {
                while (runWakingLoop)
                {
                    for (auto& resource : m_Resources)
                    {
                        resource.m_WakeCondition.notify_all(); // Wakes up all sleeping worker threads.
                    }
                }
            });

            for (auto& resource : m_Resources)
            {
                for (auto& thread : resource.m_Threads)
                {
                    thread.join();
                }
            }

            runWakingLoop = false;
            wakingThread.join();

            for (auto& resource : m_Resources)
            {
                resource.m_JobQueuesPerThread.reset();
                resource.m_Threads.clear();
                resource.m_ThreadCount = 0;
            }

            m_CoreCount = 0;
        }

        ~InternalState()
        {
            Shutdown();
        }
    };

    InternalState* g_InternalState = nullptr;

    void Initialize(uint32_t maxThreadCount)
    {
        g_InternalState = new InternalState();
        maxThreadCount = std::max(1u, maxThreadCount); // 1 for our main thread.
        g_InternalState->m_CoreCount = std::thread::hardware_concurrency();

        for (int priorityTypeIndex = 0; priorityTypeIndex < int(Priority::Count); priorityTypeIndex++)
        {
            const Priority priorityType = (Priority)priorityTypeIndex;
            PriorityResources& resource = g_InternalState->m_Resources[priorityTypeIndex];

            // Calculate the actual number of worker threads we want. We want all threads to be assigned a core accordingly.
            switch (priorityType)
            {
            case Priority::High:
                resource.m_ThreadCount = g_InternalState->m_CoreCount - 1; // -1 for the main thread.
                break;
            case Priority::Low:
                resource.m_ThreadCount = g_InternalState->m_CoreCount - 2; // -1 for the main thread, -1 for streaming.
                break;
            case Priority::Streaming:
                resource.m_ThreadCount = 1;
                break;
            default:
                assert(0);
                break;
            }

            resource.m_ThreadCount = std::clamp(resource.m_ThreadCount, 1u, maxThreadCount);
            resource.m_JobQueuesPerThread.reset(new JobQueue[resource.m_ThreadCount]);
            resource.m_Threads.reserve(resource.m_ThreadCount);

            for (uint32_t threadID = 0; threadID < resource.m_ThreadCount; threadID++)
            {
                std::thread& workerThread = resource.m_Threads.emplace_back([threadID, &resource]
                {
                    while (g_InternalState->m_IsAlive.load())
                    {
                        resource.Work(threadID);

                        // Once jobs are complete, the thread is put to sleep until it is woken up again.
                        std::unique_lock<std::mutex> lock(resource.m_WakeMutex);
                        resource.m_WakeCondition.wait(lock);
                    }
                });

                std::thread::native_handle_type threadHandle = workerThread.native_handle();
                int coreID = threadID + 1;

                if (priorityType == Priority::Streaming)
                {
                    // Put streaming on the last core.
                    // The second thread with ID 1 with streaming prioprity is assigned to the second last core at N - 1 - 1.
                    coreID = g_InternalState->m_CoreCount - 1 - threadID; 
                }

#ifdef _WIN32
                // Put each thread on a dedicated core.
                DWORD_PTR affinityMask = 1ull << coreID;
                DWORD_PTR affinityResult = SetThreadAffinityMask(threadHandle, affinityMask);
                assert(affinityResult > 0);

                if (priorityType == Priority::High)
                {
                    BOOL priorityResult = SetThreadPriority(threadHandle, THREAD_PRIORITY_NORMAL);
                    assert(priorityResult != 0);

                    std::wstring threadName = L"Cyclone::HighPriorityJobThread_" + std::to_wstring(threadID);
                    HRESULT namingResult = SetThreadDescription(threadHandle, threadName.c_str());
                    assert(SUCCEEDED(namingResult));
                }
                else if (priorityType == Priority::Low)
                {
                    BOOL priorityResult = SetThreadPriority(threadHandle, THREAD_PRIORITY_LOWEST);
                    assert(priorityResult != 0);

                    std::wstring threadName = L"Cyclone::LowPriorityJobThread_" + std::to_wstring(threadID);
                    HRESULT namingResult = SetThreadDescription(threadHandle, threadName.c_str());
                    assert(SUCCEEDED(namingResult));
                }
                else if (priorityType == Priority::Streaming)
                {
                    BOOL priorityResult = SetThreadPriority(threadHandle, THREAD_PRIORITY_LOWEST);
                    assert(priorityResult != 0);

                    std::wstring threadName = L"Cyclone::StreamingLowPriorityJobThread_" + std::to_wstring(threadID);
                    HRESULT namingResult = SetThreadDescription(threadHandle, threadName.c_str());
                    assert(SUCCEEDED(namingResult));
                }
#endif               
            }
        }

        // Post message to logging system.
        char logMessage[256] = {};
        snprintf(logMessage, sizeof(logMessage), "Cyclone initialized with %d Cores!\nHigh Priority Threads: %d\nLow Priority Threads: %d\nStreaming Threads: %d", g_InternalState->m_CoreCount, GetThreadCount(Priority::High), GetThreadCount(Priority::Low), GetThreadCount(Priority::Streaming));
        std::cout << logMessage << "\n\n";
    }

    void Shutdown()
    {
        g_InternalState->Shutdown();
        delete g_InternalState;
    }

    uint32_t GetThreadCount(Priority priorityType)
    {
        return g_InternalState->m_Resources[int(priorityType)].m_ThreadCount;
    }

    bool IsBusy(const Context& executionContext)
    {
        return executionContext.m_JobCounter.load() > 0; // m_JobCounter denotes the number of jobs that still needs to be executed.
    }

    void Wait(const Context& executionContext)
    {
        if (IsBusy(executionContext))
        {
            PriorityResources& resource = g_InternalState->m_Resources[int(executionContext.m_Priority)];

            // Wake any threads that might be sleeping.
            resource.m_WakeCondition.notify_all();

            // Work() will pick up any jobs that are still waiting and execute them on this thread.
            resource.Work(resource.m_NextQueueIndex.fetch_add(1) % resource.m_ThreadCount);

            while (IsBusy(executionContext))
            {
                // If we're here, it means there are jobs that couldn't be picked up by this thread.
                // In this case, it means there are jobs that are in the process of executing.
                // We will allow the thread to be swapped out by the OS to avoid spinning endlessly.
                std::this_thread::yield();
            }
        }
    }

    uint32_t GetDispatchGroupCount(uint32_t jobCount, uint32_t groupSize)
    {
        // Calculates the amount of job groups to dispatch. We will overestimate here.
        return (jobCount + groupSize - 1) / groupSize;
    }

    // Singular task in its own group.
    void Execute(Context& executionContext, const std::function<void(JobArguments)>& task)
    {
        PriorityResources& resource = g_InternalState->m_Resources[int(executionContext.m_Priority)];

        // Update execution context.
        executionContext.m_JobCounter.fetch_add(1);

        Job newJob;
        newJob.m_Context = &executionContext;
        newJob.m_Task = task;
        newJob.m_GroupID = 0;
        newJob.m_GroupJobOffset = 0;
        newJob.m_GroupJobEnd = 1;
        newJob.m_SharedMemorySize = 0;

        // If our job system hasn't been initialized, or if only a single thread exists, the job is executed immediately.
        if (resource.m_ThreadCount <= 1)
        {
            newJob.Execute();
        }

        resource.m_JobQueuesPerThread[resource.m_NextQueueIndex.fetch_add(1) % resource.m_ThreadCount].PushBack(newJob);
        resource.m_WakeCondition.notify_one(); // All threads in the resource wait on this wake condition. This guarantees one awaiting thread is woken up to handle the job.
    }

    void Dispatch(Context& executionContext, uint32_t jobCount, uint32_t groupSize, const std::function<void(JobArguments)>& task, size_t sharedMemorySize)
    {
        if (jobCount == 0 || groupSize == 0)
        {
            return;
        }

        PriorityResources& resource = g_InternalState->m_Resources[int(executionContext.m_Priority)];
        // A job is generated per group. Tasks within the same group execute serially, using the aforementioned job.
        const uint32_t groupCount = GetDispatchGroupCount(jobCount, groupSize);

        // Update execution context.
        executionContext.m_JobCounter.fetch_add(groupCount);

        Job newJob;
        newJob.m_Context = &executionContext;
        newJob.m_Task = task;
        newJob.m_SharedMemorySize = (uint32_t)sharedMemorySize;

        for (uint32_t groupID = 0; groupID < groupCount; groupID++)
        {
            // For each group, generate one real job.
            newJob.m_GroupID = groupID;
            newJob.m_GroupJobOffset = groupID * groupSize;
            newJob.m_GroupJobEnd = std::min(newJob.m_GroupJobOffset + groupSize, jobCount); // Prevents overflowing at the lasr group.

            // If our job system hasn't been initialized, or if only a single thread exists, the job is executed immediately.
            if (resource.m_ThreadCount <= 1)
            {
                newJob.Execute();
            }
            else
            {
                resource.m_JobQueuesPerThread[resource.m_NextQueueIndex.fetch_add(1) % resource.m_ThreadCount].PushBack(newJob);
            }
        }

        // Get all awaiting threads to pick off at the new jobs.
        if (resource.m_ThreadCount > 1)
        {
            resource.m_WakeCondition.notify_all();
        }
    }
}