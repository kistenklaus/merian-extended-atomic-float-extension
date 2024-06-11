#pragma once

#include "merian-nodes/graph/resource.hpp"

namespace merian_nodes {

template <typename T> class PtrResource : public GraphResource {
    template <typename> friend class PtrOut;
    template <typename> friend class PtrIn;

  public:
    PtrResource(const int32_t num_inputs) : num_inputs(num_inputs) {}

  private:
    const int32_t num_inputs;

    // reset after output, increased after input
    // if processed_inputs == num_inputs reset ptr if output is not persistent.
    int32_t processed_inputs;

    std::shared_ptr<T> ptr;
};

} // namespace merian_nodes
