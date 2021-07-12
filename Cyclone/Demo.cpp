#include "Threading/JobSystem.h"
#include <chrono>
#include <iostream>

JobSystem::JobSystem g_JobSystem;

void Spin(float milliseconds)
{
    milliseconds /= 1000.0f;  // Convert to seconds.
    std::chrono::high_resolution_clock::time_point timePoint1 = std::chrono::high_resolution_clock::now();
    double ms = 0;

    while (ms < milliseconds)
    {
        std::chrono::high_resolution_clock::time_point timePoint2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> timeSpan = std::chrono::duration_cast<std::chrono::duration<double>>(timePoint2 - timePoint1);
        ms = timeSpan.count();
    }
}

struct Stopwatch
{
    std::string m_ProcessName;
    std::chrono::high_resolution_clock::time_point m_Start;

    Stopwatch(const std::string& processName) : m_ProcessName(processName), m_Start(std::chrono::high_resolution_clock::now()) {}
    ~Stopwatch()
    {   
        std::chrono::steady_clock::time_point endTimePoint = std::chrono::high_resolution_clock::now();
        std::cout << m_ProcessName << ": " << std::chrono::duration_cast<std::chrono::microseconds>(endTimePoint - m_Start).count() << " milliseconds." << std::endl;
    }
};

struct Data
{
    float m_Data[16];

    void Compute(uint32_t value)
    {
        for (int i = 0; i < 16; ++i)
        {
            m_Data[i] += float(value + i);
        }
    }
};

int main(int argc, int argv[])
{
    g_JobSystem.Initialize();

    for (auto& key : g_JobSystem.m_ThreadNames)
    {
        std::cout << "Thread ID: " << key.first << ", Thread Name: " << key.second << std::endl;
    }

    std::cout << g_JobSystem.GetThreadCountAvaliable() << " Threads Avaliable." << std::endl;
    // Serial Test
    {
        Stopwatch T = Stopwatch("Serial Test: ");
        Spin(100);
        Spin(100);
        Spin(100);
        Spin(100);
        Spin(100);
        Spin(100);
    }

    // Execute Test
    {
        Stopwatch T = Stopwatch("Execute Test: ");
        g_JobSystem.Execute([](JobSystem::JobInformation jobArguments) { Spin(100); });
        g_JobSystem.Execute([](JobSystem::JobInformation jobArguments) { Spin(100); });
        g_JobSystem.Execute([](JobSystem::JobInformation jobArguments) { Spin(100); });
        g_JobSystem.Execute([](JobSystem::JobInformation jobArguments) { Spin(100); });
        std::cout << g_JobSystem.GetThreadCountAvaliable() << " Threads Avaliable." << std::endl;
        g_JobSystem.Execute([](JobSystem::JobInformation jobArguments) { Spin(100); });
        g_JobSystem.Execute([](JobSystem::JobInformation jobArguments) { Spin(100); });

        g_JobSystem.Wait();
    }

    uint32_t dataCount = 1000000;

    // Serial Loop Test 
    {
        Data* dataSet = new Data[dataCount];
        {
            Stopwatch T = Stopwatch("Serial Loop Test: ");

            for (uint32_t i = 0; i < dataCount; ++i)
            {
                dataSet[i].Compute(i);
            }
        }

        delete[] dataSet;
    }

    std::cout << g_JobSystem.GetThreadCountAvaliable() << " Threads Avaliable." << std::endl;

    // Dispatch Test
    {
        Data* dataSet = new Data[dataCount];
        {
            Stopwatch T = Stopwatch("Dispatch Test: ");

            const uint32_t groupSize = 1000;
            g_JobSystem.Dispatch(dataCount, groupSize, [&dataSet](JobSystem::JobInformation arguments)
            {
                dataSet[arguments.m_GroupIndex].Compute(1);
            });
            g_JobSystem.Wait();
        }

        delete[] dataSet;
    }

    std::cout << g_JobSystem.GetThreadCountAvaliable() << " Threads Avaliable." << std::endl;

    return 0;
}