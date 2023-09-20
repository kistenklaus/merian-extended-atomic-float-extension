#include "accumulate.hpp"
#include "merian/vk/descriptors/descriptor_set_layout_builder.hpp"
#include "merian/vk/descriptors/descriptor_set_update.hpp"
#include "merian/vk/graph/graph.hpp"
#include "merian/vk/pipeline/pipeline_compute.hpp"
#include "merian/vk/pipeline/pipeline_layout_builder.hpp"
#include "merian/vk/pipeline/specialization_info_builder.hpp"
#include "merian/vk/shader/shader_module.hpp"

static const uint32_t spv[] = {
#include "accumulate.comp.spv.h"
};

namespace merian {

AccumulateNode::AccumulateNode(const SharedContext context,
                               const ResourceAllocatorHandle allocator,
                               const vk::Format format)
    : ComputeNode(context, allocator, sizeof(AccumulatePushConstant)), format(format) {
    shader = std::make_shared<ShaderModule>(context, sizeof(spv), spv);
}

AccumulateNode::~AccumulateNode() {}

std::string AccumulateNode::name() {
    return "Accumulate";
}

std::tuple<std::vector<NodeInputDescriptorImage>, std::vector<NodeInputDescriptorBuffer>>
AccumulateNode::describe_inputs() {
    return {
        {
            NodeInputDescriptorImage::compute_read("prev_accum", 1),
            NodeInputDescriptorImage::compute_read("prev_moments", 1),

            NodeInputDescriptorImage::compute_read("irr"),

            NodeInputDescriptorImage::compute_read("mv"),
            NodeInputDescriptorImage::compute_read("moments_in"),
        },
        {
            NodeInputDescriptorBuffer::compute_read("gbuf"),
            NodeInputDescriptorBuffer::compute_read("prev_gbuf", 1),
        },
    };
}

std::tuple<std::vector<NodeOutputDescriptorImage>, std::vector<NodeOutputDescriptorBuffer>>
AccumulateNode::describe_outputs(
    const std::vector<NodeOutputDescriptorImage>& connected_image_outputs,
    const std::vector<NodeOutputDescriptorBuffer>&) {
    extent = connected_image_outputs[2].create_info.extent;

    // clang-format off
    return {
        {
            NodeOutputDescriptorImage::compute_read_write("accum", format, extent),
            NodeOutputDescriptorImage::compute_read_write("moments_accum", vk::Format::eR32G32Sfloat, extent),
        },
        {},
    };
    // clang-format on
}

SpecializationInfoHandle AccumulateNode::get_specialization_info() const noexcept {
    auto spec_builder = SpecializationInfoBuilder();
    spec_builder.add_entry(local_size_x, local_size_y, filter_mode, extended_search, reuse_border, firefly_clamp);
    return spec_builder.build();
}

const void* AccumulateNode::get_push_constant([[maybe_unused]] GraphRun& run) {
    pc.clear = run.get_iteration() == 0 || clear;
    clear = false;
    return &pc;
}

std::tuple<uint32_t, uint32_t, uint32_t> AccumulateNode::get_group_count() const noexcept {
    return {(extent.width + local_size_x - 1) / local_size_x,
            (extent.height + local_size_y - 1) / local_size_y, 1};
};

ShaderModuleHandle AccumulateNode::get_shader_module() {
    return shader;
}

void AccumulateNode::get_configuration(Configuration& config, bool& needs_rebuild) {
    config.st_separate("History");
    config.config_float("alpha", pc.accum_alpha, 0, 1,
                        "Blend factor with the previous information. More means more reuse");
    config.config_float("max history", pc.accum_max_hist,
                        "artificially limit the history counter. This can be a good alternative to "
                        "reducing the blend alpha");
    config.st_no_space();
    pc.accum_max_hist = config.config_bool("inf history") ? INFINITY : pc.accum_max_hist;

    config.st_separate("Reproject");
    float angle = glm::acos(pc.normal_reject_cos);
    config.config_angle("normal threshold", angle, "Reject points with normals farther apart", 0,
                        180);
    pc.normal_reject_cos = glm::cos(angle);
    config.config_percent("depth threshold", pc.depth_reject_percent,
                          "Reject points with depths farther apart (relative to the max)");
    int old_filter_mode = filter_mode;
    config.config_options("filter mode", filter_mode, {"nearest", "linear"});
    needs_rebuild |= old_filter_mode != filter_mode;

    int old_extended_search = extended_search;
    int old_reuse_border = reuse_border;
    config.config_bool("extended search", extended_search,
                       "search in a 3x3 radius with weakened rejection thresholds for valid "
                       "information if nothing was found. Helps "
                       "with artifacts at edges");
    config.config_bool("reuse border", reuse_border,
                       "Reuse border information (if valid) for pixel where the motion vector "
                       "points outside of the image. Can lead to smearing.");
    needs_rebuild |= old_extended_search != extended_search || old_reuse_border != reuse_border;


    config.st_separate("Other");
    clear = config.config_bool("clear");
    const float old_firefly_clamp = firefly_clamp;
    config.config_float("firefly clamp", firefly_clamp, "DANGER: Introduces bias", 0.1);
    config.st_no_space();
    if (config.config_bool("inf clamp"))
        firefly_clamp = INFINITY;
    needs_rebuild |= old_firefly_clamp != firefly_clamp;
}

void AccumulateNode::request_clear() {
    clear = true;
}

} // namespace merian
