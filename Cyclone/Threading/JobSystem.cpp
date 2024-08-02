#include "JobSystem.h"
#include <thread>
#define NOMINMAX
#include <windows.h>
#include <sstream>
#include <assert.h>
#include <winerror.h>
#include <deque>

namespace JobSystem
{
    bool JobSystem::Initialize()
    {
        // Retrieve the total number of hardware threads in the system.
        m_ThreadCountTotal = std::thread::hardware_concurrency();

        // Calculate the actual number of worker threads we want (-1 for the main thread, or any subsystems we wish to reserve a thread for).
        m_ThreadCountSupported = m_ThreadCountTotal - 1;
        m_ThreadNames[std::this_thread::get_id()] = "Main";

        // Create all our worker threads and put them to work.
        for (uint32_t threadID = 0; threadID < m_ThreadCountSupported; threadID++)
        {
            std::thread worker([this, threadID]
            {
                while (true)
                {
                    if (!TaskLoop())
                    {
                        // No tasks avaliable.
                        std::unique_lock<std::mutex> lock(m_WakeMutex);
                        m_WakeCondition.wait(lock); // Unlock and allow execution to proceed once condition is met.
                    }
                }
            });

            m_ThreadNames[worker.get_id()] = "Worker_" + std::to_string(threadID);

            // Platform specific setup. We will use the following to name our threads officially, allowing them to show up in Visual Studio Debugger.
            HANDLE hHandle = (HANDLE)worker.native_handle();
            std::wstringstream wideStringName;
            wideStringName << m_ThreadNames[worker.get_id()].c_str();
            SetThreadDescription(hHandle, wideStringName.str().c_str());

            worker.detach(); // Seperates the thread of execution, allowing execution to continue independantly. https://stackoverflow.com/questions/22803600/when-should-i-use-stdthreaddetache5
        }

        return true;
    }

    // The meat of our library. This function runs across all threads and is used to execute our jobs.
    bool JobSystem::TaskLoop()
    {
        Job job;
        if (m_JobQueue.pop_front(job))
        {
            JobInformation jobInformation = {};
            jobInformation.m_GroupID = job.m_GroupID;

            for (uint32_t i = job.m_GroupJobOffset; i < job.m_GroupJobEnd; ++i)
            {
                jobInformation.m_JobIndex = i;
                jobInformation.m_GroupIndex = i - job.m_GroupJobOffset;
                jobInformation.m_IsFirstJobInGroup = (i == job.m_GroupJobOffset);
                jobInformation.m_IsLastJobInGroup = (i == job.m_GroupJobEnd - 1);
                job.m_Job(jobInformation);
            }

            m_Counter.fetch_sub(1);
            return true;
        }

        return false;
    }

    uint32_t JobSystem::CalculateDispatchJobCount(uint32_t jobCount, uint32_t groupSize)
    {
        // Calculates the amount of jobs to dispatch for this specific task.
        return (jobCount + groupSize - 1) / groupSize;
    }

    void JobSystem::Dispatch(uint32_t jobCount, uint32_t groupSize, const std::function<void(JobInformation)>& jobInformation)
    {
        if (jobCount == 0 || groupSize == 0)
        {
            return;
        }

        const uint32_t groupCount = CalculateDispatchJobCount(jobCount, groupSize); // Tells us how many worker threads (or groups) will be activated to handle the queue.

        // Update context state.
        m_Counter.fetch_add(groupCount);

        Job job;
        job.m_Job = jobInformation;

        for (uint32_t groupID = 0; groupID < groupCount; ++groupID)
        {
            // For each group, generate one job to handle it.
            job.m_GroupID = groupID;
            job.m_GroupJobOffset = groupID * groupSize;
            job.m_GroupJobEnd = std::min(job.m_GroupJobOffset + groupSize, jobCount); // The last task belonging to this job.

            // Try to push this job until it is pushed successfully.
            while (!m_JobQueue.push_back(job))
            {
                m_WakeCondition.notify_all();
                TaskLoop();
            }
        }

        // Wake up any threads that might be sleeping.
        m_WakeCondition.notify_all();
    }

    void JobSystem::Execute(const std::function<void(JobInformation)>& jobInformation)
    {
        m_Counter.fetch_add(1);

        Job job;
        job.m_Job = jobInformation;
        job.m_GroupID = 0;
        job.m_GroupJobOffset = 0;
        job.m_GroupJobEnd = 1;

        // Try to push a new job. If we're unable to do so, we continue to wake threads up to execute existing tasks to make space for the new job.
        while (!m_JobQueue.push_back(job))
        {
            m_WakeCondition.notify_all();
            TaskLoop();
        }

        // Once pushed, we wake up a thread to execute the pushed thread.
        m_WakeCondition.notify_one();
    }

    bool JobSystem::IsBusy()
    {
        // Whenever the context label is greater than 0, it means there are tasks to be completed.
        return m_Counter.load() > 0;
    }

    void JobSystem::Wait()
    {
        // Wake any threads that might be sleeping to execute all tasks.
        m_WakeCondition.notify_all();

        // Waiting will also put the current thread to good use by working on a job if it can. Disable this to not make our main thread work.
        if (m_UseMainThreadForTasks)
        {
            TaskLoop();
        }
    }

    uint32_t JobSystem::GetThreadCountAvaliable()
    {
        if (IsBusy())
        {
            return m_ThreadCountSupported - m_Counter.load();
        }

        return m_ThreadCountSupported;
    }
}

namespace Cyclone
{
    struct Job
    {

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

            // Calculate the actual number of worker threads we want.
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
                ///
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

                /// Naming
            }
        }
    }
}