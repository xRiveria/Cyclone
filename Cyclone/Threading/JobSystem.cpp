#include "JobSystem.h"
#include <thread>

namespace JobSystem
{
    void JobSystem::Initialize()
    {
        // Retrieve the number of hardware threads in the system.
        m_ThreadCountTotal = std::thread::hardware_concurrency();

        // Calculate the actual number of worker threads we want (-1 for the main thread).
        m_ThreadCountSupported = m_ThreadCountTotal - 1;

        // Create all our works threads while immediately starting them. Each thread is put on an infinite loop and will be so until the application exists. The threads will mostly sleep (until the main thread wakes it up) to not spin the CPU wastefully.
        for (uint32_t threadID = 0; threadID < m_ThreadCountSupported; ++threadID)
        {
            std::thread worker([this] {
                while (true)
                {
                    if (!TaskLoop())
                    {
                        // No tasks, put the thread to sleep.
                        std::unique_lock<std::mutex> lock(m_WakeMutex);
                        m_WakeCondition.wait(lock);
                    }
                }
            });

            // Platform specific setup.


            worker.detach(); // Seperates the thread of execution from the thread object, allowing execution to continue independantly (our infinite loop).
        }
    }

    void JobSystem::Execute(const std::function<void(JobInformation)>& jobInformation)
    {
        m_Counter.fetch_add(1);

        Job job;
        job.m_Job = jobInformation;
        job.m_GroupID = 0;
        job.m_GroupJobOffset = 0;
        job.m_GroupJobEnd = 1;

        // Try to push a new job until it is pushed successfully.
        while (!m_JobQueue.push_back(job)) { m_WakeCondition.notify_all(); TaskLoop(); }

        // Wake any one thread that might be sleeping.
        m_WakeCondition.notify_one();
    }

    // This function executes the next item from the job queue. Return true if successful, false if there is no job avaliable.
    bool JobSystem::TaskLoop()
    {
        Job job;
        if (m_JobQueue.pop_front(job))
        {
            JobInformation jobArguments;
            jobArguments.m_GroupID = job.m_GroupID;
            
            for (uint32_t i = job.m_GroupJobOffset; i < job.m_GroupJobEnd; ++i)
            {
                jobArguments.m_JobIndex = i;
                jobArguments.m_GroupIndex = i - job.m_GroupJobOffset;
                jobArguments.m_IsFirstJobInGroup = (i == job.m_GroupJobOffset);
                jobArguments.m_IsLastJobInGroup = (i == job.m_GroupJobEnd - 1);
                job.m_Job(jobArguments);
            }

            m_Counter.fetch_sub(1);
            return true;
        }

        return false;
    }

    uint32_t CalculateDispatchGroupCount(uint32_t jobCount, uint32_t groupSize)
    {
        // Calculates the amount of job groups to dispatch.
        return (jobCount + groupSize - 1) / groupSize;
    }

    void JobSystem::Dispatch(uint32_t jobCount, uint32_t groupSize, const std::function<void(JobInformation)>& taskInformation)
    {
        if (jobCount == 0 || groupSize == 0)
        {
            return;
        }

        const uint32_t groupCount = CalculateDispatchGroupCount(jobCount, groupSize); // Tell us how many worker threads (or groups) will be put onto the job pool.

        // Update context state.
        m_Counter.fetch_add(groupCount);

        Job job;
        job.m_Job = taskInformation;

        for (uint32_t groupID = 0; groupID < groupCount; ++groupID)
        {
            // For each group, generate one real job.
            job.m_GroupID = groupID;
            job.m_GroupJobOffset = groupID * groupSize;
            job.m_GroupJobEnd = std::min(job.m_GroupJobOffset + groupSize, jobCount);

            // Try to push a new job until it is pushed successfully.
            while (!m_JobQueue.push_back(job)) { m_WakeCondition.notify_all(); TaskLoop(); }
        }

        // Wake up any threads that might be sleeping.
        m_WakeCondition.notify_all();
    }

    bool JobSystem::IsBusy()
    {
        // Whenever the context label is greater than 0, it means there are tasks to be completed.
        return m_Counter.load() > 0;
    }

    void JobSystem::Wait()
    {
        // Wake any threads that might be sleeping.
        m_WakeCondition.notify_all();

        // Waiting will also put the current thread to good use by working on another job if it can.
        while (IsBusy()) { TaskLoop(); }
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