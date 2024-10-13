#include "Core/Stopwatch.h"
#include "Threading/JobSystem.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtx/quaternion.hpp>
#include <gtx/transform.hpp>

using namespace Core;

void CameraUnitTest(uint32_t cameraCount);
void TransformUnitTest(uint32_t transformCount);
void SpinUnitTest(float milliseconds);

struct Data
{
    float m_Data[16];

    void Compute()
    {
        for (int i = 0; i < 16; ++i)
        {
            m_Data[i] += 1;
        }
    }
};

int main(int argc, int argv[])
{
    CYCLONE_UNREFERENCED_PARAMETER(argc);
    CYCLONE_UNREFERENCED_PARAMETER(argv);

    Cyclone::Initialize();

    // Serial Test: Simple Loops
    {
        Stopwatch T = Stopwatch("Serial Test (Ticking Counters)");
        SpinUnitTest(100);
        SpinUnitTest(100);
        SpinUnitTest(100);
        SpinUnitTest(100);
        SpinUnitTest(100);
        SpinUnitTest(100);
    }

    // Execute Test: Simple Loops
    {
        Stopwatch T = Stopwatch("Execute Test (Ticking Counters)");

        Cyclone::Context spinContext;

        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { CYCLONE_UNREFERENCED_PARAMETER(jobArguments); SpinUnitTest(100); });
        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { CYCLONE_UNREFERENCED_PARAMETER(jobArguments); SpinUnitTest(100); });
        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { CYCLONE_UNREFERENCED_PARAMETER(jobArguments); SpinUnitTest(100); });
        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { CYCLONE_UNREFERENCED_PARAMETER(jobArguments); SpinUnitTest(100); });
        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { CYCLONE_UNREFERENCED_PARAMETER(jobArguments); SpinUnitTest(100); });
        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { CYCLONE_UNREFERENCED_PARAMETER(jobArguments); SpinUnitTest(100); });
        Cyclone::Execute(spinContext, [](Cyclone::JobArguments jobArguments) { CYCLONE_UNREFERENCED_PARAMETER(jobArguments); SpinUnitTest(100); });

        Cyclone::Wait(spinContext);
    }

    uint32_t dataCount = 1500000;

    // Serial Test: Simple Loops
    {
        Data* dataSet = new Data[dataCount];
        {
            Stopwatch T = Stopwatch("Serial Test (Simple Loops)");

            for (uint32_t i = 0; i < dataCount; ++i)
            {
                dataSet[i].Compute();
            }
        }

        delete[] dataSet;
    }

    // Dispatch Test 1: Simple Loops
    {
        Data* dataSet = new Data[dataCount];

        {
            Cyclone::Context loopContext;
            Stopwatch T = Stopwatch("Dispatch Test (Simple Loops)");

            const uint32_t groupSize = 128;
            Cyclone::Dispatch(loopContext, dataCount, groupSize, [&dataSet](Cyclone::JobArguments arguments)
            {
                dataSet[arguments.m_JobGroupIndex].Compute();
            });
            Cyclone::Wait(loopContext);
        }

        delete[] dataSet;
    }

    // Dispatch Test 2: Camera Matrices (1500000 Camera Updates)
    CameraUnitTest(dataCount);

    // Dispatch Test 3: Entity Transforms (1500000 Transform Updates)
    TransformUnitTest(dataCount);

    return 0;
}

struct CameraComponent
{
    glm::mat4 m_ViewMatrix = glm::mat4(1.0f);
    glm::mat4 m_ProjectionMatrix = glm::mat4(1.0f);
    glm::mat4 m_ViewProjectionMatrix = glm::mat4(1.0f);
    glm::mat4 m_InverseViewProjectionMatrix = glm::mat4(1.0f);

    void UpdateCamera()
    {
        m_ViewMatrix = glm::lookAt(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        m_ProjectionMatrix = glm::perspective(90.0f, 1.78f, 0.1f, 1000.0f);
        m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
        m_InverseViewProjectionMatrix = glm::inverse(m_ViewProjectionMatrix);
    }
};

struct TransformComponent
{
    glm::vec3 m_Position;
    glm::vec3 m_Scale;
    glm::quat m_Rotation;
    glm::mat4 m_WorldMatrix; // Not going to bother with dirty checks here as we want to really squeeze performance metrics here.

    void UpdateTransform()
    {
        glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), m_Position);
        glm::mat4 rotationMatrix = glm::toMat4(m_Rotation);
        glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), m_Scale);

        m_WorldMatrix = translationMatrix * rotationMatrix * scaleMatrix;
    }
};

void CameraUnitTest(uint32_t cameraCount)
{
    {
        Stopwatch T = Stopwatch("Serial Test (Camera Transform Updates)");
        std::vector<CameraComponent> dataSet(cameraCount);
        for (uint32_t i = 0; i < cameraCount; i++)
        {
            dataSet[i].UpdateCamera();
        }
    }

    // Dispatch Test
    {
        Cyclone::Context cameraUpdateContext;
        Stopwatch T = Stopwatch("Dispatch Test (Camera Transform Updates)");
        std::vector<CameraComponent> dataSet(cameraCount);
        Cyclone::Dispatch(cameraUpdateContext, cameraCount, 1000, [&dataSet](Cyclone::JobArguments jobArguments)
        {
            dataSet[jobArguments.m_JobIndex].UpdateCamera();
        });

        Cyclone::Wait(cameraUpdateContext);
    }
}

void TransformUnitTest(uint32_t entityCount)
{
    // Serial Test
    {
        Stopwatch T = Stopwatch("Serial Test (Entity Transform Updates)");
        std::vector<TransformComponent> dataSet(entityCount);
        for (uint32_t i = 0; i < entityCount; i++)
        {
            dataSet[i].UpdateTransform();
        }
    }

    // Dispatch Test
    {
        Cyclone::Context transformUpdateContext;
        Stopwatch T = Stopwatch("Dispatch Test (Entity Transform Updates)");
        std::vector<TransformComponent> dataSet(entityCount);
        Cyclone::Dispatch(transformUpdateContext, entityCount, 1000, [&dataSet](Cyclone::JobArguments jobArguments)
            {
                dataSet[jobArguments.m_JobIndex].UpdateTransform();
            });

        Cyclone::Wait(transformUpdateContext);
    }
}

void SpinUnitTest(float milliseconds)
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