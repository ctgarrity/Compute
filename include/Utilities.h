#pragma once
#include <vulkan/vulkan.h>

namespace util
{
    void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout current_layout, VkImageLayout new_layout);
    void copy_image_to_image(
        VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D src_size, VkExtent2D dst_size);
    bool load_shader_module(const char* file_path, VkDevice device, VkShaderModule* out_shader_module);
} // namespace util
