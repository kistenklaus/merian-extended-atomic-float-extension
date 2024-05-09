#pragma once

#include "merian-nodes/graph/node.hpp"
#include "merian/vk/memory/resource_allocator.hpp"
#include "merian/vk/pipeline/pipeline.hpp"
#include "merian/vk/shader/shader_module.hpp"

#include <optional>

namespace merian {

class SVGFNode : public Node {
  private:
    struct VarianceEstimatePushConstant {
        float normal_reject_cos = 0.8;
        float depth_accept = 10; // larger reuses more
        float spatial_falloff = 3.0;
        float spatial_bias = 8.0;
    };

    struct FilterPushConstant {
        float param_z = 10; // parameter for depth      = 1   larger blurs more
        float param_n = .8; // parameter for normals    cos(alpha) for lower threshold
        float param_l = 8;  // parameter for brightness = 4   larger blurs more
        float z_bias_normals = -1.0;
        float z_bias_depth = -1.0;
    };

    struct TAAPushConstant {
        float blend_alpha = 0.0;
        float rejection_threshold = 1.0;
    };

  public:
    SVGFNode(const SharedContext context,
             const ResourceAllocatorHandle allocator,
             const std::optional<vk::Format> output_format = std::nullopt);

    ~SVGFNode();

    std::string name() override {
        return "SVGF";
    };

    std::tuple<std::vector<NodeInputDescriptorImage>, std::vector<NodeInputDescriptorBuffer>>
    describe_inputs() override;

    std::tuple<std::vector<NodeOutputDescriptorImage>, std::vector<NodeOutputDescriptorBuffer>>
    describe_outputs(
        const std::vector<NodeOutputDescriptorImage>& connected_image_outputs,
        const std::vector<NodeOutputDescriptorBuffer>& connected_buffer_outputs) override;

    void cmd_build(const vk::CommandBuffer& cmd, const std::vector<NodeIO>& ios) override;

    void cmd_process(const vk::CommandBuffer& cmd,
                     GraphRun& run,
                     const std::shared_ptr<FrameData>& frame_data,
                     const uint32_t set_index,
                     const NodeIO& io) override;

    void get_configuration(Configuration& config, bool& needs_rebuild) override;

  private:
    const SharedContext context;
    const ResourceAllocatorHandle allocator;
    const std::optional<vk::Format> output_format;

    // depends on available shared memory
    const uint32_t variance_estimate_local_size_x;
    const uint32_t variance_estimate_local_size_y;
    static constexpr uint32_t local_size_x = 32;
    static constexpr uint32_t local_size_y = 32;

    ShaderModuleHandle variance_estimate_module;
    ShaderModuleHandle filter_module;
    ShaderModuleHandle taa_module;

    VarianceEstimatePushConstant variance_estimate_pc;
    FilterPushConstant filter_pc;
    TAAPushConstant taa_pc;

    vk::ImageCreateInfo irr_create_info;

    PipelineHandle variance_estimate;
    std::vector<PipelineHandle> filters;
    PipelineHandle taa;

    uint32_t group_count_x;
    uint32_t group_count_y;

    int svgf_iterations = 0;

    std::vector<TextureHandle> graph_textures;
    std::vector<DescriptorSetHandle> graph_sets;
    DescriptorSetLayoutHandle graph_layout;
    DescriptorPoolHandle graph_pool;

    DescriptorSetLayoutHandle ping_pong_layout;
    DescriptorPoolHandle filter_pool;
    struct EAWRes {
        TextureHandle ping_pong;
        // Set reads from this resources and writes to i ^ 1
        DescriptorSetHandle set;
    };
    std::array<EAWRes, 2> ping_pong_res; // Ping pong sets

    int filter_variance = 0;
    int filter_type = 0;

    int taa_debug = 0;
    int taa_filter_prev = 0;
    int taa_clamping = 0;
    int taa_mv_sampling = 0;
};

} // namespace merian