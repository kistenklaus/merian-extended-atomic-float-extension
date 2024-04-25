#include "image_write.hpp"
#include "ext/stb_image_write.h"
#include "merian/vk/graph/graph.hpp"
#include "merian/vk/utils/blits.hpp"
#include <filesystem>

namespace merian {

#define FORMAT_PNG 0
#define FORMAT_JPG 1
#define FORMAT_HDR 2

ImageWriteNode::ImageWriteNode(
    const SharedContext context,
    const ResourceAllocatorHandle allocator,
    const std::string& filename_format =
        "image_{record_iteration:06}_{image_index:06}_{run_iteration:06}")
    : context(context), allocator(allocator), filename_format(filename_format), buf(1024) {
    assert(filename_format.size() < buf.size());
    std::copy(filename_format.begin(), filename_format.end(), buf.begin());
}

ImageWriteNode::~ImageWriteNode() {}

std::string ImageWriteNode::name() {
    return "Image Write";
}

// Declare the inputs that you require
std::tuple<std::vector<NodeInputDescriptorImage>, std::vector<NodeInputDescriptorBuffer>>
ImageWriteNode::describe_inputs() {
    return {
        {
            NodeInputDescriptorImage::transfer_src("src"),
        },
        {},
    };
}

void ImageWriteNode::record() {
    record_enable = true;
    needs_rebuild |= rebuild_on_record;
    this->iteration = 1;
    if (callback_on_record && callback)
        callback();
}

void ImageWriteNode::pre_process([[maybe_unused]] const uint64_t& run_iteration,
                                 [[maybe_unused]] NodeStatus& status) {
    if (!record_enable && ((int64_t)run_iteration == trigger_run)) {
        record();
    }
    status.request_rebuild = needs_rebuild;
    needs_rebuild = false;
};

void ImageWriteNode::cmd_process([[maybe_unused]] const vk::CommandBuffer& cmd,
                                 [[maybe_unused]] GraphRun& run,
                                 [[maybe_unused]] const uint32_t set_index,
                                 [[maybe_unused]] const std::vector<ImageHandle>& image_inputs,
                                 [[maybe_unused]] const std::vector<BufferHandle>& buffer_inputs,
                                 [[maybe_unused]] const std::vector<ImageHandle>& image_outputs,
                                 [[maybe_unused]] const std::vector<BufferHandle>& buffer_outputs) {
    if (filename_format.empty()) {
        return;
    }

    if (record_next || (record_enable && record_iteration == iteration)) {

        vk::Format format = this->format == FORMAT_HDR ? vk::Format::eR32G32B32A32Sfloat
                                                       : vk::Format::eR8G8B8A8Srgb;

        vk::ImageCreateInfo size_compatible_info{
            {},
            vk::ImageType::e2D,
            format,
            image_inputs[0]->get_extent(),
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc,
            vk::SharingMode::eExclusive,
            {},
            {},
            vk::ImageLayout::eUndefined,
        };
        ImageHandle image = allocator->createImage(size_compatible_info);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                            vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                            image->barrier(vk::ImageLayout::eTransferDstOptimal, {},
                                           vk::AccessFlagBits::eTransferWrite));
        cmd_blit_stretch(cmd, *image_inputs[0], image_inputs[0]->get_current_layout(),
                         image_inputs[0]->get_extent(), *image,
                         vk::ImageLayout::eTransferDstOptimal, image_inputs[0]->get_extent());
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
            image->barrier(vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits::eTransferWrite,
                           vk::AccessFlagBits::eTransferRead));

        vk::ImageCreateInfo linear_info{
            {},
            vk::ImageType::e2D,
            format,
            image_inputs[0]->get_extent(),
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eLinear,
            vk::ImageUsageFlagBits::eTransferDst,
            vk::SharingMode::eExclusive,
            {},
            {},
            vk::ImageLayout::eUndefined,
        };
        ImageHandle linear_image =
            allocator->createImage(linear_info, MemoryMappingType::HOST_ACCESS_RANDOM);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                            vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                            linear_image->barrier(vk::ImageLayout::eTransferDstOptimal, {},
                                                  vk::AccessFlagBits::eTransferWrite));
        cmd.copyImage(*image, image->get_current_layout(), *linear_image,
                      linear_image->get_current_layout(),
                      vk::ImageCopy(first_layer(), {}, first_layer(), {}, image->get_extent()));
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, {}, {},
            linear_image->barrier(vk::ImageLayout::eGeneral, vk::AccessFlagBits::eTransferWrite,
                                  vk::AccessFlagBits::eHostRead));

        int it = iteration;
        int run_it = run.get_iteration();
        int image_index = this->image_index++;
        std::string filename_format = this->filename_format;
        run.add_submit_callback([this, image, linear_image, it, image_index,
                                 run_it, filename_format](const QueueHandle& queue) {
            queue->wait_idle();
            void* mem = linear_image->get_memory()->map();

            std::filesystem::path path = std::filesystem::absolute(
                fmt::format(fmt::runtime(filename_format), fmt::arg("record_iteration", it),
                            fmt::arg("image_index", image_index), fmt::arg("run_iteration", run_it),
                            fmt::arg("width", linear_image->get_extent().width),
                            fmt::arg("height", linear_image->get_extent().height)));
            std::filesystem::create_directories(path.parent_path());
            const std::string tmp_filename = path.parent_path() / (".interm_" + path.filename().string());

            switch (this->format) {
            case FORMAT_PNG: {
                path += ".png";
                stbi_write_png(tmp_filename.c_str(), linear_image->get_extent().width,
                               linear_image->get_extent().height, 4, mem,
                               linear_image->get_extent().width * 4);
                break;
            }
            case FORMAT_JPG: {
                path += ".jpg";
                stbi_write_jpg(tmp_filename.c_str(), linear_image->get_extent().width,
                               linear_image->get_extent().height, 4, mem, 100);
                break;
            }
            case FORMAT_HDR: {
                path += ".hdr";
                stbi_write_hdr(tmp_filename.c_str(), linear_image->get_extent().width,
                               linear_image->get_extent().height, 4, static_cast<float*>(mem));
                break;
            }
            }

            try {
                std::filesystem::rename(tmp_filename, path);
            } catch (std::filesystem::filesystem_error const&) {
                SPDLOG_WARN("rename failed! Falling back to copy...");
                std::filesystem::copy(tmp_filename, path);
                std::filesystem::remove(tmp_filename);
            }

            linear_image->get_memory()->unmap();
        });

        if (rebuild_after_capture)
            run.request_rebuild();
        if (callback_after_capture && callback)
            callback();
        record_next = false;

        record_iteration *= record_enable ? it_power : 1;
        record_iteration += record_enable ? it_offset : 0;
    }

    iteration++;
}

void ImageWriteNode::get_configuration([[maybe_unused]] Configuration& config, bool&) {
    config.st_separate("General");
    config.config_options("format", format, {"PNG", "JPG", "HDR"},
                          Configuration::OptionsStyle::COMBO);
    config.config_bool("rebuild after capture", rebuild_after_capture,
                       "forces a graph rebuild after every capture");
    config.config_bool("rebuild on record", rebuild_on_record, "Rebuilds when recording starts");
    config.config_bool("callback after capture", callback_after_capture,
                       "calls the on_record callback after every capture");
    config.config_bool("callback on record", callback_on_record,
                       "calls the callback when the recording starts");
    if (config.config_text("filename", buf.size(), buf.data(), false,
                           "Provide a format string for the path. Supported variables are: "
                           "record_iteration, run_iteration, image_index, width, height")) {
        filename_format = buf.data();
    }
    config.output_text(
        fmt::format("abs path: {}", filename_format.empty()
                                        ? "<invalid>"
                                        : std::filesystem::absolute(filename_format).string()));

    config.st_separate("Single");
    record_next = config.config_bool("trigger");

    config.st_separate("Multiple");
    config.output_text(fmt::format("current iteration: {}",
                                   record_enable ? fmt::to_string(iteration) : "stopped"));
    const bool old_record_enable = record_enable;
    config.config_bool("enable", record_enable);
    if (record_enable && old_record_enable != record_enable)
        record();
    config.config_int("run trigger", trigger_run,
                      "The specified run starts recording and resets the iteration and calls the "
                      "configured callback and forces a rebuild if enabled.");

    config.st_separate();

    config.config_int(
        "iteration", record_iteration,
        "Save the result of of the the specified iteration. Iterations are 1-indexed.");
    record_iteration = std::max(record_iteration, 0);

    config.config_int("iteration power", it_power,
                      "Multiplies the iteration specifier with this value after every capture");
    config.config_int("iteration offset", it_offset,
                      "Adds this value to the iteration specifier after every capture. (After "
                      "applying the power).");
    config.output_text("note: Iterations are 1-indexed");
}

void ImageWriteNode::set_callback(const std::function<void()> callback) {
    this->callback = callback;
}

} // namespace merian
