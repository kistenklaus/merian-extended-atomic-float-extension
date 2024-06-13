#include "any_out.hpp"
#include "any_in.hpp"

#include "merian-nodes/resources/host_any_resource.hpp"
#include "merian-nodes/graph/errors.hpp"

namespace merian_nodes {

AnyOut::AnyOut(const std::string& name, const bool persistent)
    : TypedOutputConnector(name, !persistent), persistent(persistent) {}

GraphResourceHandle AnyOut::create_resource(
    const std::vector<std::tuple<NodeHandle, InputConnectorHandle>>& inputs,
    [[maybe_unused]] const ResourceAllocatorHandle& allocator,
    [[maybe_unused]] const ResourceAllocatorHandle& aliasing_allocator) {

    for (auto& [node, input] : inputs) {
        // check compatibility

        const auto& casted_in = std::dynamic_pointer_cast<AnyIn>(input);
        if (!casted_in) {
            throw graph_errors::connector_error{
                fmt::format("AnyOut {} cannot output to {} of node {}.", Connector::name,
                            input->name, node->name)};
        }
    }

    return std::make_shared<AnyResource>(persistent ? -1 : (int32_t)inputs.size());
}

std::any& AnyOut::resource(const GraphResourceHandle& resource) {
    return debugable_ptr_cast<AnyResource>(resource)->any;
}

Connector::ConnectorStatusFlags AnyOut::on_pre_process(
    [[maybe_unused]] GraphRun& run,
    [[maybe_unused]] const vk::CommandBuffer& cmd,
    GraphResourceHandle& resource,
    [[maybe_unused]] const NodeHandle& node,
    [[maybe_unused]] std::vector<vk::ImageMemoryBarrier2>& image_barriers,
    [[maybe_unused]] std::vector<vk::BufferMemoryBarrier2>& buffer_barriers) {
    const auto& res = debugable_ptr_cast<AnyResource>(resource);
    if (!persistent) {
        res->any.reset();
    }

    return {};
}

Connector::ConnectorStatusFlags AnyOut::on_post_process(
    [[maybe_unused]] GraphRun& run,
    [[maybe_unused]] const vk::CommandBuffer& cmd,
    GraphResourceHandle& resource,
    [[maybe_unused]] const NodeHandle& node,
    [[maybe_unused]] std::vector<vk::ImageMemoryBarrier2>& image_barriers,
    [[maybe_unused]] std::vector<vk::BufferMemoryBarrier2>& buffer_barriers) {
    const auto& res = debugable_ptr_cast<AnyResource>(resource);
    if (!res->any.has_value()) {
        throw graph_errors::connector_error{fmt::format(
            "Node {} did not set the resource for output {}.", node->name, Connector::name)};
    }
    res->processed_inputs = 0;

    return {};
}

AnyOutHandle AnyOut::create(const std::string& name, const bool persistent) {
    return std::make_shared<AnyOut>(name, persistent);
}

} // namespace merian_nodes
