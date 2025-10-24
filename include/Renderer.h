#pragma once
#include "SDL3/SDL_video.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"
#include <deque>
#include <functional>
#include <ranges>
#include <vector>
#include <vulkan/vulkan_core.h>

class Renderer {
  struct InitData {
    vkb::Instance instance = {};
    SDL_Window *window = nullptr;
    VkExtent2D window_extent = {};
    std::vector<const char *> instance_extensions;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vkb::PhysicalDevice physical_device = {};
    vkb::Device device = {};
    vkb::Swapchain swapchain = {};
    VmaAllocator allocator = {};
  };

  struct RenderData {
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
  };

  struct DeletionQueue {
  private:
    std::deque<std::function<void()>> queue;

  public:
    void flush() {
      for (const std::function<void()> &func : std::views::reverse(queue)) {
        func();
      }
      queue.clear();
    }

    void push_function(std::function<void()> &&func) {
      queue.push_back(std::move(func));
    }
  };

public:
  void init();
  void destroy();
  void run();

private:
  InitData m_init_data = {};
  RenderData m_render_data = {};
  DeletionQueue m_deletion_queue;

  void create_instance(InitData &init);
  void init_sdl(InitData &init);
  void create_surface(InitData &init);
  void create_physical_device(InitData &init);
  void create_device(InitData &init);
  void create_swapchain(InitData &init, RenderData &render);
  void init_vma(InitData &init);
};
