#pragma once

#include "merian-nodes/connectors/managed_vk_image_out.hpp"
#include "merian-nodes/graph/node.hpp"

#include <filesystem>

namespace merian_nodes {

class HDRImageRead : public Node {

  public:
    // By default images are interpretet as sRGB, turn on linear if you want to load images for
    // normal maps, depth, and such.
    //
    // Set keep_on_host to keep a copy in host memory, otherwise the image is reloaded from disk
    // everytime the graph reconnects.
    HDRImageRead(const SharedContext& context);

    ~HDRImageRead();

    std::vector<OutputConnectorHandle>
    describe_outputs(const ConnectorIOMap& output_for_input) override;

    void process(GraphRun& run,
                 const vk::CommandBuffer& cmd,
                 const DescriptorSetHandle& descriptor_set,
                 const NodeIO& io) override;

    NodeStatusFlags properties(Properties& config) override;

  private:
    const SharedContext context;
    bool keep_on_host = false;

    ManagedVkImageOutHandle con_out;

    // can be nullptr when image is unloaded.
    float* image{nullptr};
    bool needs_run = true;

    int width, height, channels;
    std::filesystem::path filename;
    std::array<char, 256> config_filename;
};

} // namespace merian_nodes
