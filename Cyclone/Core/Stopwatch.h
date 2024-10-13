#pragma once
#include <string>
#include <chrono>
#include <iostream>

namespace Core
{
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
}