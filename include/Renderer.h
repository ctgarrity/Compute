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
  vkb::Instance m_instance;
  SDL_Window *m_window = nullptr;
  std::vector<const char *> m_instance_extensions;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  vkb::PhysicalDevice m_physical_device;
  vkb::Device m_device;

  void create_instance();
  void init_sdl();
  void create_surface();
  void create_physical_device();
  void create_device();
};
