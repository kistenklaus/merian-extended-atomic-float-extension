#include "merian/vk/graph/graph.hpp"
#include "merian/vk/utils/profiler.hpp"

namespace merian {

Graph::Graph(const SharedContext context,
             const ResourceAllocatorHandle allocator,
             const std::optional<QueueHandle> wait_queue)
    : context(context), allocator(allocator), wait_queue(wait_queue) {}

void Graph::add_node(const std::string name, const std::shared_ptr<Node>& node) {
    if (node_from_name.contains(name)) {
        throw std::invalid_argument{
            fmt::format("graph already contains a node with name '{}'", name)};
    }
    if (node_data.contains(node)) {
        throw std::invalid_argument{
            fmt::format("graph already contains this node with a different name '{}'", name)};
    }

    auto [image_inputs, buffer_inputs] = node->describe_inputs();
    node_from_name[name] = node;
    node_data[node] = {node, name, image_inputs, buffer_inputs};
    node_data[node].image_input_connections.resize(image_inputs.size());
    node_data[node].buffer_input_connections.resize(buffer_inputs.size());
}

void Graph::connect_image(const NodeHandle& src,
                          const NodeHandle& dst,
                          const uint32_t src_output,
                          const uint32_t dst_input) {
    if (src_output >= node_data[src].image_output_connections.size()) {
        node_data[src].image_output_connections.resize(src_output + 1);
    }
    // dst_input is valid
    if (dst_input >= node_data[dst].image_input_connections.size()) {
        throw std::invalid_argument{
            fmt::format("There is no input '{}' on node '{}'", dst_input, node_data[dst].name)};
    }
    if (std::get<0>(node_data[dst].image_input_connections[dst_input])) {
        throw std::invalid_argument{fmt::format("The input '{}' on node '{}' is already connected",
                                                dst_input, node_data[dst].name)};
    }
    node_data[dst].image_input_connections[dst_input] = {src, src_output};

    // make sure the same underlying resource is not accessed twice:
    // only images: Since they need layout transitions
    for (auto& [n, i] : node_data[src].image_output_connections[src_output]) {
        if (n == dst && node_data[dst].image_input_descriptors[i].delay ==
                            node_data[dst].image_input_descriptors[dst_input].delay) {
            throw std::invalid_argument{fmt::format(
                "You are trying to access the same underlying image of node '{}' twice from "
                "node '{}' with connections {} -> {}, {} -> {}: ",
                node_data[src].name, node_data[dst].name, src_output, i, src_output, dst_input)};
        }
    }
    node_data[src].image_output_connections[src_output].emplace_back(dst, dst_input);
}

void Graph::connect_buffer(const NodeHandle& src,
                           const NodeHandle& dst,
                           const uint32_t src_output,
                           const uint32_t dst_input) {
    if (src_output >= node_data[src].buffer_output_connections.size()) {
        node_data[src].buffer_output_connections.resize(src_output + 1);
    }
    // dst_input is valid
    assert(dst_input < node_data[dst].buffer_input_connections.size());
    // nothing is connected to this input
    assert(!std::get<0>(node_data[dst].buffer_input_connections[dst_input]));
    node_data[dst].buffer_input_connections[dst_input] = {src, src_output};
    node_data[src].buffer_output_connections[src_output].emplace_back(dst, dst_input);
}

void Graph::cmd_build(vk::CommandBuffer& cmd, const ProfilerHandle profiler) {
    // Make sure resources are not in use
    if (wait_queue.has_value()) {
        wait_queue.value()->wait_idle();
    } else {
        context->device.waitIdle();
    }

    reset_graph();

    if (node_data.empty())
        return;

    validate_inputs();

    // Visit nodes in topological order
    // to calculate outputs, barriers and such.
    // Feedback edges must have a delay of at least 1.
    flat_topology.resize(node_data.size());
    std::unordered_set<NodeHandle> visited;
    std::queue<NodeHandle> queue = start_nodes();

    uint32_t node_index = 0;
    while (!queue.empty()) {
        flat_topology[node_index] = queue.front();
        queue.pop();

        visited.insert(flat_topology[node_index]);
        calculate_outputs(flat_topology[node_index], visited, queue);
        log_connections(flat_topology[node_index]);

        node_index++;
    }
    // For some reason a node was not appended to the queue
    assert(node_index == node_data.size());
    allocate_outputs();
    prepare_resource_sets();

    for (auto& node : flat_topology) {
        MERIAN_PROFILE_SCOPE_GPU(profiler, cmd, node->name());
        cmd_build_node(cmd, node);
    }

    current_iteration = 0;
}

void Graph::cmd_run(vk::CommandBuffer& cmd, const std::shared_ptr<Profiler> profiler) {
    MERIAN_PROFILE_SCOPE_GPU(profiler, cmd, "Graph: run");

    {
        MERIAN_PROFILE_SCOPE(profiler, "Graph: pre process");
        Node::NodeStatus status;
        for (auto& node : flat_topology) {
            MERIAN_PROFILE_SCOPE(profiler, node->name());
            node->pre_process(status);
            rebuild_requested |= status.request_rebuild;
            status = {};
        }
    }

    if (rebuild_requested) {
        MERIAN_PROFILE_SCOPE_GPU(profiler, cmd, "Graph: build");
        cmd_build(cmd, profiler);
        rebuild_requested = false;
    }

    for (auto& node : flat_topology) {
        MERIAN_PROFILE_SCOPE_GPU(profiler, cmd, node->name());
        cmd_run_node(cmd, node);
    }

    current_iteration++;
}

void Graph::validate_inputs() {
    for (auto& [dst_node, dst_data] : node_data) {
        // Images
        for (uint32_t i = 0; i < dst_data.image_input_descriptors.size(); i++) {
            auto& [src_node, src_connection_idx] = dst_data.image_input_connections[i];
            auto& in_desc = dst_data.image_input_descriptors[i];
            if (src_node == nullptr) {
                throw std::runtime_error{
                    fmt::format("image input '{}' ({}) of node '{}' was not connected!",
                                in_desc.name, i, dst_data.name)};
            }
            if (src_node == dst_node && in_desc.delay == 0) {
                throw std::runtime_error{
                    fmt::format("node '{}'' is connected to itself with delay 0, maybe you want "
                                "to use a persistent output?",
                                dst_data.name)};
            }
        }
        // Buffers
        for (uint32_t i = 0; i < dst_data.buffer_input_descriptors.size(); i++) {
            auto& [src_node, src_connection_idx] = dst_data.buffer_input_connections[i];
            auto& in_desc = dst_data.buffer_input_descriptors[i];
            if (src_node == nullptr) {
                throw std::runtime_error{
                    fmt::format("buffer input {} ({}) of node {} was not connected!", in_desc.name,
                                i, dst_data.name)};
            }
            if (src_node == dst_node && in_desc.delay == 0) {
                throw std::runtime_error{
                    fmt::format("node {} is connected to itself with delay 0, maybe you want "
                                "to use a persistent output?",
                                dst_data.name)};
            }
        }
    }
}

std::queue<NodeHandle> Graph::start_nodes() {
    std::queue<NodeHandle> queue;

    // Find nodes without inputs or with delayed inputs only
    for (auto& [node, node_data] : node_data) {
        if (node_data.image_input_descriptors.empty() &&
            node_data.buffer_input_descriptors.empty()) {
            queue.push(node);
            continue;
        }
        uint32_t num_non_delayed = 0;
        for (auto& desc : node_data.image_input_descriptors) {
            if (desc.delay == 0)
                num_non_delayed++;
        }
        for (auto& desc : node_data.buffer_input_descriptors) {
            if (desc.delay == 0)
                num_non_delayed++;
        }

        if (num_non_delayed == 0)
            queue.push(node);
    }

    return queue;
}

void Graph::calculate_outputs(NodeHandle& node,
                              std::unordered_set<NodeHandle>& visited,
                              std::queue<NodeHandle>& queue) {
    NodeData& data = node_data[node];

    std::vector<NodeOutputDescriptorImage> connected_image_outputs;
    std::vector<NodeOutputDescriptorBuffer> connected_buffer_outputs;

    // find outputs that are connected to inputs.
    for (uint32_t i = 0; i < data.image_input_descriptors.size(); i++) {
        auto& [src_node, src_output_idx] = data.image_input_connections[i];
        auto& in_desc = data.image_input_descriptors[i];
        if (in_desc.delay > 0)
            connected_image_outputs.push_back(Node::FEEDBACK_OUTPUT_IMAGE);
        else
            connected_image_outputs.push_back(
                node_data[src_node].image_output_descriptors[src_output_idx]);
    }
    for (uint32_t i = 0; i < data.buffer_input_descriptors.size(); i++) {
        auto& [src_node, src_output_idx] = data.buffer_input_connections[i];
        auto& in_desc = data.buffer_input_descriptors[i];
        if (in_desc.delay > 0)
            connected_buffer_outputs.push_back(Node::FEEDBACK_OUTPUT_BUFFER);
        else
            connected_buffer_outputs.push_back(
                node_data[src_node].buffer_output_descriptors[src_output_idx]);
    }

    // get outputs from node
    std::tie(data.image_output_descriptors, data.buffer_output_descriptors) =
        node->describe_outputs(connected_image_outputs, connected_buffer_outputs);

    // validate that the user did not try to connect something from an non existent output,
    // since on connect we did not know the number of output descriptors
    if (data.image_output_connections.size() > data.image_output_descriptors.size()) {
        throw std::runtime_error{fmt::format("image output index '{}' is invalid for node '{}'",
                                             data.image_output_connections.size() - 1, data.name)};
    }
    if (data.buffer_output_connections.size() > data.buffer_output_descriptors.size()) {
        throw std::runtime_error{fmt::format("buffer output index '{}' is invalid for node '{}'",
                                             data.buffer_output_connections.size() - 1, data.name)};
    }
    data.image_output_connections.resize(data.image_output_descriptors.size());
    data.buffer_output_connections.resize(data.buffer_output_descriptors.size());

    // check for all subsequent nodes if we visited all "requirements" and add to queue.
    // also, fail if we see a node again! (in both cases exclude "feedback" edges)

    // find all subsequent nodes that are connected over a edge with delay = 0.
    std::unordered_set<NodeHandle> candidates;
    for (auto& output : data.image_output_connections) {
        for (auto& [dst_node, image_input_idx] : output) {
            if (node_data[dst_node].image_input_descriptors[image_input_idx].delay == 0) {
                candidates.insert(dst_node);
            }
        }
    }
    for (auto& output : data.buffer_output_connections) {
        for (auto& [dst_node, buffer_input_idx] : output) {
            if (node_data[dst_node].buffer_input_descriptors[buffer_input_idx].delay == 0) {
                candidates.insert(dst_node);
            }
        }
    }

    // add to queue if all "inputs" were visited
    for (const NodeHandle& candidate : candidates) {
        if (visited.contains(candidate)) {
            // Back-edges with delay > 1 are allowed!
            throw std::runtime_error{
                fmt::format("undelayed (edges with delay = 0) graph is not acyclic! {} -> {}",
                            data.name, node_data[candidate].name)};
        }
        bool satisfied = true;
        NodeData& candidate_data = node_data[candidate];
        for (auto& [src_node, src_output_idx] : candidate_data.image_input_connections) {
            satisfied &= visited.contains(src_node);
        }
        for (auto& [src_node, src_output_idx] : candidate_data.buffer_input_connections) {
            satisfied &= visited.contains(src_node);
        }
        if (satisfied) {
            queue.push(candidate);
        }
    }
}

void Graph::log_connections(NodeHandle& src) {
#ifdef NDEBUG
    return;
#endif

    NodeData& src_data = node_data[src];
    for (uint32_t i = 0; i < src_data.image_output_descriptors.size(); i++) {
        auto& src_out_desc = src_data.image_output_descriptors[i];
        auto& src_output = src_data.image_output_connections[i];
        for (auto& [dst_node, image_input_idx] : src_output) {
            NodeData& dst_data = node_data[dst_node];
            auto& dst_in_desc = dst_data.image_input_descriptors[image_input_idx];
            SPDLOG_DEBUG("image connection: {}({}) --{}-> {}({})", src_data.name, src_out_desc.name,
                         dst_in_desc.delay, dst_data.name, dst_in_desc.name);
        }
    }
    for (uint32_t i = 0; i < src_data.buffer_output_descriptors.size(); i++) {
        auto& src_out_desc = src_data.buffer_output_descriptors[i];
        auto& src_output = src_data.buffer_output_connections[i];
        for (auto& [dst_node, buffer_input_idx] : src_output) {
            NodeData& dst_data = node_data[dst_node];
            auto& dst_in_desc = dst_data.buffer_input_descriptors[buffer_input_idx];
            SPDLOG_DEBUG("buffer connection: {}({}) --{}-> {}({})", src_data.name,
                         src_out_desc.name, dst_in_desc.delay, dst_data.name, dst_in_desc.name);
        }
    }
}

void Graph::allocate_outputs() {
    for (auto& [src_node, src_data] : node_data) {
        // Buffers
        src_data.allocated_buffer_outputs.resize(src_data.buffer_output_descriptors.size());
        for (uint32_t src_out_idx = 0; src_out_idx < src_data.buffer_output_descriptors.size();
             src_out_idx++) {
            auto& out_desc = src_data.buffer_output_descriptors[src_out_idx];
            vk::BufferUsageFlags usage_flags = out_desc.create_info.usage;
            vk::PipelineStageFlags2 input_pipeline_stages;
            vk::AccessFlags2 input_access_flags;
            uint32_t max_delay = 0;
            for (auto& [dst_node, dst_input_idx] :
                 src_data.buffer_output_connections[src_out_idx]) {
                auto& in_desc = node_data[dst_node].buffer_input_descriptors[dst_input_idx];
                if (out_desc.persistent && in_desc.delay > 0) {
                    throw std::runtime_error{fmt::format(
                        "persistent outputs cannot be accessed with delay > 0. {}: {} -> {}: {}",
                        src_data.name, src_out_idx, node_data[dst_node].name, dst_input_idx)};
                }
                max_delay = std::max(max_delay, in_desc.delay);
                usage_flags |= in_desc.usage_flags;
                input_pipeline_stages |= in_desc.pipeline_stages;
                input_access_flags |= in_desc.access_flags;
            }
            // Create max_delay + 1 buffers
            for (uint32_t j = 0; j < max_delay + 1; j++) {
                BufferHandle buffer =
                    allocator->createBuffer(out_desc.create_info.size, usage_flags, NONE,
                                            fmt::format("node '{}' buffer, output '{}', copy '{}'",
                                                        src_data.name, out_desc.name, j));
                src_data.allocated_buffer_outputs[src_out_idx].emplace_back(
                    std::make_shared<BufferResource>(buffer, vk::PipelineStageFlagBits2::eTopOfPipe,
                                                     vk::AccessFlags2(), false,
                                                     input_pipeline_stages, input_access_flags));
            }
        }

        // Images
        src_data.allocated_image_outputs.resize(src_data.image_output_descriptors.size());
        for (uint32_t src_out_idx = 0; src_out_idx < src_data.image_output_descriptors.size();
             src_out_idx++) {
            auto& out_desc = src_data.image_output_descriptors[src_out_idx];
            vk::ImageCreateInfo create_info = out_desc.create_info;
            vk::PipelineStageFlags2 input_pipeline_stages;
            vk::AccessFlags2 input_access_flags;
            uint32_t max_delay = 0;
            for (auto& [dst_node, dst_input_idx] : src_data.image_output_connections[src_out_idx]) {
                auto& in_desc = node_data[dst_node].image_input_descriptors[dst_input_idx];
                if (out_desc.persistent && in_desc.delay > 0) {
                    throw std::runtime_error{fmt::format(
                        "persistent outputs cannot be accessed with delay > 0. {}: {} -> {}: {}",
                        src_data.name, src_out_idx, node_data[dst_node].name, dst_input_idx)};
                }
                max_delay = std::max(max_delay, in_desc.delay);
                create_info.usage |= in_desc.usage_flags;
                input_pipeline_stages |= in_desc.pipeline_stages;
                input_access_flags |= in_desc.access_flags;
            }
            // Create max_delay + 1 images
            for (uint32_t j = 0; j < max_delay + 1; j++) {
                ImageHandle image =
                    allocator->createImage(create_info, NONE,
                                           fmt::format("node '{}' image, output '{}', copy '{}'",
                                                       src_data.name, out_desc.name, j));
                src_data.allocated_image_outputs[src_out_idx].emplace_back(
                    std::make_shared<ImageResource>(image, vk::PipelineStageFlagBits2::eTopOfPipe,
                                                    vk::AccessFlags2(), false,
                                                    input_pipeline_stages, input_access_flags));
            }
        }
    }
}

void Graph::prepare_resource_sets() {
    for (auto& [dst_node, dst_data] : node_data) {
        // Find the lowest number of sets needed (lcm)
        std::vector<uint32_t> num_resources;

        // By checking how many copies of that resource exists in the sources
        for (auto& [src_node, src_output_idx] : dst_data.image_input_connections) {
            num_resources.push_back(
                node_data[src_node].allocated_image_outputs[src_output_idx].size());
        }
        for (auto& [src_node, src_output_idx] : dst_data.buffer_input_connections) {
            num_resources.push_back(
                node_data[src_node].allocated_buffer_outputs[src_output_idx].size());
        }
        // ...and how many output resources the node has
        for (auto& images : dst_data.allocated_image_outputs) {
            num_resources.push_back(images.size());
        }
        for (auto& buffers : dst_data.allocated_buffer_outputs) {
            num_resources.push_back(buffers.size());
        }

        // After this many iterations we can again use the first resource set
        uint32_t num_sets = lcm(num_resources);

        // Precompute resource sets for each iteration
        dst_data.precomputed_input_images.resize(num_sets);
        dst_data.precomputed_input_buffers.resize(num_sets);
        dst_data.precomputed_output_images.resize(num_sets);
        dst_data.precomputed_output_buffers.resize(num_sets);
        dst_data.precomputed_input_images_resource.resize(num_sets);
        dst_data.precomputed_input_buffers_resource.resize(num_sets);
        dst_data.precomputed_output_images_resource.resize(num_sets);
        dst_data.precomputed_output_buffers_resource.resize(num_sets);

        for (uint32_t set_idx = 0; set_idx < num_sets; set_idx++) {
            // Precompute inputs
            for (uint32_t i = 0; i < dst_data.image_input_descriptors.size(); i++) {
                auto& [src_node, src_output_idx] = dst_data.image_input_connections[i];
                auto& in_desc = dst_data.image_input_descriptors[i];
                const uint32_t num_resources =
                    node_data[src_node].allocated_image_outputs[src_output_idx].size();
                const uint32_t resource_idx =
                    (set_idx + num_resources - in_desc.delay) % num_resources;
                const auto& resource =
                    node_data[src_node].allocated_image_outputs[src_output_idx][resource_idx];
                dst_data.precomputed_input_images[set_idx].push_back(resource->image);
                dst_data.precomputed_input_images_resource[set_idx].push_back(resource);
            }
            for (uint32_t i = 0; i < dst_data.buffer_input_descriptors.size(); i++) {
                auto& [src_node, src_output_idx] = dst_data.buffer_input_connections[i];
                auto& in_desc = dst_data.buffer_input_descriptors[i];
                const uint32_t num_resources =
                    node_data[src_node].allocated_buffer_outputs[src_output_idx].size();
                const uint32_t resource_idx =
                    (set_idx + num_resources - in_desc.delay) % num_resources;
                const auto& resource =
                    node_data[src_node].allocated_buffer_outputs[src_output_idx][resource_idx];
                dst_data.precomputed_input_buffers[set_idx].push_back(resource->buffer);
                dst_data.precomputed_input_buffers_resource[set_idx].push_back(resource);
            }
            // Precompute outputs
            for (auto& images : dst_data.allocated_image_outputs) {
                dst_data.precomputed_output_images[set_idx].push_back(
                    images[set_idx % images.size()]->image);
                dst_data.precomputed_output_images_resource[set_idx].push_back(
                    images[set_idx % images.size()]);
            }
            for (auto& buffers : dst_data.allocated_buffer_outputs) {
                dst_data.precomputed_output_buffers[set_idx].push_back(
                    buffers[set_idx % buffers.size()]->buffer);
                dst_data.precomputed_output_buffers_resource[set_idx].push_back(
                    buffers[set_idx % buffers.size()]);
            }
        }
    }
}

void Graph::cmd_build_node(vk::CommandBuffer& cmd, NodeHandle& node) {
    NodeData& data = node_data[node];
    for (uint32_t set_idx = 0; set_idx < data.precomputed_input_images.size(); set_idx++) {
        cmd_barrier_for_node(cmd, data, set_idx);
    }
    node->cmd_build(cmd, data.precomputed_input_images, data.precomputed_input_buffers,
                    data.precomputed_output_images, data.precomputed_output_buffers);
}

// Insert the according barriers for that node
void Graph::cmd_run_node(vk::CommandBuffer& cmd, NodeHandle& node) {
    NodeData& data = node_data[node];
    uint32_t set_idx = current_iteration % data.precomputed_input_images.size();

    cmd_barrier_for_node(cmd, data, set_idx);

    auto& in_images = data.precomputed_input_images[set_idx];
    auto& in_buffers = data.precomputed_input_buffers[set_idx];
    auto& out_images = data.precomputed_output_images[set_idx];
    auto& out_buffers = data.precomputed_output_buffers[set_idx];

    node->cmd_process(cmd, current_iteration, set_idx, in_images, in_buffers, out_images,
                      out_buffers);
}

void Graph::cmd_barrier_for_node(vk::CommandBuffer& cmd, NodeData& data, uint32_t& set_idx) {
    image_barriers_for_set.clear();
    buffer_barriers_for_set.clear();

    auto& in_images_res = data.precomputed_input_images_resource[set_idx];
    auto& in_buffers_res = data.precomputed_input_buffers_resource[set_idx];

    // in-images
    for (uint32_t i = 0; i < data.image_input_descriptors.size(); i++) {
        auto& in_desc = data.image_input_descriptors[i];
        auto& res = in_images_res[i];
        if (res->last_used_as_output) {
            // Need to insert barrier and transition layout
            vk::ImageMemoryBarrier2 img_bar = res->image->barrier2(
                in_desc.required_layout, res->current_access_flags, res->input_access_flags,
                res->current_stage_flags, res->input_stage_flags);
            image_barriers_for_set.push_back(img_bar);
            res->current_stage_flags = res->input_stage_flags;
            res->current_access_flags = res->input_access_flags;
            res->last_used_as_output = false;
        } else {
            // No barrier required, if no transition required
            if (in_desc.required_layout != res->image->get_current_layout()) {
                vk::ImageMemoryBarrier2 img_bar = res->image->barrier2(
                    in_desc.required_layout, res->current_access_flags, res->current_access_flags,
                    res->current_stage_flags, res->current_stage_flags);
                image_barriers_for_set.push_back(img_bar);
            }
        }
    }
    // in-buffers
    for (uint32_t i = 0; i < data.buffer_input_descriptors.size(); i++) {
        auto& res = in_buffers_res[i];
        if (res->last_used_as_output) {
            vk::BufferMemoryBarrier2 buffer_bar{res->current_stage_flags,
                                                res->current_access_flags,
                                                res->input_stage_flags,
                                                res->input_access_flags,
                                                VK_QUEUE_FAMILY_IGNORED,
                                                VK_QUEUE_FAMILY_IGNORED,
                                                *res->buffer,
                                                0,
                                                VK_WHOLE_SIZE};
            buffer_barriers_for_set.push_back(buffer_bar);
            res->current_stage_flags = res->input_stage_flags;
            res->current_access_flags = res->input_access_flags;
            res->last_used_as_output = false;
        } // else nothing to do
    }

    auto& out_images_res = data.precomputed_output_images_resource[set_idx];
    auto& out_buffers_res = data.precomputed_output_buffers_resource[set_idx];

    // out-images
    for (uint32_t i = 0; i < data.image_output_descriptors.size(); i++) {
        auto& out_desc = data.image_output_descriptors[i];
        auto& res = out_images_res[i];
        // if not persistent: transition from undefined -> a bit faster
        vk::ImageMemoryBarrier2 img_bar = res->image->barrier2(
            out_desc.required_layout, res->current_access_flags, out_desc.access_flags,
            res->current_stage_flags, out_desc.pipeline_stages, VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED, all_levels_and_layers(), !out_desc.persistent);

        image_barriers_for_set.push_back(img_bar);
        res->current_stage_flags = out_desc.pipeline_stages;
        res->current_access_flags = out_desc.access_flags;
        res->last_used_as_output = true;
    }
    // out-buffers
    for (uint32_t i = 0; i < data.buffer_output_descriptors.size(); i++) {
        auto& out_desc = data.buffer_output_descriptors[i];
        auto& res = out_buffers_res[i];

        vk::BufferMemoryBarrier2 buffer_bar{res->current_stage_flags,
                                            res->current_access_flags,
                                            out_desc.pipeline_stages,
                                            out_desc.access_flags,
                                            VK_QUEUE_FAMILY_IGNORED,
                                            VK_QUEUE_FAMILY_IGNORED,
                                            *res->buffer,
                                            0,
                                            VK_WHOLE_SIZE};
        buffer_barriers_for_set.push_back(buffer_bar);
        res->current_stage_flags = out_desc.pipeline_stages;
        res->current_access_flags = out_desc.access_flags;
        res->last_used_as_output = true;
    }

    vk::DependencyInfoKHR dep_info{{}, {}, buffer_barriers_for_set, image_barriers_for_set};
    cmd.pipelineBarrier2(dep_info);
}

void Graph::reset_graph() {
    this->flat_topology.clear();
    for (auto& [node, data] : node_data) {

        data.image_output_descriptors.clear();
        data.buffer_output_descriptors.clear();

        data.allocated_image_outputs.clear();
        data.allocated_buffer_outputs.clear();

        data.precomputed_input_images.clear();
        data.precomputed_input_buffers.clear();
        data.precomputed_output_images.clear();
        data.precomputed_output_buffers.clear();

        data.precomputed_input_images_resource.clear();
        data.precomputed_input_buffers_resource.clear();
        data.precomputed_output_images_resource.clear();
        data.precomputed_output_buffers_resource.clear();
    }
}

} // namespace merian