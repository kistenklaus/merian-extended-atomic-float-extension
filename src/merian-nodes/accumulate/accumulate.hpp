#pragma once

#include "merian-nodes/compute_node/compute_node.hpp"
#include "merian/vk/memory/resource_allocator.hpp"
#include "merian/vk/pipeline/pipeline.hpp"

namespace merian {

class AccumulateNode : public ComputeNode {

  private:
    static constexpr uint32_t local_size_x = 16;
    static constexpr uint32_t local_size_y = 16;

    struct AccumulatePushConstant {
        float accum_alpha = 1.0;
        float accum_max_hist = INFINITY;
        float normal_reject_rad = 0.8;
        float depth_reject_percent = 0.05;
        int filter_mode = 1;
    };

  public:
    AccumulateNode(const SharedContext context, const ResourceAllocatorHandle alloc);

    ~AccumulateNode();

    std::string name() override;

    std::tuple<std::vector<NodeInputDescriptorImage>, std::vector<NodeInputDescriptorBuffer>>
    describe_inputs() override;

    std::tuple<std::vector<NodeOutputDescriptorImage>, std::vector<NodeOutputDescriptorBuffer>>
    describe_outputs(
        const std::vector<NodeOutputDescriptorImage>& connected_image_outputs,
        const std::vector<NodeOutputDescriptorBuffer>& connected_buffer_outputs) override;

    SpecializationInfoHandle get_specialization_info() const noexcept override;

    const void* get_push_constant() override;

    std::tuple<uint32_t, uint32_t, uint32_t> get_group_count() const noexcept override;

    ShaderModuleHandle get_shader_module() override;

    void get_configuration(Configuration& config) override;

  private:
    vk::Extent3D extent;
    AccumulatePushConstant pc;
    ShaderModuleHandle shader;
};

} // namespace merian
