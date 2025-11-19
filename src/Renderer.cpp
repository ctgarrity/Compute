#include "Renderer.h"

#include <cstdint>
#include <iostream>
#include <print>
#include <fstream>

#include <vulkan/vulkan_core.h>

#include "SDL3/SDL_init.h"
#include "SDL3/SDL_timer.h"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_vulkan.h"

#include "VkBootstrap.h"

#include "Initializers.h"
#include "Utilities.h"

#include "glm/fwd.hpp"
#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_vulkan.h"

void Renderer::init()
{
    init_sdl();
    create_instance();
    create_surface();
    create_physical_device();
    create_device();
    create_swapchain();
    init_vma();
    init_descriptors();
    create_draw_image();
    create_command_buffers();
    init_sync_structures();
    init_imgui();
    init_compute_pipeline();
    std::println("Renderer initialized");
}

void Renderer::destroy()
{
    VK_CHECK(vkDeviceWaitIdle(m_init_data.device));
    m_init_data.swapchain.destroy_image_views(m_render_data.swapchain_image_views);
    vkb::destroy_swapchain(m_init_data.swapchain);
    for (auto& frame : m_render_data.frame_data)
    {
        frame.flush_frame_data();
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
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(m_init_data.window))
                done = true;
            if (event.window.type == SDL_EVENT_WINDOW_RESIZED)
            {
                m_render_data.resize_requested = true;
            }
        }

        if (SDL_GetWindowFlags(m_init_data.window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        if (m_render_data.resize_requested)
        {
            int width, height;
            SDL_GetWindowSize(m_init_data.window, &width, &height);
            m_init_data.window_extent.width = width;
            m_init_data.window_extent.height = height;
            recreate_swapchain();
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (ImGui::Begin("Push Constants"))
        {
            ImGui::InputFloat("Time", (float*)&m_render_data.push_constants_data.time);
            ImGui::InputFloat3("Color 1", (float*)&m_render_data.push_constants_data.color1);
            ImGui::InputFloat3("Color 2", (float*)&m_render_data.push_constants_data.color2);
            ImGui::InputFloat2("Cell Coords", (float*)&m_render_data.push_constants_data.cell_coords);
        }
        ImGui::End();
        ImGui::Render();
        draw_frame();
    }
}

void Renderer::init_sdl()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        std::cerr << "Error: SDL_InitData(): " << SDL_GetError() << std::endl;
    }
    m_deletion_queue.push_function([]() { SDL_Quit(); });

    // Create window with Vulkan graphics context
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    uint32_t requested_width = 1280;
    uint32_t requested_height = 800;
    m_init_data.window_extent = { requested_width, requested_height };
    m_init_data.window = SDL_CreateWindow("Vulkan Compute",
                                          (int)(m_init_data.window_extent.width * main_scale),
                                          (int)(m_init_data.window_extent.height * main_scale),
                                          window_flags);
    int h, w;
    SDL_GetWindowSize(m_init_data.window, &w, &h);
    m_init_data.window_extent.width = w;
    m_init_data.window_extent.height = h;
    std::println("Window size: Width {}, Height {}", m_init_data.window_extent.width, m_init_data.window_extent.height);
    if (!m_init_data.window)
    {
        std::cerr << "Error: SDL_CreateWindow(): " << SDL_GetError() << std::endl;
    }
    // SDL_SetWindowFullscreen(m_init_data.window, true);

    m_deletion_queue.push_function([this]() { SDL_DestroyWindow(m_init_data.window); });

    std::println("SDL initialized");
}

void Renderer::create_instance()
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
        m_init_data.instance_extensions.push_back(extensions[i]);
    }
    m_init_data.instance_extensions.push_back(
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME); // From Vulkan-Samples
    m_init_data.instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

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

    m_init_data.instance = instance_builder_return.value();
    std::println("Instance created");

    m_deletion_queue.push_function([this]() { vkb::destroy_instance(m_init_data.instance); });
}

void Renderer::create_surface()
{
    if (!SDL_Vulkan_CreateSurface(m_init_data.window, m_init_data.instance, nullptr, &m_init_data.surface))
    {
        std::cerr << "Error: SDL_Vulkan_CreateSurface(): " << SDL_GetError() << std::endl;
    }
    std::println("Surface created");

    m_deletion_queue.push_function([this]()
                                   { SDL_Vulkan_DestroySurface(m_init_data.instance, m_init_data.surface, nullptr); });
}

void Renderer::create_physical_device()
{
    VkPhysicalDeviceVulkan13Features features13 = {};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    vkb::PhysicalDeviceSelector selector{ m_init_data.instance };
    auto phys_ret = selector.set_surface(m_init_data.surface)
                        .prefer_gpu_device_type()
                        .set_required_features_12(features12)
                        .set_required_features_13(features13)
                        .select();
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

    // std::vector<const char*> extensions = { VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    //                                         VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    //                                         VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    //                                         VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME };
    // if (!phys_ret->enable_extensions_if_present(extensions))
    // {
    //     std::cerr << "One or more device extensions not supported!" << std::endl;
    // }

    m_init_data.physical_device = phys_ret.value();
    std::println("Physical device created");
}

void Renderer::create_device()
{
    vkb::DeviceBuilder device_builder{ m_init_data.physical_device };
    // automatically propagate needed data from instance & physical device
    auto dev_ret = device_builder.build();
    if (!dev_ret)
    {
        std::cerr << "Failed to create Vulkan device. Error: " << dev_ret.error().message() << std::endl;
    }
    m_init_data.device = dev_ret.value();
    std::println("Device created");

    m_deletion_queue.push_function([this]() { vkb::destroy_device(m_init_data.device); });
    // m_init_data.graphics_queue_index = m_init_data.device.get_queue_index(vkb::QueueType::graphics).value();
}

void Renderer::create_swapchain()
{
    vkb::SwapchainBuilder swapchain_builder{ m_init_data.device };
    auto swap_builder_ret =
        swapchain_builder.set_old_swapchain(m_init_data.swapchain)
            .set_desired_min_image_count(3)
            .set_desired_extent(m_init_data.window_extent.width, m_init_data.window_extent.height)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
            .build();

    if (!swap_builder_ret)
    {
        std::cerr << "Failed to create swapchain" << std::endl;
    }

    vkb::destroy_swapchain(m_init_data.swapchain);
    m_init_data.swapchain = swap_builder_ret.value();
    m_render_data.swapchain_images = m_init_data.swapchain.get_images().value();
    m_render_data.swapchain_image_views = m_init_data.swapchain.get_image_views().value();
    std::println("Swapchain created");
}

void Renderer::recreate_swapchain()
{
    vkDeviceWaitIdle(m_init_data.device);

    m_init_data.swapchain.destroy_image_views(m_render_data.swapchain_image_views);
    destroy_draw_image();
    create_swapchain();
    create_draw_image();
}

void Renderer::init_vma()
{
    auto system_info_ret = vkb::SystemInfo::get_system_info();
    if (!system_info_ret)
    {
        std::cerr << system_info_ret.error().message() << std::endl;
    }
    auto system_info = system_info_ret.value();

    VmaAllocatorCreateInfo alloc_info = {};
    alloc_info.instance = m_init_data.instance;
    alloc_info.physicalDevice = m_init_data.physical_device;
    alloc_info.device = m_init_data.device;
    alloc_info.vulkanApiVersion = system_info.instance_api_version;
    alloc_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    VK_CHECK(vmaCreateAllocator(&alloc_info, &m_init_data.allocator));

    std::println("Vma allocator created");
    m_deletion_queue.push_function([this]() { vmaDestroyAllocator(m_init_data.allocator); });
}

void Renderer::create_draw_image()
{
    VkExtent3D draw_image_extent = { m_init_data.swapchain.extent.width, m_init_data.swapchain.extent.height, 1 };
    m_render_data.draw_image.image_extent = draw_image_extent;
    m_render_data.draw_image.image_format = VK_FORMAT_R16G16B16A16_SFLOAT;

    VkImageUsageFlags image_usage = {};
    image_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    image_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = nullptr;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = m_render_data.draw_image.image_format;
    image_info.extent = m_render_data.draw_image.image_extent;
    image_info.usage = image_usage;
    image_info.arrayLayers = 1;
    image_info.mipLevels = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(m_init_data.allocator,
                            &image_info,
                            &alloc_info,
                            &m_render_data.draw_image.image,
                            &m_render_data.draw_image.allocation,
                            nullptr));
    // m_deletion_queue.push_function(
    //     [this]()
    //     {
    //         vmaDestroyImage(m_init_data.allocator, m_render_data.draw_image.image,
    //         m_render_data.draw_image.allocation);
    //     });

    VkImageViewCreateInfo image_view_info = {};
    image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_info.pNext = nullptr;
    image_view_info.image = m_render_data.draw_image.image;
    image_view_info.format = m_render_data.draw_image.image_format;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.layerCount = 1;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VK_CHECK(vkCreateImageView(m_init_data.device, &image_view_info, nullptr, &m_render_data.draw_image.image_view));
    // m_deletion_queue.push_function(
    //     [this]() { vkDestroyImageView(m_init_data.device, m_render_data.draw_image.image_view, nullptr); });

    std::println("Draw image created\n\twidth: {}\n\theigth: {}",
                 m_render_data.draw_image.image_extent.width,
                 m_render_data.draw_image.image_extent.height);

    VkDescriptorSetAllocateInfo descriptor_alloc_info = {};
    descriptor_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_alloc_info.pNext = nullptr;
    descriptor_alloc_info.descriptorPool = m_render_data.descriptor_pool;
    descriptor_alloc_info.descriptorSetCount = 1;
    descriptor_alloc_info.pSetLayouts = &m_render_data.descriptor_layout;
    VK_CHECK(vkAllocateDescriptorSets(m_init_data.device, &descriptor_alloc_info, &m_render_data.descriptor_set));
    update_draw_image_descriptor();
}

void Renderer::destroy_draw_image()
{
    vkDestroyImageView(m_init_data.device, m_render_data.draw_image.image_view, nullptr);
    vmaDestroyImage(m_init_data.allocator, m_render_data.draw_image.image, m_render_data.draw_image.allocation);
    vkResetDescriptorPool(m_init_data.device, m_render_data.descriptor_pool, 0);
}

void Renderer::update_draw_image_descriptor()
{
    VkDescriptorImageInfo image_info = {};
    image_info.sampler = VK_NULL_HANDLE;                        // storage image, no sampler
    image_info.imageView = m_render_data.draw_image.image_view; // your created view
    image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;           // matches how you use it

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_render_data.descriptor_set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &image_info;

    vkUpdateDescriptorSets(m_init_data.device, 1, &write, 0, nullptr);
}

void Renderer::create_command_buffers()
{
    VkCommandPoolCreateInfo command_info = {};
    command_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_info.pNext = nullptr;
    command_info.queueFamilyIndex = m_init_data.device.get_queue_index(vkb::QueueType::graphics).value();
    command_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    for (auto& frame : m_render_data.frame_data)
    {
        VK_CHECK(vkCreateCommandPool(m_init_data.device, &command_info, nullptr, &frame.command_pool));

        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.pNext = nullptr;
        alloc_info.commandPool = frame.command_pool;
        alloc_info.commandBufferCount = 1;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        VK_CHECK(vkAllocateCommandBuffers(m_init_data.device, &alloc_info, &frame.command_buffer));
    }

    m_deletion_queue.push_function(
        [this]()
        {
            for (size_t i = 0; i < m_render_data.frame_data.size(); i++)
            {
                vkDestroyCommandPool(m_init_data.device, m_render_data.frame_data[i].command_pool, nullptr);
            }
        });
    std::println("Command buffers created");
}

void Renderer::init_descriptors()
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
    vkCreateDescriptorSetLayout(m_init_data.device, &layout_info, nullptr, &m_render_data.descriptor_layout);
    m_deletion_queue.push_function(
        [this]() { vkDestroyDescriptorSetLayout(m_init_data.device, m_render_data.descriptor_layout, nullptr); });

    std::vector<VkDescriptorPoolSize> pool_sizes = { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 } };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.pNext = nullptr;
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.poolSizeCount = (uint32_t)pool_sizes.size();
    pool_info.maxSets = 1;
    VK_CHECK(vkCreateDescriptorPool(m_init_data.device, &pool_info, nullptr, &m_render_data.descriptor_pool));
    m_deletion_queue.push_function(
        [this]() { vkDestroyDescriptorPool(m_init_data.device, m_render_data.descriptor_pool, nullptr); });
    std::println("Descriptors initialized");
}

void Renderer::init_sync_structures()
{
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.pNext = nullptr;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_info.pNext = nullptr;

    for (auto& frame : m_render_data.frame_data)
    {
        VK_CHECK(vkCreateFence(m_init_data.device, &fence_info, nullptr, &frame.render_fence));
        VK_CHECK(vkCreateSemaphore(m_init_data.device, &semaphore_info, nullptr, &frame.acquire_semaphore));
    }

    m_deletion_queue.push_function(
        [this]()
        {
            for (size_t i = 0; i < m_render_data.frame_data.size(); i++)
            {
                vkDestroyFence(m_init_data.device, m_render_data.frame_data[i].render_fence, nullptr);
                vkDestroySemaphore(m_init_data.device, m_render_data.frame_data[i].acquire_semaphore, nullptr);
            }
        });

    m_render_data.submit_semaphores.resize(m_render_data.swapchain_images.size());
    for (size_t i = 0; i < m_render_data.swapchain_images.size(); i++)
    {
        VK_CHECK(vkCreateSemaphore(m_init_data.device, &semaphore_info, nullptr, &m_render_data.submit_semaphores[i]));
    }

    m_deletion_queue.push_function(
        [this]()
        {
            for (size_t i = 0; i < m_render_data.swapchain_images.size(); i++)
            {
                vkDestroySemaphore(m_init_data.device, m_render_data.submit_semaphores[i], nullptr);
            }
        });
    std::println("Sync structures initialized");
}

void Renderer::init_imgui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplSDL3_InitForVulkan(m_init_data.window);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = m_init_data.instance;
    init_info.PhysicalDevice = m_init_data.physical_device;
    init_info.Device = m_init_data.device;
    init_info.QueueFamily = m_init_data.device.get_queue_index(vkb::QueueType::graphics).value();
    init_info.Queue = m_init_data.device.get_queue(vkb::QueueType::graphics).value();
    // init_info.PipelineCache = YOUR_PIPELINE_CACHE; // optional
    // init_info.DescriptorPool = YOUR_DESCRIPTOR_POOL; // see below Todo: Check if the DescriptorPoolSize is correct
    init_info.DescriptorPoolSize =
        IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE; // (Optional) Set to create internal descriptor pool instead
                                                           // of using DescriptorPool
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = 2;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.UseDynamicRendering = true;
    // init_info.Allocator = YOUR_ALLOCATOR; // optional
    // init_info.CheckVkResultFn = check_vk_result; // optional

    VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {};
    pipeline_rendering_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline_rendering_create_info.pNext = nullptr;
    pipeline_rendering_create_info.colorAttachmentCount = 1;
    pipeline_rendering_create_info.pColorAttachmentFormats = &m_init_data.swapchain.image_format;

    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering_create_info;

    ImGui_ImplVulkan_Init(&init_info);
    // (this gets a bit more complicated, see example app for full reference)
    // ImGui_ImplVulkan_CreateFontsTexture(get_current_frame().main_command_buffer);
    // (your code submit a queue)
    // ImGui_ImplVulkan_DestroyFontUploadObjects();
    m_deletion_queue.push_function(
        [&]()
        {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
        });

    std::cout << "imgui initialized" << std::endl;
}

void Renderer::draw_imgui(VkCommandBuffer cmd, VkImageView target_image_view)
{
    VkRenderingAttachmentInfo color_attachment =
        init::color_attachment_info(target_image_view, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo render_info = init::rendering_info(m_init_data.swapchain.extent, &color_attachment, nullptr);

    vkCmdBeginRendering(cmd, &render_info);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
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

void Renderer::init_compute_pipeline()
{
    VkPushConstantRange push_constant_range = {};
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstantsData);
    push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pNext = nullptr;
    layout_info.pSetLayouts = &m_render_data.descriptor_layout;
    layout_info.setLayoutCount = 1;
    layout_info.pPushConstantRanges = &push_constant_range;
    layout_info.pushConstantRangeCount = 1;
    VK_CHECK(vkCreatePipelineLayout(m_init_data.device, &layout_info, nullptr, &m_render_data.compute_layout));

    VkShaderModule gradient_shader_module = {};
    if (!load_shader_module("shaders/gradient.spv", m_init_data.device, &gradient_shader_module))
    {
        std::cerr << "Failed to load gradient shader" << std::endl;
        return;
    }

    VkPipelineShaderStageCreateInfo stage_info = {};
    stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_info.pNext = nullptr;
    stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_info.module = gradient_shader_module;
    stage_info.pName = "main";

    VkComputePipelineCreateInfo compute_pipeline_create_info = {};
    compute_pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute_pipeline_create_info.pNext = nullptr;
    compute_pipeline_create_info.layout = m_render_data.compute_layout;
    compute_pipeline_create_info.stage = stage_info;

    VK_CHECK(vkCreateComputePipelines(m_init_data.device,
                                      VK_NULL_HANDLE,
                                      1,
                                      &compute_pipeline_create_info,
                                      nullptr,
                                      &m_render_data.compute_pipeline));

    vkDestroyShaderModule(m_init_data.device, gradient_shader_module, nullptr);
    m_deletion_queue.push_function(
        [this]()
        {
            vkDestroyPipelineLayout(m_init_data.device, m_render_data.compute_layout, nullptr);
            vkDestroyPipeline(m_init_data.device, m_render_data.compute_pipeline, nullptr);
        });
    std::println("Compute pipeline initialized");
}

void Renderer::init_push_constants()
{
    m_render_data.push_constants_data.color1 = glm::vec3(1.0f, 0.0f, 0.0f);
    m_render_data.push_constants_data.color2 = glm::vec3(0.0f, 0.0f, 1.0f);
}

void Renderer::draw_frame()
{
    VK_CHECK(vkWaitForFences(m_init_data.device, 1, &get_current_frame().render_fence, true, 1'000'000'000));
    VK_CHECK(vkResetFences(m_init_data.device, 1, &get_current_frame().render_fence));
    get_current_frame().flush_frame_data();

    uint32_t swapchain_image_index;
    // VK_CHECK(vkAcquireNextImageKHR(m_init_data.device,
    //                                m_init_data.swapchain,
    //                                1'000'000'000,
    //                                get_current_frame().acquire_semaphore,
    //                                nullptr,
    //                                &swapchain_image_index));
    VkResult result = vkAcquireNextImageKHR(m_init_data.device,
                                            m_init_data.swapchain,
                                            1'000'000'000,
                                            get_current_frame().acquire_semaphore,
                                            nullptr,
                                            &swapchain_image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        m_render_data.resize_requested = true;
        return;
    }

    VkCommandBuffer cmd_buffer = get_current_frame().command_buffer;
    VK_CHECK(vkResetCommandBuffer(cmd_buffer, 0));
    VkCommandBufferBeginInfo begin_info = init::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd_buffer, &begin_info));

    util::transition_image(
        cmd_buffer, m_render_data.draw_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_render_data.compute_pipeline);
    vkCmdBindDescriptorSets(cmd_buffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_render_data.compute_layout,
                            0,
                            1,
                            &m_render_data.descriptor_set,
                            0,
                            nullptr);

    m_render_data.push_constants_data.time = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    vkCmdPushConstants(cmd_buffer,
                       m_render_data.compute_layout,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(PushConstantsData),
                       &m_render_data.push_constants_data);

    const uint32_t local_size_x = 16;
    const uint32_t local_size_y = 16;
    const uint32_t group_x = (m_render_data.draw_image.image_extent.width + local_size_x - 1) / local_size_x;
    const uint32_t group_y = (m_render_data.draw_image.image_extent.height + local_size_y - 1) / local_size_y;
    vkCmdDispatch(cmd_buffer, group_x, group_y, 1);

    util::transition_image(
        cmd_buffer, m_render_data.draw_image.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    util::transition_image(cmd_buffer,
                           m_render_data.swapchain_images[swapchain_image_index],
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkExtent2D draw_extent_2D = { m_render_data.draw_image.image_extent.width,
                                  m_render_data.draw_image.image_extent.height };
    util::copy_image_to_image(cmd_buffer,
                              m_render_data.draw_image.image,
                              m_render_data.swapchain_images[swapchain_image_index],
                              draw_extent_2D,
                              m_init_data.swapchain.extent);
    util::transition_image(cmd_buffer,
                           m_render_data.swapchain_images[swapchain_image_index],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    draw_imgui(cmd_buffer, m_render_data.swapchain_image_views[swapchain_image_index]);
    util::transition_image(cmd_buffer,
                           m_render_data.swapchain_images[swapchain_image_index],
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    VK_CHECK(vkEndCommandBuffer(cmd_buffer));

    VkCommandBufferSubmitInfo cmd_buffer_info = init::command_buffer_submit_info(cmd_buffer);
    VkSemaphoreSubmitInfo wait_info = init::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
                                                                  get_current_frame().acquire_semaphore);
    VkSemaphoreSubmitInfo signal_info = init::semaphore_submit_info(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, m_render_data.submit_semaphores[swapchain_image_index]);
    VkSubmitInfo2 submit = init::submit_info(&cmd_buffer_info, &signal_info, &wait_info);
    VK_CHECK(vkQueueSubmit2(
        m_init_data.device.get_queue(vkb::QueueType::graphics).value(), 1, &submit, get_current_frame().render_fence));

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = nullptr;
    present_info.pSwapchains = &m_init_data.swapchain.swapchain;
    present_info.swapchainCount = 1;
    present_info.pWaitSemaphores = &m_render_data.submit_semaphores[swapchain_image_index];
    present_info.waitSemaphoreCount = 1;
    present_info.pImageIndices = &swapchain_image_index;

    // VK_CHECK(vkQueuePresentKHR(m_init_data.device.get_queue(vkb::QueueType::graphics).value(), &present_info));
    result = vkQueuePresentKHR(m_init_data.device.get_queue(vkb::QueueType::graphics).value(), &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        m_render_data.resize_requested = true;
    }
    m_render_data.frame_index++;
}
