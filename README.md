# Cyclone

Cyclone is a C++ multithreading library built for usage in real-time applications on Windows. It provides a simple API for launching threads based on hardware availability whilst supporting loop-based parallelizations and priority scheduling. 

Tasks given to Cyclone are known as "Jobs". These are automatically picked up by available threads and processed without any need for manual intervention, although this behavior can be influenced by priority settings accordingly. Users may also manage cross-thread dependencies with the usage of Contexts, a high-level construct which threads can be assigned to and waited upon.

The syncing of results back to the main thread can be done through self-augmentation using event systems as required.

## Compilation

To build the library, simply navigate to the `Scripts` folder and run `CycloneBuildWindows.bat`. This will leverage Premake and automatically generate a C++17 solution in the project's root directory.

## Usage

```c++
// Dispatch Test: Loop Based Parallelization
{
    uint32_t dataCount = 1500000;
    Data* dataSet = new Data[dataCount];
    {
        Cyclone::Context loopContext;
        Stopwatch stopWatch = Stopwatch("Loop Dispatch Test Took: ");

        const uint32_t groupSize = 256; // Generates dataCount/groupSize individual jobs to process the loop.
        Cyclone::Dispatch(loopContext, dataCount, groupSize, [&dataSet](Cyclone::JobArguments jobArguments)
        {
            // Compute() starts a simple loop that counts to 16.
            dataSet[jobArguments.m_JobGroupIndex].Compute();
        });

        // Stalls the calling thread to ensure that all jobs belonging to the given context have completed execution.
        Cyclone::Wait(loopContext);
    }

    delete[] dataSet;
}

// Execution Test: Function Based
{
    Stopwatch stopWatch = Stopwatch("Batched Execution Test: ");
    Cyclone::Context spinContext;

    // Spin() starts a simple clock that ticks up to the given parameter in milliseconds.
    Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(10); });
    Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(100); });
    Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(1000); });
    Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(10000); });
    Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(100000); });
    Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(1000000); });
    Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { Spin(10000000); });

    // Stalls the calling thread to ensure that all jobs belonging to the given context have completed execution.
    Cyclone::Wait(spinContext);
}
