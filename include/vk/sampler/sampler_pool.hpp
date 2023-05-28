#pragma once

#include "utils/vector_utils.hpp"

#include <cfloat>
#include <vulkan/vulkan.hpp>

#include <functional>
#include <unordered_map>
#include <vector>

namespace merian {
//////////////////////////////////////////////////////////////////////////
/**
  \class merian::SamplerPool

  This merian::SamplerPool class manages unique VkSampler objects. To minimize the total
  number of sampler objects, this class ensures that identical configurations
  return the same sampler

  Example :
  \code{.cpp}
  merian::SamplerPool pool(device);

  for (auto it : textures) {
    VkSamplerCreateInfo info = {...};

    // acquire ensures we create the minimal subset of samplers
    it.sampler = pool.acquireSampler(info);
  }

  // you can manage releases individually, or just use deinit/destructor of pool
  for (auto it : textures) {
    pool.releaseSampler(it.sampler);
  }
  \endcode

*/

class SamplerPool {
  public:
    SamplerPool(SamplerPool const&) = delete;
    SamplerPool& operator=(SamplerPool const&) = delete;
    SamplerPool() = delete;

    SamplerPool(VkDevice device) : device(device) {}
    ~SamplerPool();

    /* creates a new sampler or re-uses an existing one with ref-count
     * createInfo may contain VkSamplerReductionModeCreateInfo and VkSamplerYcbcrConversionCreateInfo
     */
    vk::Sampler acquireSampler(const vk::SamplerCreateInfo& createInfo);
    // decrements ref-count and destroys sampler if possible
    void releaseSampler(vk::Sampler sampler);

  private:
    struct SamplerState {
        vk::SamplerCreateInfo createInfo;
        vk::SamplerReductionModeCreateInfo reduction;
        vk::SamplerYcbcrConversionCreateInfo ycbr;

        SamplerState() {}
        bool operator==(const SamplerState& other) const = default;
    };

    struct Chain {
        VkStructureType sType;
        const Chain* pNext;
    };

    struct Entry {
        vk::Sampler sampler = VK_NULL_HANDLE;
        uint32_t nextFreeIndex = ~0;
        uint32_t refCount = 0;
        SamplerState state;
    };

    vk::Device device = nullptr;
    uint32_t freeIndex = ~0;
    std::vector<Entry> entries;

    std::unordered_map<SamplerState, uint32_t, HashAligned32<SamplerState>> state_map;
    std::unordered_map<VkSampler, uint32_t> sampler_map;
};

} // namespace merian
