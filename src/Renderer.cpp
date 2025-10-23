#include "Renderer.h"

#include <cstdint>
#include <iostream>
#include <print>

#include "SDL3/SDL_init.h"
#include "SDL3/SDL_timer.h"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_vulkan.h"
#include "VkBootstrap.h"
#include <vulkan/vulkan_core.h>

void Renderer::init() {
  init_sdl();
  create_instance();
  create_surface();
  create_physical_device();
  create_device();
  create_swapchain(m_init_data);
  std::println("Renderer initialized");
}

void Renderer::destroy() {
  m_init_data.swapchain.destroy_image_views(
      m_render_data.swapchain_image_views);
  vkb::destroy_swapchain(m_init_data.swapchain);
  vkb::destroy_device(m_init_data.device);
  vkDestroySurfaceKHR(m_init_data.instance, m_init_data.surface, nullptr);
  vkb::destroy_instance(m_init_data.instance);
  SDL_DestroyWindow(m_init_data.window);
  SDL_Quit();
  std::println("Renderer destroyed");
}

void Renderer::run() {
  bool done = false;
  while (!done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT)
        done = true;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(m_init_data.window))
        done = true;
    }

    if (SDL_GetWindowFlags(m_init_data.window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(10);
      continue;
    }
  }
}

void Renderer::init_sdl() {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::cerr << "Error: SDL_Init(): " << SDL_GetError() << std::endl;
  }

  // Create window with Vulkan graphics context
  float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  SDL_WindowFlags window_flags =
      SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  m_init_data.window =
      SDL_CreateWindow("Vulkan Compute", (int)(1280 * main_scale),
                       (int)(800 * main_scale), window_flags);
  if (m_init_data.window == nullptr) {
    std::cerr << "Error: SDL_CreateWindow(): " << SDL_GetError() << std::endl;
  }

  std::println("SDL initialized");
}

void Renderer::create_instance() {
  uint32_t extension_count = 0;
  const char *const *extensions =
      SDL_Vulkan_GetInstanceExtensions(&extension_count);
  for (uint32_t i = 0; i < extension_count; i++) {
    m_init_data.instance_extensions.push_back(extensions[i]);
  }
  m_init_data.instance_extensions.push_back(
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME); // From
                                                               // Vulkan-Samples
  m_init_data.instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  vkb::InstanceBuilder instance_builder;
  auto instance_builder_return =
      instance_builder.set_app_name("Compute Shader Playground")
          .set_engine_name("Compute Shader Playground")
          .require_api_version(1, 4, 0)
          .enable_validation_layers()
          .use_default_debug_messenger()
          .enable_extensions(m_init_data.instance_extensions)
          .build();

  if (!instance_builder_return) {
    std::cerr << "Failed to create vkb instance: "
              << instance_builder_return.error().message() << std::endl;
  }

  m_init_data.instance = instance_builder_return.value();
  std::println("Instance created");
}

void Renderer::create_surface() {
  if (!SDL_Vulkan_CreateSurface(m_init_data.window, m_init_data.instance,
                                nullptr, &m_init_data.surface)) {
    std::cerr << "Error: SDL_Vulkan_CreateSurface(): " << SDL_GetError()
              << std::endl;
  }
  std::println("Surface created");
}

void Renderer::create_physical_device() {
  vkb::PhysicalDeviceSelector selector{m_init_data.instance};
  auto phys_ret = selector.set_surface(m_init_data.surface)
                      .prefer_gpu_device_type()
                      .select();
  if (!phys_ret) {
    std::cerr << "Failed to select Vulkan Physical Device. Error: "
              << phys_ret.error().message() << "\n";
    if (phys_ret.error() == vkb::PhysicalDeviceError::no_suitable_device) {
      const auto &detailed_reasons = phys_ret.detailed_failure_reasons();
      if (!detailed_reasons.empty()) {
        std::cerr << "GPU Selection failure reasons:\n";
        for (const std::string &reason : detailed_reasons) {
          std::cerr << reason << "\n";
        }
      }
    }
  }

  std::vector<const char *> extensions = {
      VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
      VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
      VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
      VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};
  if (!phys_ret->enable_extensions_if_present(extensions)) {
    std::cerr << "One or more device extensions not supported!" << std::endl;
  }

  m_init_data.physical_device = phys_ret.value();
  std::println("Physical device created");
}

void Renderer::create_device() {
  vkb::DeviceBuilder device_builder{m_init_data.physical_device};
  // automatically propagate needed data from instance & physical device
  auto dev_ret = device_builder.build();
  if (!dev_ret) {
    std::cerr << "Failed to create Vulkan device. Error: "
              << dev_ret.error().message() << std::endl;
  }
  m_init_data.device = dev_ret.value();
  std::println("Device created");
}

void Renderer::create_swapchain(Init &init) {
  vkb::SwapchainBuilder swapchain_builder{init.device};
  auto swap_builder_ret =
      swapchain_builder.set_desired_min_image_count(3)
          .set_old_swapchain(init.swapchain)
          //.set_desired_extent(uint32_t width, uint32_t height)
          .build();

  if (!swap_builder_ret) {
    std::cerr << "Failed to create swapchain";
  }

  vkb::destroy_swapchain(init.swapchain);
  init.swapchain = swap_builder_ret.value();
  m_render_data.swapchain_images = init.swapchain.get_images().value();
  m_render_data.swapchain_image_views =
      init.swapchain.get_image_views().value();
  std::println("Swapchain created");
}
