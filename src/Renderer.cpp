#include "Renderer.h"

#include <cstdint>
#include <iostream>
#include <print>

#include <vulkan/vulkan_core.h>
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_timer.h"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_vulkan.h"
#include "VkBootstrap.h"

void Renderer::init()
{
    init_sdl(m_init_data);
    create_instance(m_init_data);
    create_surface(m_init_data);
    create_physical_device(m_init_data);
    create_device(m_init_data);
    create_swapchain(m_init_data, m_render_data);
    init_vma(m_init_data);
    init_descriptors(m_init_data, m_render_data);
    create_draw_image(m_init_data, m_render_data);
    create_command_buffers(m_init_data, m_render_data);
    std::println("Renderer initialized");
}

void Renderer::destroy()
{
    VK_CHECK(vkDeviceWaitIdle(m_init_data.device));
    m_init_data.swapchain.destroy_image_views(m_render_data.swapchain_image_views);
    vkb::destroy_swapchain(m_init_data.swapchain);
    for (auto& frame : m_render_data.frame_data)
    {
        frame.deletion_queue.flush();
    }
    m_deletion_queue.flush();
    std::println("Renderer destroyed");
}

void Renderer::run()
{
    bool done = false;
    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(m_init_data.window))
                done = true;
        }

        if (SDL_GetWindowFlags(m_init_data.window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }
    }
}

void Renderer::init_sdl(InitData& init)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        std::cerr << "Error: SDL_InitData(): " << SDL_GetError() << std::endl;
    }
    m_deletion_queue.push_function([]() { SDL_Quit(); });

    // Create window with Vulkan graphics context
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    init.window_extent = { 1280, 800 };
    init.window = SDL_CreateWindow("Vulkan Compute",
                                   (int)(init.window_extent.width * main_scale),
                                   (int)(init.window_extent.height * main_scale),
                                   window_flags);
    if (init.window == nullptr)
    {
        std::cerr << "Error: SDL_CreateWindow(): " << SDL_GetError() << std::endl;
    }

    m_deletion_queue.push_function([init]() { SDL_DestroyWindow(init.window); });

    std::println("SDL initialized");
}

void Renderer::create_instance(InitData& init)
{
    auto system_info_ret = vkb::SystemInfo::get_system_info();
    if (!system_info_ret)
    {
        std::cerr << system_info_ret.error().message() << std::endl;
    }
    auto system_info = system_info_ret.value();
    std::println("Instance API: {}", system_info.instance_api_version);

    uint32_t extension_count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    for (uint32_t i = 0; i < extension_count; i++)
    {
        init.instance_extensions.push_back(extensions[i]);
    }
    init.instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME); // From Vulkan-Samples
    init.instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    vkb::InstanceBuilder instance_builder;
    auto instance_builder_return = instance_builder.set_app_name("Compute Shader Playground")
                                       .set_engine_name("Compute Shader Playground")
                                       .require_api_version(1, 4, 0)
                                       .enable_validation_layers()
                                       .use_default_debug_messenger()
                                       .enable_extensions(m_init_data.instance_extensions)
                                       .build();

    if (!instance_builder_return)
    {
        std::cerr << "Failed to create vkb instance: " << instance_builder_return.error().message() << std::endl;
    }

    init.instance = instance_builder_return.value();
    std::println("Instance created");

    m_deletion_queue.push_function([init]() { vkb::destroy_instance(init.instance); });
}

void Renderer::create_surface(InitData& init)
{
    if (!SDL_Vulkan_CreateSurface(init.window, init.instance, nullptr, &init.surface))
    {
        std::cerr << "Error: SDL_Vulkan_CreateSurface(): " << SDL_GetError() << std::endl;
    }
    std::println("Surface created");

    m_deletion_queue.push_function([init]() { SDL_Vulkan_DestroySurface(init.instance, init.surface, nullptr); });
}

void Renderer::create_physical_device(InitData& init)
{
    vkb::PhysicalDeviceSelector selector{ init.instance };
    auto phys_ret = selector.set_surface(init.surface).prefer_gpu_device_type().select();
    if (!phys_ret)
    {
        std::cerr << "Failed to select Vulkan Physical Device. Error: " << phys_ret.error().message() << "\n";
        if (phys_ret.error() == vkb::PhysicalDeviceError::no_suitable_device)
        {
            const auto& detailed_reasons = phys_ret.detailed_failure_reasons();
            if (!detailed_reasons.empty())
            {
                std::cerr << "GPU Selection failure reasons:\n";
                for (const std::string& reason : detailed_reasons)
                {
                    std::cerr << reason << "\n";
                }
            }
        }
    }

    std::vector<const char*> extensions = { VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                                            VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
                                            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
                                            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME };
    if (!phys_ret->enable_extensions_if_present(extensions))
    {
        std::cerr << "One or more device extensions not supported!" << std::endl;
    }

    init.physical_device = phys_ret.value();
    std::println("Physical device created");
}

void Renderer::create_device(InitData& init)
{
    vkb::DeviceBuilder device_builder{ init.physical_device };
    // automatically propagate needed data from instance & physical device
    auto dev_ret = device_builder.build();
    if (!dev_ret)
    {
        std::cerr << "Failed to create Vulkan device. Error: " << dev_ret.error().message() << std::endl;
    }
    init.device = dev_ret.value();
    std::println("Device created");

    m_deletion_queue.push_function([init]() { vkb::destroy_device(init.device); });
}

void Renderer::create_swapchain(InitData& init, RenderData& render)
{
    vkb::SwapchainBuilder swapchain_builder{ init.device };
    auto swap_builder_ret = swapchain_builder.set_desired_min_image_count(3)
                                .set_old_swapchain(init.swapchain)
                                .set_desired_extent(init.window_extent.width, init.window_extent.height)
                                .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                                .build();

    if (!swap_builder_ret)
    {
        std::cerr << "Failed to create swapchain";
    }

    vkb::destroy_swapchain(init.swapchain);
    init.swapchain = swap_builder_ret.value();
    render.swapchain_images = init.swapchain.get_images().value();
    render.swapchain_image_views = init.swapchain.get_image_views().value();
    std::println("Swapchain created");
}

void Renderer::init_vma(InitData& init)
{
    auto system_info_ret = vkb::SystemInfo::get_system_info();
    if (!system_info_ret)
    {
        std::cerr << system_info_ret.error().message() << std::endl;
    }
    auto system_info = system_info_ret.value();

    VmaAllocatorCreateInfo alloc_info = {};
    alloc_info.instance = init.instance;
    alloc_info.physicalDevice = init.physical_device;
    alloc_info.device = init.device;
    alloc_info.vulkanApiVersion = system_info.instance_api_version;
    alloc_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    VK_CHECK(vmaCreateAllocator(&alloc_info, &init.allocator));

    std::println("Vma allocator created");
    m_deletion_queue.push_function([init]() { vmaDestroyAllocator(init.allocator); });
}

void Renderer::create_draw_image(InitData& init, RenderData& render)
{
    VkExtent3D draw_image_extent = { init.swapchain.extent.width, init.window_extent.height, 1 };
    render.draw_image.image_extent = draw_image_extent;
    render.draw_image.image_format = VK_FORMAT_R16G16B16A16_SFLOAT;

    VkImageUsageFlags image_usage = {};
    image_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    image_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = nullptr;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = render.draw_image.image_format;
    image_info.extent = render.draw_image.image_extent;
    image_info.usage = image_usage;
    image_info.arrayLayers = 1;
    image_info.mipLevels = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(
        init.allocator, &image_info, &alloc_info, &render.draw_image.image, &render.draw_image.allocation, nullptr))
    m_deletion_queue.push_function(
        [init, render]() { vmaDestroyImage(init.allocator, render.draw_image.image, render.draw_image.allocation); });

    VkImageViewCreateInfo image_view_info = {};
    image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_info.pNext = nullptr;
    image_view_info.image = render.draw_image.image;
    image_view_info.format = render.draw_image.image_format;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.layerCount = 1;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VK_CHECK(vkCreateImageView(init.device, &image_view_info, nullptr, &render.draw_image.image_view));
    m_deletion_queue.push_function([init, render]()
                                   { vkDestroyImageView(init.device, render.draw_image.image_view, nullptr); });

    std::println("Draw image created\n\twidth: {}\n\theigth: {}",
                 render.draw_image.image_extent.width,
                 render.draw_image.image_extent.height);

    VkDescriptorSetAllocateInfo descriptor_alloc_info = {};
    descriptor_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_alloc_info.pNext = nullptr;
    descriptor_alloc_info.descriptorPool = render.descriptor_pool;
    descriptor_alloc_info.descriptorSetCount = 1;
    descriptor_alloc_info.pSetLayouts = &render.descriptor_layout;
    VK_CHECK(vkAllocateDescriptorSets(init.device, &descriptor_alloc_info, &render.descriptor_set));
}

void Renderer::create_command_buffers(InitData& init, RenderData& render)
{
    VkCommandPoolCreateInfo command_info = {};
    command_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_info.pNext = nullptr;
    command_info.queueFamilyIndex = init.device.get_queue_index(vkb::QueueType::graphics).value();
    command_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    for (auto& frame : render.frame_data)
    {
        VK_CHECK(vkCreateCommandPool(init.device, &command_info, nullptr, &frame.command_pool));
        frame.deletion_queue.push_function([init, frame]()
                                           { vkDestroyCommandPool(init.device, frame.command_pool, nullptr); });

        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.pNext = nullptr;
        alloc_info.commandPool = frame.command_pool;
        alloc_info.commandBufferCount = 1;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        VK_CHECK(vkAllocateCommandBuffers(init.device, &alloc_info, &frame.command_buffer));
    }
    std::println("Command buffers created");
}

void Renderer::init_descriptors(InitData& init, RenderData& render)
{
    VkDescriptorSetLayoutBinding layout_binding = {};
    layout_binding.binding = 0;
    layout_binding.descriptorCount = 1;
    layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    layout_binding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> descriptor_layout_bindings;
    descriptor_layout_bindings.push_back(layout_binding);

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.pNext = nullptr;
    layout_info.bindingCount = (uint32_t)descriptor_layout_bindings.size();
    layout_info.pBindings = descriptor_layout_bindings.data();
    vkCreateDescriptorSetLayout(init.device, &layout_info, nullptr, &render.descriptor_layout);
    m_deletion_queue.push_function([init, render]()
                                   { vkDestroyDescriptorSetLayout(init.device, render.descriptor_layout, nullptr); });

    std::vector<VkDescriptorPoolSize> pool_sizes = { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 } };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.pNext = nullptr;
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.poolSizeCount = (uint32_t)pool_sizes.size();
    pool_info.maxSets = 1;
    VK_CHECK(vkCreateDescriptorPool(init.device, &pool_info, nullptr, &render.descriptor_pool));
    m_deletion_queue.push_function([init, render]()
                                   { vkDestroyDescriptorPool(init.device, render.descriptor_pool, nullptr); });
    std::println("Descriptors initialized");
}
