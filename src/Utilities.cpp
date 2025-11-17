#include "Utilities.h"
#include "Initializers.h"
#include <fstream>
#include <vector>

namespace util
{
    void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout current_layout, VkImageLayout new_layout)
    {
        VkImageMemoryBarrier2 image_barrier = {};
        image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        image_barrier.pNext = nullptr;
        image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        image_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
        image_barrier.oldLayout = current_layout;
        image_barrier.newLayout = new_layout;

        VkImageAspectFlags aspect_mask = (new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                             ? VK_IMAGE_ASPECT_DEPTH_BIT
                                             : VK_IMAGE_ASPECT_COLOR_BIT;
        image_barrier.subresourceRange = init::image_subresource_range(aspect_mask);
        image_barrier.image = image;

        VkDependencyInfo dependency_info{};
        dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency_info.pNext = nullptr;
        dependency_info.imageMemoryBarrierCount = 1;
        dependency_info.pImageMemoryBarriers = &image_barrier;

        vkCmdPipelineBarrier2(cmd, &dependency_info);
    }

    void copy_image_to_image(
        VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D src_size, VkExtent2D dst_size)
    {
        VkImageBlit2 blit_region = {};
        blit_region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
        blit_region.pNext = nullptr;
        blit_region.srcOffsets[1].x = src_size.width;
        blit_region.srcOffsets[1].y = src_size.height;
        blit_region.srcOffsets[1].z = 1;
        blit_region.dstOffsets[1].x = dst_size.width;
        blit_region.dstOffsets[1].y = dst_size.height;
        blit_region.dstOffsets[1].z = 1;
        blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.srcSubresource.baseArrayLayer = 0;
        blit_region.srcSubresource.layerCount = 1;
        blit_region.srcSubresource.mipLevel = 0;
        blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.dstSubresource.baseArrayLayer = 0;
        blit_region.dstSubresource.layerCount = 1;
        blit_region.dstSubresource.mipLevel = 0;

        VkBlitImageInfo2 blit_info = {};
        blit_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blit_info.pNext = nullptr;
        blit_info.dstImage = destination;
        blit_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blit_info.srcImage = source;
        blit_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blit_info.filter = VK_FILTER_LINEAR;
        blit_info.regionCount = 1;
        blit_info.pRegions = &blit_region;

        vkCmdBlitImage2(cmd, &blit_info);
    }

    bool load_shader_module(const char* file_path, VkDevice device, VkShaderModule* out_shader_module)
    {
        std::ifstream file(file_path, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        const size_t file_size = file.tellg();
        std::vector<uint32_t> buffer(file_size / sizeof(uint32_t));
        file.seekg(0);
        file.read((char*)buffer.data(), file_size);
        file.close();

        VkShaderModuleCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create_info.pNext = nullptr;
        create_info.codeSize = buffer.size() * sizeof(uint32_t);
        create_info.pCode = buffer.data();

        VkShaderModule shader_module = {};
        if (vkCreateShaderModule(device, &create_info, nullptr, &shader_module) != VK_SUCCESS)
        {
            return false;
        }
        *out_shader_module = shader_module;
        return true;
    }
} // namespace util
