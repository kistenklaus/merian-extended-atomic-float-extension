#include "merian/vk/memory/resource_allocations.hpp"
#include "merian/vk/memory/memory_allocator.hpp"
#include "merian/vk/sampler/sampler_pool.hpp"

#include <spdlog/spdlog.h>
#include <vulkan/vulkan.hpp>

namespace merian {

Buffer::Buffer(const vk::Buffer& buffer,
               const MemoryAllocationHandle& memory,
               const vk::BufferUsageFlags& usage)
    : buffer(buffer), memory(memory), usage(usage) {
    SPDLOG_DEBUG("create buffer ({})", fmt::ptr(this));
}

Buffer::~Buffer() {
    SPDLOG_DEBUG("destroy buffer ({})", fmt::ptr(this));
    memory->get_context()->device.destroyBuffer(buffer);
}

vk::DeviceAddress Buffer::get_device_address() {
    assert(usage | vk::BufferUsageFlagBits::eShaderDeviceAddress);
    return memory->get_context()->device.getBufferAddress(get_buffer_device_address_info());
}

vk::BufferMemoryBarrier Buffer::buffer_barrier(const vk::AccessFlags src_access_flags,
                                               const vk::AccessFlags dst_access_flags,
                                               uint32_t src_queue_family_index,
                                               uint32_t dst_queue_family_index) {
    auto info = memory->get_memory_info();
    return {
        src_access_flags, dst_access_flags, src_queue_family_index, dst_queue_family_index, buffer,
        info.offset,      info.size};
}

// --------------------------------------------------------------------------

Image::Image(const vk::Image& image,
             const MemoryAllocationHandle& memory,
             const vk::Extent3D extent,
             const vk::ImageLayout current_layout)
    : image(image), memory(memory), extent(extent), current_layout(current_layout) {
    SPDLOG_DEBUG("create image ({})", fmt::ptr(this));
}

Image::~Image() {
    SPDLOG_DEBUG("destroy image ({})", fmt::ptr(this));
    memory->get_context()->device.destroyImage(image);
}

// Do not forget submite the barrier, else the internal state does not match the actual state
vk::ImageMemoryBarrier Image::transition_layout(const vk::ImageLayout new_layout,
                                                const vk::AccessFlags src_access_flags,
                                                const vk::AccessFlags dst_access_flags,
                                                const uint32_t src_queue_family_index,
                                                const uint32_t dst_queue_family_index,
                                                const vk::ImageAspectFlags aspect_flags,
                                                const uint32_t base_mip_level,
                                                const uint32_t mip_level_count,
                                                const uint32_t base_array_layer,
                                                const uint32_t array_layer_count) {

    vk::ImageMemoryBarrier barrier{
        src_access_flags,
        dst_access_flags,
        current_layout,
        new_layout,
        src_queue_family_index,
        dst_queue_family_index,
        image,
        {aspect_flags, base_mip_level, mip_level_count, base_array_layer, array_layer_count},
    };

    current_layout = new_layout;

    return barrier;
}

// --------------------------------------------------------------------------

Texture::Texture(const ImageHandle& image,
                 const vk::ImageViewCreateInfo& view_create_info,
                 const std::optional<SamplerHandle> sampler)
    : image(image), sampler(sampler) {
    SPDLOG_DEBUG("create texture ({})", fmt::ptr(this));
    view = image->get_memory()->get_context()->device.createImageView(view_create_info);
}

Texture::~Texture() {
    SPDLOG_DEBUG("destroy texture ({})", fmt::ptr(this));
    image->get_memory()->get_context()->device.destroyImageView(view);
}

void Texture::attach_sampler(const std::optional<SamplerHandle> sampler) {
    this->sampler = sampler;
}

AccelerationStructure::AccelerationStructure(const vk::AccelerationStructureKHR& as,
                                             const BufferHandle& buffer)
    : as(as), buffer(buffer) {
    SPDLOG_DEBUG("create acceleration structure ({})", fmt::ptr(this));
}

AccelerationStructure::~AccelerationStructure() {
    SPDLOG_DEBUG("destroy acceleration structure ({})", fmt::ptr(this));
    buffer->get_memory()->get_context()->device.destroyAccelerationStructureKHR(as);
}

vk::DeviceAddress AccelerationStructure::get_acceleration_structure_device_address() {
    vk::AccelerationStructureDeviceAddressInfoKHR address_info{as};
    return buffer->get_memory()->get_context()->device.getAccelerationStructureAddressKHR(
        address_info);
}

} // namespace merian