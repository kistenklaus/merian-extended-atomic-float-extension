#pragma once

#include "merian/vk/window/glfw_window.hpp"
#include "merian/vk/window/surface.hpp"

#include <spdlog/spdlog.h>

namespace merian {

inline vk::SurfaceKHR surface_from_glfw_window(const SharedContext& context,
                                               const GLFWWindowHandle& window) {
    VkSurfaceKHR psurf;
    if (glfwCreateWindowSurface(context->instance, *window, NULL, &psurf))
        throw std::runtime_error("Surface creation failed!");
    return vk::SurfaceKHR(psurf);
}

class GLFWSurface : public Surface {
    friend class ExtensionVkGLFW;

  private:
    // Manage the supplied surface. The surface is destroyed when this window is destroyed.
    GLFWSurface(const SharedContext& context,
                const vk::SurfaceKHR& surface,
                const GLFWWindowHandle& window)
        : Surface(context, surface), window(window) {}

  public:
    GLFWSurface(const SharedContext& context, const GLFWWindowHandle& window)
        : Surface(context, surface_from_glfw_window(context, window)), window(window) {
        SPDLOG_DEBUG("create surface ({})", fmt::ptr(this));
    }

  private:
    const GLFWWindowHandle window;
};

} // namespace merian
