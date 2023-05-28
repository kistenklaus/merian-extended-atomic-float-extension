#include "vk/extension/extension_resources.hpp"
#include "vk/memory/memory_allocator_vma.hpp"

namespace merian {

void ExtensionResources::on_context_created(const Context& context) {
    SPDLOG_DEBUG("create SamplerPool");
    _sampler_pool = new SamplerPool(context.device);

    SPDLOG_DEBUG("create Vulkan Memory Allocator");
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = context.physical_device;
    allocatorInfo.device = context.device;
    allocatorInfo.instance = context.instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &vma_allocator);

    SPDLOG_DEBUG("create VMAMemoryAllocator");
    _memory_allocator = new VMAMemoryAllocator(context.device, context.physical_device, vma_allocator);

    SPDLOG_DEBUG("create ResourceAllocator");
    _resource_allocator = new ResourceAllocator(context.device, context.physical_device, _memory_allocator, *_sampler_pool);
}

void ExtensionResources::on_destroy_context(const Context&) {
    SPDLOG_DEBUG("destroy ResourceAllocator");
    _resource_allocator->deinit();
    delete _resource_allocator;

    SPDLOG_DEBUG("destroy VMAMemoryAllocator");
    delete _memory_allocator;

    SPDLOG_DEBUG("destroy Vulkan Memory Allocator");
    vmaDestroyAllocator(vma_allocator);
    vma_allocator = VK_NULL_HANDLE;

    SPDLOG_DEBUG("destroy SamplerPool");
    delete _sampler_pool;
}

} // namespace merian