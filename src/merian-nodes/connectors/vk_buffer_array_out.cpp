#include "vk_buffer_array_out.hpp"
#include "vk_buffer_array_in.hpp"

#include "merian-nodes/graph/errors.hpp"
#include "merian-nodes/graph/node.hpp"
#include "merian/utils/pointer.hpp"

namespace merian_nodes {

BufferArrayOut::BufferArrayOut(const std::string& name, const uint32_t array_size)
    : TypedOutputConnector(name, false), buffers(array_size) {}

GraphResourceHandle BufferArrayOut::create_resource(
    [[maybe_unused]] const std::vector<std::tuple<NodeHandle, InputConnectorHandle>>& inputs,
    const ResourceAllocatorHandle& allocator,
    [[maybe_unused]] const ResourceAllocatorHandle& aliasing_allocator,
    [[maybe_unused]] const uint32_t resoruce_index,
    const uint32_t ring_size) {

    vk::PipelineStageFlags2 input_pipeline_stages;
    vk::AccessFlags2 input_access_flags;

    for (auto& [input_node, input] : inputs) {
        const auto& con_in = std::dynamic_pointer_cast<BufferArrayIn>(input);
        if (!con_in) {
            throw graph_errors::connector_error{
                fmt::format("VkImageOut {} cannot output to {}.", name, input->name)};
        }
        input_pipeline_stages |= con_in->pipeline_stages;
        input_access_flags |= con_in->access_flags;
    }

    return std::make_shared<BufferArrayResource>(buffers, ring_size, allocator->get_dummy_buffer(),
                                                 input_pipeline_stages, input_access_flags);
}

BufferArrayResource& BufferArrayOut::resource(const GraphResourceHandle& resource) {
    return *debugable_ptr_cast<BufferArrayResource>(resource);
}

Connector::ConnectorStatusFlags BufferArrayOut::on_pre_process(
    [[maybe_unused]] GraphRun& run,
    [[maybe_unused]] const vk::CommandBuffer& cmd,
    GraphResourceHandle& resource,
    [[maybe_unused]] const NodeHandle& node,
    [[maybe_unused]] std::vector<vk::ImageMemoryBarrier2>& image_barriers,
    [[maybe_unused]] std::vector<vk::BufferMemoryBarrier2>& buffer_barriers) {

    const auto& res = debugable_ptr_cast<BufferArrayResource>(resource);
    if (!res->current_updates.empty()) {
        res->pending_updates.clear();
        std::swap(res->pending_updates, res->current_updates);
        return NEEDS_DESCRIPTOR_UPDATE;
    }

    return {};
}

Connector::ConnectorStatusFlags BufferArrayOut::on_post_process(
    GraphRun& run,
    [[maybe_unused]] const vk::CommandBuffer& cmd,
    GraphResourceHandle& resource,
    [[maybe_unused]] const NodeHandle& node,
    [[maybe_unused]] std::vector<vk::ImageMemoryBarrier2>& image_barriers,
    [[maybe_unused]] std::vector<vk::BufferMemoryBarrier2>& buffer_barriers) {

    const auto& res = debugable_ptr_cast<BufferArrayResource>(resource);

    Connector::ConnectorStatusFlags flags{};
    if (!res->current_updates.empty()) {
        res->pending_updates.clear();
        std::swap(res->pending_updates, res->current_updates);
        flags |= NEEDS_DESCRIPTOR_UPDATE;
    }

    res->in_flight_buffers[run.get_in_flight_index()] = buffers;

    return flags;
}

BufferArrayOutHandle BufferArrayOut::create(const std::string& name, const uint32_t array_size) {
    return std::make_shared<BufferArrayOut>(name, array_size);
}

uint32_t BufferArrayOut::array_size() const {
    return buffers.size();
}

} // namespace merian_nodes
