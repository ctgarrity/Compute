#pragma once
#include "vulkan/vulkan_core.h"

namespace init
{
    VkCommandPoolCreateInfo command_pool_create_info(uint32_t queue_family_index, VkCommandPoolCreateFlags flags = 0);
    VkCommandBufferAllocateInfo command_buffer_allocate_info(VkCommandPool command_pool,
                                                             uint32_t command_buffer_count = 1);
    VkCommandBufferBeginInfo command_buffer_begin_info(VkCommandBufferUsageFlags flags = 0);
    VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer cmd);

    VkFenceCreateInfo fence_create_info(VkFenceCreateFlags flags = 0);
    VkSemaphoreCreateInfo semaphore_create_info(VkSemaphoreCreateFlags flags = 0);
    VkSemaphoreSubmitInfo semaphore_submit_info(VkPipelineStageFlags2 stage_mask, VkSemaphore semaphore);

    VkImageSubresourceRange image_subresource_range(VkImageAspectFlags aspect_mask);
    VkSubmitInfo2 submit_info(VkCommandBufferSubmitInfo* cmd,
                              VkSemaphoreSubmitInfo* signal_semaphore_info,
                              VkSemaphoreSubmitInfo* wait_semaphore_info);

    VkImageCreateInfo image_create_info(VkFormat format, VkImageUsageFlags usage_flags, VkExtent3D extent);
    VkImageViewCreateInfo image_view_create_info(VkFormat format, VkImage image, VkImageAspectFlags aspect_flags);

    VkRenderingAttachmentInfo color_attachment_info(VkImageView view,
                                                    VkClearValue* clear,
                                                    VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo depth_attachment_info(VkImageView view,
                                                    VkImageLayout layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    VkRenderingInfo rendering_info(VkExtent2D renderExtent,
                                   VkRenderingAttachmentInfo* color_attachment,
                                   VkRenderingAttachmentInfo* depth_attachment);

    VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_info(VkShaderStageFlagBits stage,
                                                                      VkShaderModule shader_module,
                                                                      const char* entry = "main");
    VkPipelineLayoutCreateInfo pipeline_layout_create_info();
} // namespace init
