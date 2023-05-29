#pragma once

#include "vk/utils/check_result.hpp"

#include <mutex>
#include <vulkan/vulkan.hpp>

namespace merian {

/* A container that holds a queue together with a mutex and provides utility functions.
 *
 * All submits are protected using a mutex. When using *_wait the queue is blocked until the queue is idle.
 */
class QueueContainer {
  public:
    QueueContainer() = delete;

    QueueContainer(vk::Queue& queue) : queue(queue) {}
    QueueContainer(vk::Device& device, uint32_t queue_family_index, uint32_t queue_index) {
        queue = device.getQueue(queue_family_index, queue_index);
    }

    void submit(std::vector<vk::CommandBuffer>& command_buffers,
                vk::Fence fence = VK_NULL_HANDLE,
                const std::vector<vk::Semaphore>& wait_semaphores = {},
                const std::vector<vk::Semaphore>& signal_semaphores = {},
                const vk::PipelineStageFlags& wait_dst_stage_mask = {}) {
        vk::SubmitInfo submit_info{wait_semaphores, wait_dst_stage_mask, command_buffers, signal_semaphores};
        submit(submit_info, fence);
    }

    void submit(vk::CommandBuffer& command_buffer, vk::Fence fence = VK_NULL_HANDLE) {
        vk::SubmitInfo submit_info{
            {}, {}, {}, 1, &command_buffer,
        };
        submit(submit_info, fence);
    }

    void submit(vk::SubmitInfo& submit_info, vk::Fence fence = VK_NULL_HANDLE) {
        std::lock_guard<std::mutex> lock_guard(mutex);
        check_result(queue.submit(1, &submit_info, fence), "queue submit failed");
    }

    // Submits the command buffers then waits using waitIdle()
    void submit_wait(std::vector<vk::CommandBuffer>& command_buffers,
                     vk::Fence fence = VK_NULL_HANDLE,
                     const std::vector<vk::Semaphore>& wait_semaphores = {},
                     const std::vector<vk::Semaphore>& signal_semaphores = {},
                     const vk::PipelineStageFlags& wait_dst_stage_mask = {}) {
        vk::SubmitInfo submit_info{wait_semaphores, wait_dst_stage_mask, command_buffers, signal_semaphores};
        submit(submit_info, fence);
    }

    void submit_wait(vk::CommandBuffer& command_buffer, vk::Fence fence = VK_NULL_HANDLE) {
        vk::SubmitInfo submit_info{
            {}, {}, {}, 1, &command_buffer,
        };
        submit_wait(submit_info, fence);
    }

    // Submits then waits using waitIdle()
    void submit_wait(vk::SubmitInfo& submit_info, vk::Fence fence = VK_NULL_HANDLE) {
        std::lock_guard<std::mutex> lock_guard(mutex);
        check_result(queue.submit(1, &submit_info, fence), "queue submit failed");
        queue.waitIdle();
    }

  public:
    vk::Queue queue;
    std::mutex mutex;
};

} // namespace merian