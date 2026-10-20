#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/Color.h>

#include <functional>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace Galeshi {

inline void ParallelFor(JPH::JobSystem* js, uint32_t count, const std::function<void(uint32_t, uint32_t)>& callback) {
    if (!js || count == 0) return;

    int maxConcurrency = js->GetMaxConcurrency();
    uint32_t numThreads = maxConcurrency > 0 ? static_cast<uint32_t>(maxConcurrency) : 1;
    uint32_t chunkSize = (count + numThreads - 1) / numThreads;

    // 1. Создаём барьер, который будет отслеживать завершение всех задач
    JPH::JobSystem::Barrier* barrier = js->CreateBarrier();

    // 2. Создаём задачи и сразу добавляем их в барьер
    for (uint32_t i = 0; i < count; i += chunkSize) {
        uint32_t start = i;
        uint32_t end = std::min(i + chunkSize, count);

        JPH::JobSystem::JobHandle handle = js->CreateJob(
            "ParallelForChunk",
            JPH::Color::sWhite,
            [callback, start, end]() {
                callback(start, end);
            }
        );

        barrier->AddJob(handle);
    }

    // 3. Ждём завершения всех задач, добавленных в барьер
    js->WaitForJobs(barrier);

    // 4. Уничтожаем барьер
    js->DestroyBarrier(barrier);
}

} // namespace Galeshi
