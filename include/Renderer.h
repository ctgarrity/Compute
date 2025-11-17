#pragma once
#include <vulkan/vulkan_core.h>
#include "glm/fwd.hpp"
#include "vulkan/vk_enum_string_helper.h"
#include "SDL3/SDL_video.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"
#include "glm/glm.hpp"

#include <deque>
#include <functional>
#include <ranges>
#include <vector>
#include <array>

#define VK_CHECK(func)                                                                                                 \
    {                                                                                                                  \
        const VkResult result = func;                                                                                  \
        if (result != VK_SUCCESS)                                                                                      \
        {                                                                                                              \
            std::cerr << "Error calling function " << #func << "at " << __FILE__ << ":" << __LINE__ << ". Result is "  \
                      << string_VkResult(result) << std::endl;                                                         \
            assert(false);                                                                                             \
        }                                                                                                              \
    }

class Renderer
{
    static constexpr unsigned int FRAMES_IN_FLIGHT = 2;

    struct InitData
    {
        vkb::Instance instance = {};
        SDL_Window* window = nullptr;
        VkExtent2D window_extent = {};
        std::vector<const char*> instance_extensions;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        vkb::PhysicalDevice physical_device = {};
        vkb::Device device = {};
        // uint32_t graphics_queue_index;
        vkb::Swapchain swapchain = {};
        VmaAllocator allocator = {};
    };

    struct AllocatedImage
    {
        VkImage image;
        VkImageView image_view;
        VmaAllocation allocation;
        VkExtent3D image_extent;
        VkFormat image_format;
    };

    struct DeletionQueue
    {
    private:
        std::deque<std::function<void()>> queue;

    public:
        void flush()
        {
            for (const std::function<void()>& func : std::views::reverse(queue))
            {
                func();
            }
            queue.clear();
        }

        void push_function(std::function<void()>&& func)
        {
            queue.push_back(std::move(func));
        }
    };

    struct FrameData
    {
    private:
        DeletionQueue deletion_queue;

    public:
        VkCommandPool command_pool;
        VkCommandBuffer command_buffer;

        VkSemaphore acquire_semaphore;
        VkFence render_fence;

        void flush_frame_data()
        {
            deletion_queue.flush();
        }
    };

    struct PushConstantsData
    {
        glm::vec4 data1;
        glm::vec4 data2;
        glm::vec4 data3;
        glm::vec4 data4;
    };

    struct RenderData
    {
        std::vector<VkImage> swapchain_images;
        std::vector<VkImageView> swapchain_image_views;
        AllocatedImage draw_image;
        std::array<FrameData, FRAMES_IN_FLIGHT> frame_data;
        uint32_t frame_index = 0;
        std::vector<VkSemaphore> submit_semaphores;
        VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        VkPipelineLayout compute_layout = VK_NULL_HANDLE;
        VkPipeline compute_pipeline = VK_NULL_HANDLE;
        PushConstantsData push_constants_data;
    };

public:
    void init();
    void destroy();
    void run();

private:
    InitData m_init_data;
    RenderData m_render_data;
    DeletionQueue m_deletion_queue;

    void create_instance();
    void init_sdl();
    void create_surface();
    void create_physical_device();
    void create_device();
    void create_swapchain();
    void init_vma();
    void create_draw_image();
    void create_command_buffers();
    void init_descriptors();
    void init_sync_structures();
    void init_compute_pipeline();
    void draw_frame();
    FrameData& get_current_frame()
    {
        return m_render_data.frame_data[m_render_data.frame_index % FRAMES_IN_FLIGHT];
    };
};
