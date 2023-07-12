#pragma once

#include "merian/utils/configuration.hpp"

#include <string>
#include <vector>

namespace merian {

// A configuration recorder to display and set configuration from ImGui.
class ImGuiConfiguration : public Configuration {
  public:
    virtual ~ImGuiConfiguration() override;

    virtual bool st_begin_child(const std::string& id, const std::string& label = "") override;
    virtual void st_end_child() override;

    virtual bool st_new_section(const std::string& label = "") override;
    virtual void st_separate(const std::string& label = "") override;
    virtual void st_no_space() override;

    virtual void output_text(const std::string& text) override;
    virtual void output_plot_line(const std::string& label,
                                  const std::vector<float>& samples,
                                  const float scale_min,
                                  const float scale_max) override;

    virtual void
    config_color(const std::string& id, glm::vec3& color, const std::string& desc = "") override;
    virtual void
    config_color(const std::string& id, glm::vec4& color, const std::string& desc = "") override;
    virtual void
    config_vec(const std::string& id, glm::vec3& value, const std::string& desc = "") override;
    virtual void
    config_vec(const std::string& id, glm::vec4& value, const std::string& desc = "") override;
    virtual void
    config_angle(const std::string& id, float& angle, const std::string& desc = "") override;
    virtual void
    config_percent(const std::string& id, float& value, const std::string& desc = "") override;
    virtual void config_float(const std::string& id,
                              float& value,
                              const std::string& desc = "",
                              const float sensitivity = 1.0f) override;
    virtual void config_float(const std::string& id,
                              float& value,
                              const float& min = FLT_MIN,
                              const float& max = FLT_MAX,
                              const std::string& desc = "") override;
    virtual void
    config_int(const std::string& id, int& value, const std::string& desc = "") override;
    virtual void config_int(const std::string& id,
                            int& value,
                            const int& min = std::numeric_limits<int>::min(),
                            const int& max = std::numeric_limits<int>::max(),
                            const std::string& desc = "") override;
    virtual void
    config_float3(const std::string& id, float value[3], const std::string& desc = "") override;
    virtual void
    config_bool(const std::string& id, bool& value, const std::string& desc = "") override;
    virtual bool config_bool(const std::string& id, const std::string& desc = "") override;
    virtual void config_options(const std::string& id,
                                int& selected,
                                const std::vector<std::string>& options,
                                const OptionsStyle style = OptionsStyle::DONT_CARE,
                                const std::string& desc = "") override;
    virtual bool config_text(const std::string& id,
                             const uint32_t max_len,
                             char* string,
                             const bool needs_submit = false,
                             const std::string& desc = "") override;
};

} // namespace merian