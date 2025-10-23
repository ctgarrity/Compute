#pragma once
#include "SDL3/SDL_video.h"
#include "VkBootstrap.h"
#include <vector>
#include <vulkan/vulkan_core.h>

class Renderer {
public:
  void init();
  void destroy();
  void run();

private:
  struct Init {
    vkb::Instance instance = {};
    SDL_Window *window = nullptr;
    VkExtent2D window_extent = {};
    std::vector<const char *> instance_extensions;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vkb::PhysicalDevice physical_device = {};
    vkb::Device device = {};
    vkb::Swapchain swapchain = {};
  };

  struct RenderData {
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
  };

  Init m_init_data = {};
  RenderData m_render_data = {};

  void create_instance();
  void init_sdl();
  void create_surface();
  void create_physical_device();
  void create_device();
  void create_swapchain(Init &init);
};
