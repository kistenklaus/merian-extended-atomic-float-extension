#pragma once

#include "merian/vk/sync/semaphore.hpp"

namespace merian {
class TimelineSemaphore : public Semaphore {
  public:
    TimelineSemaphore(const SharedContext& context, const uint64_t initial_value = 0);

    uint64_t get_counter_value() const;

    // Waits until the semaphore holds a value that is >= the supplied value.
    // If timeout_nanos > 0: returns true of the value was signaled, false if the timeout was
    // reached. If timeout_nanos = 0: returns true if the value was signaled, false otherwise (does
    // not wait).
    bool wait(const uint64_t value, const uint64_t timeout_nanos = UINT64_MAX);

    void signal(const uint64_t value);
};

using TimelineSemaphoreHandle = std::shared_ptr<TimelineSemaphore>;
} // namespace merian
