#include "Renderer.h"
#include "VkBootstrap.h"

#include <iostream>
#include <print>
#include <vulkan/vulkan_core.h>

void Renderer::init() {
  create_instance();
  std::println("Renderer initialized");
}

void Renderer::destroy() {
  vkb::destroy_instance(m_instance);
  std::println("Renderer destroyed");
}

void Renderer::create_instance() {
  vkb::InstanceBuilder instance_builder;
  auto instance_builder_return =
      instance_builder.set_app_name("Compute Shader Playground")
          .set_engine_name("Compute Shader Playground")
          .require_api_version(1, 4, 0)
          .enable_validation_layers()
          .use_default_debug_messenger()
          .build();

  if (!instance_builder_return) {
    std::cerr << "Failed to create vkb instance: "
              << instance_builder_return.error().message() << std::endl;
  }

  m_instance = instance_builder_return.value();
  std::println("Instance created");
}
