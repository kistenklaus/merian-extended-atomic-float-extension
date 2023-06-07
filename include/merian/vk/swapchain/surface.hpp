#pragma once

#include "merian/vk/swapchain/glfw_window.hpp"

#include "merian/vk/context.hpp"
#include <GLFW/glfw3.h>

#include <memory>
#include <spdlog/spdlog.h>

namespace merian {

class Surface : public std::enable_shared_from_this<Surface> {

  public:
    // Manage the supplied surface. The surface is destroyed when this window is destroyed.
    Surface(const SharedContext& context, vk::SurfaceKHR& surface)
        : context(context), surface(surface) {}

    Surface(const SharedContext& context, GLFWWindowHandle window) : context(context) {
        SPDLOG_DEBUG("create surface ({})", fmt::ptr(this));

        auto psurf = VkSurfaceKHR(surface);
        if (glfwCreateWindowSurface(context->instance, *window, NULL, &psurf))
            throw std::runtime_error("Surface creation failed!");
        surface = vk::SurfaceKHR(psurf);
    }

    ~Surface() {
        SPDLOG_DEBUG("destroy surface ({})", fmt::ptr(this));
        context->instance.destroySurfaceKHR(surface);
    }

    operator const vk::SurfaceKHR&() const {
        return surface;
    }

    const vk::SurfaceKHR& get_surface() const {
        return surface;
    }

  private:
    const SharedContext context;
    vk::SurfaceKHR surface;
};

using SurfaceHandle = std::shared_ptr<Surface>;

} // namespace merian
