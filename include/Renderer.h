#pragma once
#include <vulkan/vulkan_core.h>
#include "vulkan/vk_enum_string_helper.h"
#include "SDL3/SDL_video.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

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
        DeletionQueue deletion_queue;

        VkCommandPool command_pool;
        VkCommandBuffer command_buffer;

        VkSemaphore acquire_semaphore;
        VkFence render_fence;
    };

    struct RenderData
    {
        std::vector<VkImage> swapchain_images;
        std::vector<VkImageView> swapchain_image_views;
        AllocatedImage draw_image;
        std::array<FrameData, FRAMES_IN_FLIGHT> frame_data;
        std::vector<VkDescriptorSetLayoutBinding> descriptor_layout_bindings;
    };

public:
    void init();
    void destroy();
    void run();

private:
    InitData m_init_data = {};
    RenderData m_render_data = {};
    DeletionQueue m_deletion_queue;

    void create_instance(InitData& init);
    void init_sdl(InitData& init);
    void create_surface(InitData& init);
    void create_physical_device(InitData& init);
    void create_device(InitData& init);
    void create_swapchain(InitData& init, RenderData& render);
    void init_vma(InitData& init);
    void create_draw_image(InitData& init, RenderData& render);
    void create_command_buffers(InitData& init, RenderData& render);
    void create_descriptors(RenderData& render);
};
