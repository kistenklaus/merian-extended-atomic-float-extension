#pragma once

#include "merian-nodes/connectors/host_ptr_in.hpp"
#include "merian-nodes/graph/connector_output.hpp"
#include "merian-nodes/graph/errors.hpp"
#include "merian-nodes/graph/node.hpp"

#include <memory>

namespace merian_nodes {

template <typename T> class HostPtrOut;
template <typename T> using HostPtrOutHandle = std::shared_ptr<HostPtrOut<T>>;

// Transfer information between nodes on the host using shared_ptr.
template <typename T> class HostPtrOut : public TypedOutputConnector<std::shared_ptr<T>&> {

  public:
    HostPtrOut(const std::string& name, const bool persistent)
        : TypedOutputConnector<std::shared_ptr<T>&>(name, !persistent), persistent(persistent) {}

    GraphResourceHandle
    create_resource(const std::vector<std::tuple<NodeHandle, InputConnectorHandle>>& inputs,
                    [[maybe_unused]] const ResourceAllocatorHandle& allocator,
                    [[maybe_unused]] const ResourceAllocatorHandle& aliasing_allocator) override {

        for (auto& [node, input] : inputs) {
            // check compatibility
            const auto& casted_in = std::dynamic_pointer_cast<HostPtrIn<T>>(input);
            if (!casted_in) {
                throw graph_errors::connector_error{
                    fmt::format("HostPtrOut {} cannot output to {} of node {}.", Connector::name,
                                input->name, node->name)};
            }
        }

        return std::make_shared<HostPtrResource<T>>(persistent ? -1 : (int32_t)inputs.size());
    }

    std::shared_ptr<T>& resource(const GraphResourceHandle& resource) override {
        return debugable_ptr_cast<HostPtrResource<T>>(resource)->ptr;
    }

    Connector::ConnectorStatusFlags on_pre_process(
        [[maybe_unused]] GraphRun& run,
        [[maybe_unused]] const vk::CommandBuffer& cmd,
        GraphResourceHandle& resource,
        [[maybe_unused]] const NodeHandle& node,
        [[maybe_unused]] std::vector<vk::ImageMemoryBarrier2>& image_barriers,
        [[maybe_unused]] std::vector<vk::BufferMemoryBarrier2>& buffer_barriers) override {
        const auto& res = debugable_ptr_cast<HostPtrResource<T>>(resource);
        if (!persistent) {
            res->ptr.reset();
        }

        return {};
    }

    Connector::ConnectorStatusFlags on_post_process(
        [[maybe_unused]] GraphRun& run,
        [[maybe_unused]] const vk::CommandBuffer& cmd,
        GraphResourceHandle& resource,
        [[maybe_unused]] const NodeHandle& node,
        [[maybe_unused]] std::vector<vk::ImageMemoryBarrier2>& image_barriers,
        [[maybe_unused]] std::vector<vk::BufferMemoryBarrier2>& buffer_barriers) override {
        const auto& res = debugable_ptr_cast<HostPtrResource<T>>(resource);
        if (!res->ptr) {
            throw graph_errors::connector_error{fmt::format(
                "Node {} did not set the resource for output {}.", node->name, Connector::name)};
        }
        res->processed_inputs = 0;

        return {};
    }

  public:
    static HostPtrOutHandle<T> create(const std::string& name, const bool persistent = false) {
        return std::make_shared<HostPtrOut<T>>(name, persistent);
    }

  private:
    const bool persistent;
};

} // namespace merian_nodes
