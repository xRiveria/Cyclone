#include "Threading/JobSystem.h"
#include <chrono>
#include <iostream>

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
        const std::chrono::duration<double, std::milli> milliseconds = endTimePoint - m_Start;
        std::cout << m_ProcessName << ": " << static_cast<float>(milliseconds.count()) << " milliseconds." << std::endl;
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
    Cyclone::Initialize();

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
        Cyclone::Context spinContext;
        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(100); });
        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(100); });
        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(100); });
        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(100); });
        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(100); });
        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(100); });
        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(100); });

        Cyclone::Wait(spinContext);
    }

    uint32_t dataCount = 10000000;

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

    // Dispatch Test
    {
        Data* dataSet = new Data[dataCount];

        {
            Cyclone::Context loopContext;
            Stopwatch T = Stopwatch("Dispatch Test: ");

            const uint32_t groupSize = 1000;
            Cyclone::Dispatch(loopContext, dataCount, groupSize, [&dataSet](Cyclone::JobArguments arguments)
            {
                dataSet[arguments.m_JobGroupIndex].Compute(1);
            });
            Cyclone::Wait(loopContext);
        }

        delete[] dataSet;
    }

    return 0;
}