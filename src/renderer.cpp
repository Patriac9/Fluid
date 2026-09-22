#include <vulkan/vulkan.h>
#include "renderer.h"
#include "window.h"
#include "fluid/model.h"
#include <cstddef>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include "fluid_shaders.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace Fluid {
namespace {
constexpr uint32_t kMaxScenes = 32;
constexpr float kDlssScale = 1.0f / 1.5f;
void check(VkResult result, const char *operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " (Vulkan error " + std::to_string(result) + ")");
}
struct Buffer {
    VkBuffer handle{};
    VkDeviceMemory memory{};
    VkDeviceSize capacity{};
    VkDeviceAddress address{};
};
struct Texture {
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
};
struct Accel {
    VkAccelerationStructureKHR handle{};
    Buffer buffer;
    VkDeviceSize size{};
    VkDeviceAddress address{};
};
struct ScenePush {
    glm::mat4 mvp{1.0f};
    glm::vec4 tint{1.0f};
    glm::vec4 material{};
    glm::vec4 eye{};
    glm::vec4 rotation{1, 0, 0, 0};
};
static_assert(sizeof(ScenePush) == 128);
struct BlitPush {
    glm::vec2 screen{};
    glm::vec2 pad{};
    glm::vec4 rect{};
};
static_assert(sizeof(BlitPush) == 32);
struct SceneRange {
    uint32_t vertex = 0, first = 0, count = 0, line_first = 0, line_count = 0, grid_first = 0, grid_count = 0;
    uint32_t vertex_count = 0;
};
enum class PipeKind { Ui, Scene, Line, SceneRt, Blit, Upscale };
VkSampleCountFlagBits pick_samples(VkSampleCountFlags supported, uint32_t want) {
    for (uint32_t s : {8u, 4u, 2u, 1u})
        if (s <= want && (supported & s))
            return static_cast<VkSampleCountFlagBits>(s);
    return VK_SAMPLE_COUNT_1_BIT;
}
VkBool32 VKAPI_PTR validation_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                       VkDebugUtilsMessageTypeFlagsEXT,
                                       const VkDebugUtilsMessengerCallbackDataEXT *data, void *) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[Fluid Vulkan] " << data->pMessage << '\n';
    return VK_FALSE;
}
template <size_t N> std::vector<uint32_t> shader_words(const unsigned char (&bytes)[N]) {
    static_assert(N % 4 == 0, "SPIR-V bytecode must contain complete words");
    std::vector<uint32_t> words(N / 4);
    std::memcpy(words.data(), bytes, N);
    return words;
}
std::vector<uint32_t> shader_code(const char *name) {
    if (std::strcmp(name, "ui.vert") == 0)
        return shader_words(Embedded::ui_vert);
    if (std::strcmp(name, "ui.frag") == 0)
        return shader_words(Embedded::ui_frag);
    if (std::strcmp(name, "scene.vert") == 0)
        return shader_words(Embedded::scene_vert);
    if (std::strcmp(name, "scene.frag") == 0)
        return shader_words(Embedded::scene_frag);
    if (std::strcmp(name, "scene_rt.frag") == 0)
        return shader_words(Embedded::scene_rt_frag);
    if (std::strcmp(name, "blit.vert") == 0)
        return shader_words(Embedded::blit_vert);
    if (std::strcmp(name, "blit.frag") == 0)
        return shader_words(Embedded::blit_frag);
    if (std::strcmp(name, "fullscreen.vert") == 0)
        return shader_words(Embedded::fullscreen_vert);
    if (std::strcmp(name, "upscale.frag") == 0)
        return shader_words(Embedded::upscale_frag);
    throw std::logic_error(std::string("Unknown embedded Fluid shader: ") + name);
}
bool has_name(const std::vector<VkExtensionProperties> &exts, const char *name) {
    return std::any_of(exts.begin(), exts.end(),
                       [&](const auto &e) { return std::strcmp(name, e.extensionName) == 0; });
}
} // namespace

struct Renderer::Impl {
    GLFWwindow *window{};
    VkInstance instance{};
    VkDebugUtilsMessengerEXT debug{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    uint32_t graphics_family = UINT32_MAX, present_family = UINT32_MAX;
    VkQueue graphics{}, present{};
    VkSwapchainKHR swapchain{};
    VkFormat format{}, depth_format{};
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    Texture atlas, ui_msaa;
    VkSampler sampler{};
    VkDescriptorSetLayout descriptor_layout{}, blit_set_layout{}, rt_set_layout{};
    VkDescriptorPool descriptor_pool{}, extra_pool{};
    VkDescriptorSet descriptor{};
    std::array<VkDescriptorSet, kMaxScenes> color_sets{}, display_sets{}, rt_sets{};
    VkRenderPass render_pass{}, scene_pass_1x{}, scene_pass_msaa{}, upscale_pass{};
    VkPipelineLayout ui_layout{}, scene_layout{}, scene_rt_layout{}, blit_layout{}, upscale_layout{};
    VkPipeline ui_pipeline{}, blit_pipeline{}, upscale_pipeline{};
    VkPipeline scene_pipeline_1x{}, scene_pipeline_msaa{}, line_pipeline_1x{}, line_pipeline_msaa{};
    VkPipeline scene_rt_pipeline_1x{}, scene_rt_pipeline_msaa{};
    VkCommandPool command_pool{};
    VkCommandBuffer command{};
    VkSemaphore image_available{}, render_finished{};
    VkFence fence{};
    Buffer ui_buffer, mesh_buffer, index_buffer, readback;
    std::string gpu_name = "Not initialized", screenshot_path;
    bool transfer_supported = false;
    bool vulkan12 = false;
    bool rt_available = false;
    bool scene_aa = false;
    bool ray_tracing = false;
    bool dlss = false;
    VkSampleCountFlagBits gui_samples = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits scene_aa_samples = VK_SAMPLE_COUNT_1_BIT;
    uint32_t scratch_alignment = 256;
    std::vector<SceneRange> scene_ranges;
    struct SceneGpu {
        Texture color, msaa_color, depth, upscaled;
        VkFramebuffer scene_fb{}, upscale_fb{};
        uint32_t w = 0, h = 0, out_w = 0, out_h = 0;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        Accel blas, tlas;
        Buffer scratch, instances;
        bool rt_ready = false;
    };
    std::vector<SceneGpu> scene_gpu;
    PFN_vkGetBufferDeviceAddress GetBufferDeviceAddress{};
    PFN_vkCreateAccelerationStructureKHR CreateAccelerationStructureKHR{};
    PFN_vkDestroyAccelerationStructureKHR DestroyAccelerationStructureKHR{};
    PFN_vkGetAccelerationStructureBuildSizesKHR GetAccelerationStructureBuildSizesKHR{};
    PFN_vkCmdBuildAccelerationStructuresKHR CmdBuildAccelerationStructuresKHR{};
    PFN_vkGetAccelerationStructureDeviceAddressKHR GetAccelerationStructureDeviceAddressKHR{};

    ~Impl() { cleanup(); }
    void destroy_buffer(Buffer &b) {
        if (device && b.handle)
            vkDestroyBuffer(device, b.handle, nullptr);
        if (device && b.memory)
            vkFreeMemory(device, b.memory, nullptr);
        b = {};
    }
    void destroy_texture(Texture &t) {
        if (device && t.view)
            vkDestroyImageView(device, t.view, nullptr);
        if (device && t.image)
            vkDestroyImage(device, t.image, nullptr);
        if (device && t.memory)
            vkFreeMemory(device, t.memory, nullptr);
        t = {};
    }
    void destroy_accel(Accel &a) {
        if (device && a.handle && DestroyAccelerationStructureKHR)
            DestroyAccelerationStructureKHR(device, a.handle, nullptr);
        a.handle = VK_NULL_HANDLE;
        destroy_buffer(a.buffer);
        a.size = 0;
        a.address = 0;
    }
    void destroy_scene_gpu(SceneGpu &s) {
        if (device && s.scene_fb)
            vkDestroyFramebuffer(device, s.scene_fb, nullptr);
        if (device && s.upscale_fb)
            vkDestroyFramebuffer(device, s.upscale_fb, nullptr);
        s.scene_fb = s.upscale_fb = VK_NULL_HANDLE;
        destroy_texture(s.color);
        destroy_texture(s.msaa_color);
        destroy_texture(s.depth);
        destroy_texture(s.upscaled);
        destroy_accel(s.blas);
        destroy_accel(s.tlas);
        destroy_buffer(s.scratch);
        destroy_buffer(s.instances);
        s.w = s.h = s.out_w = s.out_h = 0;
        s.samples = VK_SAMPLE_COUNT_1_BIT;
        s.rt_ready = false;
    }
    void destroy_scene_pipelines() {
        auto kill = [&](VkPipeline &p) {
            if (p)
                vkDestroyPipeline(device, p, nullptr);
            p = VK_NULL_HANDLE;
        };
        kill(scene_pipeline_1x);
        kill(scene_pipeline_msaa);
        kill(line_pipeline_1x);
        kill(line_pipeline_msaa);
        kill(scene_rt_pipeline_1x);
        kill(scene_rt_pipeline_msaa);
        kill(upscale_pipeline);
        if (scene_pass_1x)
            vkDestroyRenderPass(device, scene_pass_1x, nullptr);
        if (scene_pass_msaa)
            vkDestroyRenderPass(device, scene_pass_msaa, nullptr);
        if (upscale_pass)
            vkDestroyRenderPass(device, upscale_pass, nullptr);
        scene_pass_1x = scene_pass_msaa = upscale_pass = VK_NULL_HANDLE;
    }
    void destroy_swapchain() {
        if (!device)
            return;
        for (auto f : framebuffers)
            vkDestroyFramebuffer(device, f, nullptr);
        framebuffers.clear();
        if (ui_pipeline)
            vkDestroyPipeline(device, ui_pipeline, nullptr);
        if (blit_pipeline)
            vkDestroyPipeline(device, blit_pipeline, nullptr);
        ui_pipeline = blit_pipeline = VK_NULL_HANDLE;
        if (render_pass)
            vkDestroyRenderPass(device, render_pass, nullptr);
        render_pass = VK_NULL_HANDLE;
        destroy_texture(ui_msaa);
        for (auto v : views)
            vkDestroyImageView(device, v, nullptr);
        views.clear();
        images.clear();
        if (swapchain)
            vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    void cleanup() {
        if (device) {
            vkDeviceWaitIdle(device);
            destroy_swapchain();
            for (auto &s : scene_gpu)
                destroy_scene_gpu(s);
            scene_gpu.clear();
            destroy_scene_pipelines();
            destroy_buffer(ui_buffer);
            destroy_buffer(mesh_buffer);
            destroy_buffer(index_buffer);
            destroy_buffer(readback);
            destroy_texture(atlas);
            if (sampler)
                vkDestroySampler(device, sampler, nullptr);
            if (extra_pool)
                vkDestroyDescriptorPool(device, extra_pool, nullptr);
            if (descriptor_pool)
                vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
            if (ui_layout)
                vkDestroyPipelineLayout(device, ui_layout, nullptr);
            if (scene_layout)
                vkDestroyPipelineLayout(device, scene_layout, nullptr);
            if (scene_rt_layout)
                vkDestroyPipelineLayout(device, scene_rt_layout, nullptr);
            if (blit_layout)
                vkDestroyPipelineLayout(device, blit_layout, nullptr);
            if (upscale_layout)
                vkDestroyPipelineLayout(device, upscale_layout, nullptr);
            if (descriptor_layout)
                vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
            if (blit_set_layout)
                vkDestroyDescriptorSetLayout(device, blit_set_layout, nullptr);
            if (rt_set_layout)
                vkDestroyDescriptorSetLayout(device, rt_set_layout, nullptr);
            if (image_available)
                vkDestroySemaphore(device, image_available, nullptr);
            if (render_finished)
                vkDestroySemaphore(device, render_finished, nullptr);
            if (fence)
                vkDestroyFence(device, fence, nullptr);
            if (command_pool)
                vkDestroyCommandPool(device, command_pool, nullptr);
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        if (surface)
            vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
        if (debug) {
            auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (fn)
                fn(instance, debug, nullptr);
        }
        debug = VK_NULL_HANDLE;
        if (instance)
            vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
    uint32_t memory_type(uint32_t mask, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties mem{};
        vkGetPhysicalDeviceMemoryProperties(physical, &mem);
        for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
            if ((mask & (1u << i)) && (mem.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
        throw std::runtime_error("GPU has no suitable memory type");
    }
    void ensure_buffer(Buffer &b, VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties, bool device_address) {
        if (b.capacity >= size && b.handle)
            return;
        destroy_buffer(b);
        b.capacity = std::max<VkDeviceSize>(size, 4096);
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        ci.size = b.capacity;
        ci.usage = usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device, &ci, nullptr, &b.handle), "Create GPU buffer");
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device, b.handle, &req);
        VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = memory_type(req.memoryTypeBits, properties);
        if (device_address)
            alloc.pNext = &flags;
        check(vkAllocateMemory(device, &alloc, nullptr, &b.memory), "Allocate GPU buffer");
        check(vkBindBufferMemory(device, b.handle, b.memory, 0), "Bind GPU buffer");
        if (device_address && GetBufferDeviceAddress) {
            VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            info.buffer = b.handle;
            b.address = GetBufferDeviceAddress(device, &info);
        }
    }
    void upload(Buffer &b, const void *data, size_t size, VkBufferUsageFlags usage) {
        if (!size)
            return;
        VkBufferUsageFlags extra = 0;
        bool address = false;
        if (rt_available && (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT))) {
            extra = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
            address = true;
        }
        ensure_buffer(b, size, usage | extra,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, address);
        void *mapped{};
        check(vkMapMemory(device, b.memory, 0, size, 0, &mapped), "Map GPU buffer");
        std::memcpy(mapped, data, size);
        vkUnmapMemory(device, b.memory);
    }
    void make_texture(Texture &t, uint32_t width, uint32_t height, VkFormat f, VkImageUsageFlags usage,
                      VkImageAspectFlags aspect, VkSampleCountFlagBits samples) {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent = {width, height, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.format = f;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = usage;
        ci.samples = samples;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateImage(device, &ci, nullptr, &t.image), "Create GPU image");
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, t.image, &req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &alloc, nullptr, &t.memory), "Allocate GPU image");
        check(vkBindImageMemory(device, t.image, t.memory, 0), "Bind GPU image");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = t.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = f;
        view.subresourceRange = {aspect, 0, 1, 0, 1};
        check(vkCreateImageView(device, &view, nullptr, &t.view), "Create GPU image view");
    }
    void transition(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
                    VkAccessFlags src_access, VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
                    VkPipelineStageFlags dst_stage) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = old_layout;
        b.newLayout = new_layout;
        b.srcAccessMask = src_access;
        b.dstAccessMask = dst_access;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    void begin_command() {
        check(vkResetCommandBuffer(command, 0), "Reset command buffer");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &begin), "Begin command buffer");
    }
    void initialize(GLFWwindow *w, const application_cfg &app, const Ui &ui) {
        window = w;
        uint32_t extension_count = 0;
        const char **required = glfwGetRequiredInstanceExtensions(&extension_count);
        if (!required)
            throw std::runtime_error("GLFW cannot load Vulkan. Install a Vulkan-capable graphics driver.");
        std::vector<const char *> extensions(required, required + extension_count);
        uint32_t available_count = 0;
        check(vkEnumerateInstanceExtensionProperties(nullptr, &available_count, nullptr),
              "Enumerate Vulkan extensions");
        std::vector<VkExtensionProperties> available(available_count);
        check(vkEnumerateInstanceExtensionProperties(nullptr, &available_count, available.data()),
              "Enumerate Vulkan extensions");
        auto has_extension = [&](const char *name) { return has_name(available, name); };
        std::vector<const char *> layers;
#ifndef NDEBUG
        uint32_t count = 0;
        check(vkEnumerateInstanceLayerProperties(&count, nullptr), "Enumerate Vulkan layers");
        std::vector<VkLayerProperties> available_layers(count);
        check(vkEnumerateInstanceLayerProperties(&count, available_layers.data()), "Enumerate Vulkan layers");
        for (auto &l : available_layers)
            if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
                layers.push_back("VK_LAYER_KHRONOS_validation");
        if (!layers.empty() && has_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
        VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        ai.pApplicationName = app.app_name.c_str();
        ai.applicationVersion = VK_MAKE_VERSION(app.app_version[0], app.app_version[1], app.app_version[2]);
        ai.pEngineName = app.engine_name.c_str();
        ai.engineVersion =
            VK_MAKE_VERSION(app.engine_version[0], app.engine_version[1], app.engine_version[2]);
        ai.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &ai;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (has_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            if (has_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
                extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        }
#endif
        ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        ci.ppEnabledExtensionNames = extensions.data();
        ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
        ci.ppEnabledLayerNames = layers.data();
        VkResult created = vkCreateInstance(&ci, nullptr, &instance);
        if (created != VK_SUCCESS) {
            ai.apiVersion = VK_API_VERSION_1_0;
            check(vkCreateInstance(&ci, nullptr, &instance), "Create Vulkan instance");
            vulkan12 = false;
        } else
            vulkan12 = true;
        if (!layers.empty() && has_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            if (fn) {
                VkDebugUtilsMessengerCreateInfoEXT di{
                    VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
                di.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                di.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                di.pfnUserCallback = validation_callback;
                check(fn(instance, &di, nullptr, &debug), "Create Vulkan diagnostics");
            }
        }
        check(glfwCreateWindowSurface(instance, window, nullptr, &surface), "Create window surface");
        choose_device();
        create_device();
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.queueFamilyIndex = graphics_family;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(device, &pi, nullptr, &command_pool), "Create command pool");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ca.commandPool = command_pool;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &ca, &command), "Allocate command buffer");
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device, &si, nullptr, &image_available), "Create image semaphore");
        check(vkCreateSemaphore(device, &si, nullptr, &render_finished), "Create render semaphore");
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device, &fi, nullptr, &fence), "Create frame fence");
        create_atlas(ui);
        create_layouts();
        create_scene_passes();
        create_scene_pipelines();
        create_swapchain();
    }
    void choose_device() {
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "Find graphics devices");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "Find graphics devices");
        int best = -1;
        for (auto candidate : devices) {
            uint32_t ec = 0;
            check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &ec, nullptr),
                  "Find device extensions");
            std::vector<VkExtensionProperties> exts(ec);
            check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &ec, exts.data()),
                  "Find device extensions");
            if (!has_name(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
                continue;
            uint32_t qc = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qc, nullptr);
            std::vector<VkQueueFamilyProperties> qs(qc);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qc, qs.data());
            uint32_t gf = UINT32_MAX, pf = UINT32_MAX;
            for (uint32_t i = 0; i < qc; ++i) {
                VkBool32 supported = VK_FALSE;
                check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &supported),
                      "Check presentation queue");
                if (qs[i].queueCount && (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                    gf = i;
                if (qs[i].queueCount && supported)
                    pf = i;
                if (gf == pf && gf != UINT32_MAX)
                    break;
            }
            if (gf == UINT32_MAX || pf == UINT32_MAX)
                continue;
            uint32_t formats = 0, modes = 0;
            check(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &formats, nullptr),
                  "Check surface formats");
            check(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &modes, nullptr),
                  "Check present modes");
            if (!formats || !modes)
                continue;
            VkPhysicalDeviceProperties prop{};
            vkGetPhysicalDeviceProperties(candidate, &prop);
            int score = prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 100;
            if (vulkan12 && has_name(exts, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
                has_name(exts, VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
                has_name(exts, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME))
                score += 250;
            if (score > best) {
                best = score;
                physical = candidate;
                graphics_family = gf;
                present_family = pf;
                gpu_name = prop.deviceName;
            }
        }
        if (!physical)
            throw std::runtime_error(
                "No Vulkan graphics device supports this window. Update your GPU driver.");
        for (auto f : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
            VkFormatProperties p{};
            vkGetPhysicalDeviceFormatProperties(physical, f, &p);
            if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                depth_format = f;
                break;
            }
        }
        if (!depth_format)
            throw std::runtime_error("No supported depth buffer format");
        VkPhysicalDeviceProperties prop{};
        vkGetPhysicalDeviceProperties(physical, &prop);
        gui_samples = pick_samples(prop.limits.framebufferColorSampleCounts, 4);
        scene_aa_samples = pick_samples(
            prop.limits.framebufferColorSampleCounts & prop.limits.framebufferDepthSampleCounts, 4);
    }
    void load_rt() {
        auto load = [&](const char *name) { return vkGetDeviceProcAddr(device, name); };
        GetBufferDeviceAddress =
            reinterpret_cast<PFN_vkGetBufferDeviceAddress>(load("vkGetBufferDeviceAddress"));
        if (!GetBufferDeviceAddress)
            GetBufferDeviceAddress =
                reinterpret_cast<PFN_vkGetBufferDeviceAddress>(load("vkGetBufferDeviceAddressKHR"));
        CreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
            load("vkCreateAccelerationStructureKHR"));
        DestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
            load("vkDestroyAccelerationStructureKHR"));
        GetAccelerationStructureBuildSizesKHR =
            reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
                load("vkGetAccelerationStructureBuildSizesKHR"));
        CmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
            load("vkCmdBuildAccelerationStructuresKHR"));
        GetAccelerationStructureDeviceAddressKHR =
            reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
                load("vkGetAccelerationStructureDeviceAddressKHR"));
        if (!GetBufferDeviceAddress || !CreateAccelerationStructureKHR ||
            !DestroyAccelerationStructureKHR || !GetAccelerationStructureBuildSizesKHR ||
            !CmdBuildAccelerationStructuresKHR || !GetAccelerationStructureDeviceAddressKHR)
            rt_available = false;
    }
    void create_device() {
        float priority = 1;
        std::vector<VkDeviceQueueCreateInfo> queues;
        for (auto family : {graphics_family, present_family}) {
            if (!queues.empty() && family == graphics_family)
                continue;
            VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            q.queueFamilyIndex = family;
            q.queueCount = 1;
            q.pQueuePriorities = &priority;
            queues.push_back(q);
        }
        std::vector<const char *> exts{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        uint32_t ec = 0;
        check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &ec, nullptr),
              "Read device extensions");
        std::vector<VkExtensionProperties> properties(ec);
        check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &ec, properties.data()),
              "Read device extensions");
        for (auto &e : properties)
            if (std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0)
                exts.push_back("VK_KHR_portability_subset");
        VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR as_f{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
        VkPhysicalDeviceRayQueryFeaturesKHR rq_f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
        if (vulkan12 && has_name(properties, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
            has_name(properties, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
            has_name(properties, VK_KHR_RAY_QUERY_EXTENSION_NAME)) {
            VkPhysicalDeviceVulkan12Features v12q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            VkPhysicalDeviceAccelerationStructureFeaturesKHR as_q{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
            VkPhysicalDeviceRayQueryFeaturesKHR rq_q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
            VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            f2.pNext = &v12q;
            v12q.pNext = &as_q;
            as_q.pNext = &rq_q;
            vkGetPhysicalDeviceFeatures2(physical, &f2);
            rt_available = v12q.bufferDeviceAddress && as_q.accelerationStructure && rq_q.rayQuery;
        }
        VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        ci.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
        ci.pQueueCreateInfos = queues.data();
        if (rt_available) {
            exts.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            exts.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            exts.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            if (has_name(properties, "VK_KHR_spirv_1_4"))
                exts.push_back("VK_KHR_spirv_1_4");
            if (has_name(properties, "VK_KHR_shader_float_controls"))
                exts.push_back("VK_KHR_shader_float_controls");
            v12.bufferDeviceAddress = VK_TRUE;
            as_f.accelerationStructure = VK_TRUE;
            rq_f.rayQuery = VK_TRUE;
            v12.pNext = &as_f;
            as_f.pNext = &rq_f;
            ci.pNext = &v12;
            VkPhysicalDeviceAccelerationStructurePropertiesKHR as_props{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
            VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            p2.pNext = &as_props;
            vkGetPhysicalDeviceProperties2(physical, &p2);
            if (as_props.minAccelerationStructureScratchOffsetAlignment)
                scratch_alignment = as_props.minAccelerationStructureScratchOffsetAlignment;
        }
        ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
        ci.ppEnabledExtensionNames = exts.data();
        check(vkCreateDevice(physical, &ci, nullptr, &device), "Create Vulkan device");
        vkGetDeviceQueue(device, graphics_family, 0, &graphics);
        vkGetDeviceQueue(device, present_family, 0, &present);
        if (rt_available)
            load_rt();
    }
    void create_atlas(const Ui &ui) {
        auto &font = ui.font();
        Buffer staging;
        try {
            upload(staging, font.pixels().data(), font.pixels().size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            make_texture(atlas, static_cast<uint32_t>(font.width()), static_cast<uint32_t>(font.height()),
                         VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT);
            begin_command();
            transition(atlas.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {static_cast<uint32_t>(font.width()), static_cast<uint32_t>(font.height()), 1};
            vkCmdCopyBufferToImage(command, staging.handle, atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1, &copy);
            transition(atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            check(vkEndCommandBuffer(command), "Finish font upload");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &command;
            check(vkQueueSubmit(graphics, 1, &submit, VK_NULL_HANDLE), "Upload font atlas");
            check(vkQueueWaitIdle(graphics), "Wait for font upload");
        } catch (...) {
            destroy_buffer(staging);
            throw;
        }
        destroy_buffer(staging);
        VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sc.magFilter = sc.minFilter = VK_FILTER_LINEAR;
        sc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sc.maxLod = 0;
        check(vkCreateSampler(device, &sc, nullptr, &sampler), "Create font sampler");
    }
    VkDescriptorSetLayout make_sampler_layout() {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        lc.bindingCount = 1;
        lc.pBindings = &binding;
        VkDescriptorSetLayout layout{};
        check(vkCreateDescriptorSetLayout(device, &lc, nullptr, &layout), "Create sampler layout");
        return layout;
    }
    void create_layouts() {
        descriptor_layout = make_sampler_layout();
        blit_set_layout = make_sampler_layout();
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pc.maxSets = 1;
        pc.poolSizeCount = 1;
        pc.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &pc, nullptr, &descriptor_pool), "Create font descriptor pool");
        VkDescriptorSetAllocateInfo ac{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ac.descriptorPool = descriptor_pool;
        ac.descriptorSetCount = 1;
        ac.pSetLayouts = &descriptor_layout;
        check(vkAllocateDescriptorSets(device, &ac, &descriptor), "Allocate font descriptor");
        VkDescriptorImageInfo image_info{sampler, atlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptor;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.setLayoutCount = 1;
        li.pSetLayouts = &descriptor_layout;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &range;
        check(vkCreatePipelineLayout(device, &li, nullptr, &ui_layout), "Create UI pipeline layout");
        range = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ScenePush)};
        li.setLayoutCount = 0;
        li.pSetLayouts = nullptr;
        check(vkCreatePipelineLayout(device, &li, nullptr, &scene_layout), "Create scene pipeline layout");
        range = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(BlitPush)};
        li.setLayoutCount = 1;
        li.pSetLayouts = &blit_set_layout;
        check(vkCreatePipelineLayout(device, &li, nullptr, &blit_layout), "Create blit pipeline layout");
        range = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 2};
        check(vkCreatePipelineLayout(device, &li, nullptr, &upscale_layout), "Create upscale pipeline layout");
        std::vector<VkDescriptorPoolSize> extra_sizes{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxScenes * 2}};
        uint32_t extra_sets = kMaxScenes * 2;
        if (rt_available) {
            VkDescriptorSetLayoutBinding rt_binding{};
            rt_binding.binding = 0;
            rt_binding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            rt_binding.descriptorCount = 1;
            rt_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutCreateInfo rtc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            rtc.bindingCount = 1;
            rtc.pBindings = &rt_binding;
            check(vkCreateDescriptorSetLayout(device, &rtc, nullptr, &rt_set_layout),
                  "Create ray tracing layout");
            range = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ScenePush)};
            li.pSetLayouts = &rt_set_layout;
            check(vkCreatePipelineLayout(device, &li, nullptr, &scene_rt_layout),
                  "Create ray tracing pipeline layout");
            extra_sizes.push_back({VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, kMaxScenes});
            extra_sets += kMaxScenes;
        }
        VkDescriptorPoolCreateInfo epc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        epc.maxSets = extra_sets;
        epc.poolSizeCount = static_cast<uint32_t>(extra_sizes.size());
        epc.pPoolSizes = extra_sizes.data();
        check(vkCreateDescriptorPool(device, &epc, nullptr, &extra_pool), "Create scene descriptor pool");
        std::array<VkDescriptorSetLayout, kMaxScenes> sampler_layouts;
        sampler_layouts.fill(blit_set_layout);
        ac.descriptorPool = extra_pool;
        ac.descriptorSetCount = kMaxScenes;
        ac.pSetLayouts = sampler_layouts.data();
        check(vkAllocateDescriptorSets(device, &ac, color_sets.data()), "Allocate scene color descriptors");
        check(vkAllocateDescriptorSets(device, &ac, display_sets.data()),
              "Allocate scene display descriptors");
        if (rt_available) {
            std::array<VkDescriptorSetLayout, kMaxScenes> rt_layouts;
            rt_layouts.fill(rt_set_layout);
            ac.pSetLayouts = rt_layouts.data();
            check(vkAllocateDescriptorSets(device, &ac, rt_sets.data()), "Allocate ray tracing descriptors");
        }
    }
    VkRenderPass make_scene_pass(VkSampleCountFlagBits samples) {
        const bool msaa = samples != VK_SAMPLE_COUNT_1_BIT;
        std::array<VkAttachmentDescription, 3> a{};
        uint32_t count = msaa ? 3 : 2;
        a[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        a[0].samples = samples;
        a[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a[0].storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
        a[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a[0].finalLayout =
            msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        a[1].format = depth_format;
        a[1].samples = samples;
        a[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        if (msaa) {
            a[2].format = VK_FORMAT_R8G8B8A8_UNORM;
            a[2].samples = VK_SAMPLE_COUNT_1_BIT;
            a[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            a[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            a[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
            resolve{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &color;
        sub.pDepthStencilAttachment = &depth_ref;
        if (msaa)
            sub.pResolveAttachments = &resolve;
        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask =
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        ri.attachmentCount = count;
        ri.pAttachments = a.data();
        ri.subpassCount = 1;
        ri.pSubpasses = &sub;
        ri.dependencyCount = static_cast<uint32_t>(deps.size());
        ri.pDependencies = deps.data();
        VkRenderPass pass{};
        check(vkCreateRenderPass(device, &ri, nullptr, &pass), "Create scene render pass");
        return pass;
    }
    VkSampleCountFlagBits format_samples(VkFormat f, uint32_t want) {
        VkImageFormatProperties ifp{};
        if (vkGetPhysicalDeviceImageFormatProperties(physical, f, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0,
                                                     &ifp) != VK_SUCCESS)
            return VK_SAMPLE_COUNT_1_BIT;
        return pick_samples(ifp.sampleCounts, want);
    }
    void create_scene_passes() {
        scene_aa_samples = format_samples(VK_FORMAT_R8G8B8A8_UNORM, scene_aa_samples);
        scene_pass_1x = make_scene_pass(VK_SAMPLE_COUNT_1_BIT);
        if (scene_aa_samples != VK_SAMPLE_COUNT_1_BIT)
            scene_pass_msaa = make_scene_pass(scene_aa_samples);
        VkAttachmentDescription a{};
        a.format = VK_FORMAT_R8G8B8A8_UNORM;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &color;
        VkSubpassDependency dep{};
        dep.srcSubpass = 0;
        dep.dstSubpass = VK_SUBPASS_EXTERNAL;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        ri.attachmentCount = 1;
        ri.pAttachments = &a;
        ri.subpassCount = 1;
        ri.pSubpasses = &sub;
        ri.dependencyCount = 1;
        ri.pDependencies = &dep;
        check(vkCreateRenderPass(device, &ri, nullptr, &upscale_pass), "Create upscale render pass");
    }
    void create_scene_pipelines() {
        scene_pipeline_1x =
            create_pipeline(PipeKind::Scene, scene_pass_1x, scene_layout, VK_SAMPLE_COUNT_1_BIT);
        line_pipeline_1x = create_pipeline(PipeKind::Line, scene_pass_1x, scene_layout, VK_SAMPLE_COUNT_1_BIT);
        upscale_pipeline =
            create_pipeline(PipeKind::Upscale, upscale_pass, upscale_layout, VK_SAMPLE_COUNT_1_BIT);
        if (scene_pass_msaa) {
            scene_pipeline_msaa =
                create_pipeline(PipeKind::Scene, scene_pass_msaa, scene_layout, scene_aa_samples);
            line_pipeline_msaa =
                create_pipeline(PipeKind::Line, scene_pass_msaa, scene_layout, scene_aa_samples);
        }
        if (rt_available && scene_rt_layout) {
            scene_rt_pipeline_1x =
                create_pipeline(PipeKind::SceneRt, scene_pass_1x, scene_rt_layout, VK_SAMPLE_COUNT_1_BIT);
            if (scene_pass_msaa)
                scene_rt_pipeline_msaa = create_pipeline(PipeKind::SceneRt, scene_pass_msaa, scene_rt_layout,
                                                         scene_aa_samples);
        }
    }
    void create_swapchain() {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width <= 0 || height <= 0)
            return;
        check(vkDeviceWaitIdle(device), "Wait before resizing");
        destroy_swapchain();
        VkSurfaceCapabilitiesKHR caps{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps),
              "Read surface capabilities");
        uint32_t count = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr),
              "Read surface formats");
        if (!count)
            throw std::runtime_error("Window surface has no color formats");
        std::vector<VkSurfaceFormatKHR> formats(count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()),
              "Read surface formats");
        VkSurfaceFormatKHR selected = formats.front();
        if (formats.size() == 1 && selected.format == VK_FORMAT_UNDEFINED)
            selected = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        for (auto f : formats)
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                selected = f;
                break;
            }
        format = selected.format;
        gui_samples = format_samples(format, 4);
        extent = caps.currentExtent;
        if (extent.width == UINT32_MAX)
            extent = {std::clamp(static_cast<uint32_t>(width), caps.minImageExtent.width,
                                 caps.maxImageExtent.width),
                      std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height,
                                 caps.maxImageExtent.height)};
        uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount)
            image_count = std::min(image_count, caps.maxImageCount);
        transfer_supported = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface = surface;
        ci.minImageCount = image_count;
        ci.imageFormat = format;
        ci.imageColorSpace = selected.colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (transfer_supported ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
        std::array<uint32_t, 2> families{graphics_family, present_family};
        ci.imageSharingMode =
            graphics_family == present_family ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
        if (ci.imageSharingMode == VK_SHARING_MODE_CONCURRENT) {
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = families.data();
        }
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (caps.supportedCompositeAlpha & alpha) {
                ci.compositeAlpha = alpha;
                break;
            }
        ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        ci.clipped = VK_TRUE;
        check(vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain), "Create swapchain");
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "Read swapchain images");
        images.resize(count);
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()), "Read swapchain images");
        views.resize(count, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < count; ++i) {
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = images[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = format;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            check(vkCreateImageView(device, &vi, nullptr, &views[i]), "Create swapchain image view");
        }
        create_ui_pass();
        if (gui_samples != VK_SAMPLE_COUNT_1_BIT)
            make_texture(ui_msaa, extent.width, extent.height, format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, gui_samples);
        framebuffers.resize(count, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < count; ++i) {
            std::array<VkImageView, 2> attachments{};
            uint32_t n = 1;
            if (gui_samples != VK_SAMPLE_COUNT_1_BIT) {
                attachments[0] = ui_msaa.view;
                attachments[1] = views[i];
                n = 2;
            } else
                attachments[0] = views[i];
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = render_pass;
            fi.attachmentCount = n;
            fi.pAttachments = attachments.data();
            fi.width = extent.width;
            fi.height = extent.height;
            fi.layers = 1;
            check(vkCreateFramebuffer(device, &fi, nullptr, &framebuffers[i]), "Create framebuffer");
        }
        ui_pipeline = create_pipeline(PipeKind::Ui, render_pass, ui_layout, gui_samples);
        blit_pipeline = create_pipeline(PipeKind::Blit, render_pass, blit_layout, gui_samples);
    }
    void create_ui_pass() {
        const bool msaa = gui_samples != VK_SAMPLE_COUNT_1_BIT;
        std::array<VkAttachmentDescription, 2> a{};
        a[0].format = format;
        a[0].samples = gui_samples;
        a[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a[0].storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
        a[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a[0].finalLayout = msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        if (msaa) {
            a[1].format = format;
            a[1].samples = VK_SAMPLE_COUNT_1_BIT;
            a[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            a[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            a[1].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            resolve{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &color;
        if (msaa)
            sub.pResolveAttachments = &resolve;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        ri.attachmentCount = msaa ? 2 : 1;
        ri.pAttachments = a.data();
        ri.subpassCount = 1;
        ri.pSubpasses = &sub;
        ri.dependencyCount = 1;
        ri.pDependencies = &dep;
        check(vkCreateRenderPass(device, &ri, nullptr, &render_pass), "Create render pass");
    }
    bool srgb_surface() const {
        return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB ||
               format == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
    }
    VkPipeline create_pipeline(PipeKind kind, VkRenderPass pass, VkPipelineLayout layout,
                               VkSampleCountFlagBits samples) {
        const bool scene_vtx = kind == PipeKind::Scene || kind == PipeKind::Line || kind == PipeKind::SceneRt;
        const bool lines = kind == PipeKind::Line;
        const char *vs_name = "ui.vert", *fs_name = "ui.frag";
        if (kind == PipeKind::Scene || kind == PipeKind::Line) {
            vs_name = "scene.vert";
            fs_name = "scene.frag";
        } else if (kind == PipeKind::SceneRt) {
            vs_name = "scene.vert";
            fs_name = "scene_rt.frag";
        } else if (kind == PipeKind::Blit) {
            vs_name = "blit.vert";
            fs_name = "blit.frag";
        } else if (kind == PipeKind::Upscale) {
            vs_name = "fullscreen.vert";
            fs_name = "upscale.frag";
        }
        auto vert = shader_code(vs_name), frag = shader_code(fs_name);
        VkShaderModule vs{}, fs{};
        auto module = [&](const std::vector<uint32_t> &bytes, VkShaderModule &result) {
            VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            ci.codeSize = bytes.size() * 4;
            ci.pCode = bytes.data();
            check(vkCreateShaderModule(device, &ci, nullptr, &result), "Create shader module");
        };
        VkPipeline result{};
        try {
            module(vert, vs);
            module(frag, fs);
            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            for (auto &s : stages) {
                s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                s.pName = "main";
            }
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vs;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fs;
            VkBool32 target_srgb = srgb_surface() ? VK_TRUE : VK_FALSE;
            VkSpecializationMapEntry color_entry{0, 0, sizeof(target_srgb)};
            VkSpecializationInfo color_specialization{1, &color_entry, sizeof(target_srgb), &target_srgb};
            if (kind == PipeKind::Ui || kind == PipeKind::Blit)
                stages[1].pSpecializationInfo = &color_specialization;
            VkVertexInputBindingDescription binding{
                0, static_cast<uint32_t>(scene_vtx ? sizeof(MeshVertex) : sizeof(UiVertex)),
                VK_VERTEX_INPUT_RATE_VERTEX};
            std::array<VkVertexInputAttributeDescription, 3> attrs{};
            if (scene_vtx) {
                attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                            static_cast<uint32_t>(offsetof(MeshVertex, position))};
                attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                            static_cast<uint32_t>(offsetof(MeshVertex, normal))};
            } else if (kind == PipeKind::Ui) {
                attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT,
                            static_cast<uint32_t>(offsetof(UiVertex, position))};
                attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(UiVertex, uv))};
                attrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                            static_cast<uint32_t>(offsetof(UiVertex, color))};
            }
            VkPipelineVertexInputStateCreateInfo vi{
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            if (kind != PipeKind::Blit && kind != PipeKind::Upscale) {
                vi.vertexBindingDescriptionCount = 1;
                vi.pVertexBindingDescriptions = &binding;
                vi.vertexAttributeDescriptionCount = scene_vtx ? 2 : 3;
                vi.pVertexAttributeDescriptions = attrs.data();
            }
            VkPipelineInputAssemblyStateCreateInfo ia{
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            ia.topology = lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            vp.viewportCount = 1;
            vp.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo rs{
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            rs.polygonMode = VK_POLYGON_MODE_FILL;
            rs.cullMode = VK_CULL_MODE_NONE;
            rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rs.lineWidth = 1;
            VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            ms.rasterizationSamples = samples;
            VkPipelineDepthStencilStateCreateInfo ds{
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            ds.depthTestEnable = scene_vtx;
            ds.depthWriteEnable = scene_vtx;
            ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            VkPipelineColorBlendAttachmentState blend{};
            blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blend.blendEnable = kind != PipeKind::Upscale ? VK_TRUE : VK_FALSE;
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.colorBlendOp = VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alphaBlendOp = VK_BLEND_OP_ADD;
            VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            bs.attachmentCount = 1;
            bs.pAttachments = &blend;
            std::array<VkDynamicState, 2> dyn{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dynamic.dynamicStateCount = static_cast<uint32_t>(dyn.size());
            dynamic.pDynamicStates = dyn.data();
            VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            gi.stageCount = static_cast<uint32_t>(stages.size());
            gi.pStages = stages.data();
            gi.pVertexInputState = &vi;
            gi.pInputAssemblyState = &ia;
            gi.pViewportState = &vp;
            gi.pRasterizationState = &rs;
            gi.pMultisampleState = &ms;
            gi.pDepthStencilState = &ds;
            gi.pColorBlendState = &bs;
            gi.pDynamicState = &dynamic;
            gi.layout = layout;
            gi.renderPass = pass;
            check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gi, nullptr, &result),
                  "Create graphics pipeline");
        } catch (...) {
            if (vs)
                vkDestroyShaderModule(device, vs, nullptr);
            if (fs)
                vkDestroyShaderModule(device, fs, nullptr);
            throw;
        }
        vkDestroyShaderModule(device, vs, nullptr);
        vkDestroyShaderModule(device, fs, nullptr);
        return result;
    }
    void upload_draw_data(const DrawList &draw) {
        upload(ui_buffer, draw.vertices.data(), draw.vertices.size() * sizeof(UiVertex),
               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;
        scene_ranges.assign(draw.scenes.size(), {});
        for (size_t s = 0; s < draw.scenes.size(); ++s) {
            const auto &scene = draw.scenes[s];
            auto &range = scene_ranges[s];
            range.vertex = static_cast<uint32_t>(vertices.size());
            if (scene.mesh) {
                range.vertex_count = static_cast<uint32_t>(scene.mesh->vertices.size());
                vertices.insert(vertices.end(), scene.mesh->vertices.begin(), scene.mesh->vertices.end());
                range.first = static_cast<uint32_t>(indices.size());
                range.count = static_cast<uint32_t>(scene.mesh->indices.size());
                indices.insert(indices.end(), scene.mesh->indices.begin(), scene.mesh->indices.end());
                if (scene.wireframe) {
                    range.line_first = static_cast<uint32_t>(indices.size());
                    for (size_t i = 0; i + 2 < scene.mesh->indices.size(); i += 3) {
                        auto a = scene.mesh->indices[i], b = scene.mesh->indices[i + 1],
                             c = scene.mesh->indices[i + 2];
                        indices.insert(indices.end(), {a, b, b, c, c, a});
                    }
                    range.line_count = static_cast<uint32_t>(indices.size()) - range.line_first;
                }
            }
            if (scene.grid) {
                range.grid_first = static_cast<uint32_t>(indices.size());
                for (int i = -10; i <= 10; ++i) {
                    float v = static_cast<float>(i) * 0.5f;
                    for (auto p : {glm::vec3(v, -1.15f, -5), glm::vec3(v, -1.15f, 5),
                                   glm::vec3(-5, -1.15f, v), glm::vec3(5, -1.15f, v)}) {
                        indices.push_back(static_cast<uint32_t>(vertices.size()) - range.vertex);
                        vertices.push_back({p, {0, 1, 0}});
                    }
                }
                range.grid_count = static_cast<uint32_t>(indices.size()) - range.grid_first;
            }
        }
        upload(mesh_buffer, vertices.data(), vertices.size() * sizeof(MeshVertex),
               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        upload(index_buffer, indices.data(), indices.size() * sizeof(uint32_t),
               VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }
    VkRect2D scissor(Rect r, float sx, float sy) {
        float left = std::clamp(r.x * sx, 0.0f, static_cast<float>(extent.width));
        float top = std::clamp(r.y * sy, 0.0f, static_cast<float>(extent.height));
        float right = std::clamp((r.x + std::max(0.0f, r.w)) * sx, left, static_cast<float>(extent.width));
        float bottom = std::clamp((r.y + std::max(0.0f, r.h)) * sy, top, static_cast<float>(extent.height));
        return {{static_cast<int32_t>(std::floor(left)), static_cast<int32_t>(std::floor(top))},
                {static_cast<uint32_t>(std::ceil(right) - std::floor(left)),
                 static_cast<uint32_t>(std::ceil(bottom) - std::floor(top))}};
    }
    void bind_sampler(VkDescriptorSet set, VkImageView view) {
        VkDescriptorImageInfo image{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    void ensure_scene_target(uint32_t index, uint32_t in_w, uint32_t in_h, uint32_t out_w, uint32_t out_h,
                             VkSampleCountFlagBits samples, bool want_upscale) {
        if (scene_gpu.size() <= index)
            scene_gpu.resize(index + 1);
        auto &s = scene_gpu[index];
        if (s.scene_fb && s.w == in_w && s.h == in_h && s.out_w == out_w && s.out_h == out_h &&
            s.samples == samples && (!want_upscale || s.upscale_fb))
            return;
        destroy_scene_gpu(s);
        s.w = in_w;
        s.h = in_h;
        s.out_w = out_w;
        s.out_h = out_h;
        s.samples = samples;
        const bool msaa = samples != VK_SAMPLE_COUNT_1_BIT;
        if (msaa) {
            make_texture(s.msaa_color, in_w, in_h, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, samples);
            make_texture(s.depth, in_w, in_h, depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT, samples);
            make_texture(s.color, in_w, in_h, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT);
            std::array<VkImageView, 3> attachments{s.msaa_color.view, s.depth.view, s.color.view};
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = scene_pass_msaa;
            fi.attachmentCount = 3;
            fi.pAttachments = attachments.data();
            fi.width = in_w;
            fi.height = in_h;
            fi.layers = 1;
            check(vkCreateFramebuffer(device, &fi, nullptr, &s.scene_fb), "Create scene framebuffer");
        } else {
            make_texture(s.color, in_w, in_h, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT);
            make_texture(s.depth, in_w, in_h, depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT, VK_SAMPLE_COUNT_1_BIT);
            std::array<VkImageView, 2> attachments{s.color.view, s.depth.view};
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = scene_pass_1x;
            fi.attachmentCount = 2;
            fi.pAttachments = attachments.data();
            fi.width = in_w;
            fi.height = in_h;
            fi.layers = 1;
            check(vkCreateFramebuffer(device, &fi, nullptr, &s.scene_fb), "Create scene framebuffer");
        }
        bind_sampler(color_sets[index], s.color.view);
        if (want_upscale) {
            make_texture(s.upscaled, out_w, out_h, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT);
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = upscale_pass;
            fi.attachmentCount = 1;
            fi.pAttachments = &s.upscaled.view;
            fi.width = out_w;
            fi.height = out_h;
            fi.layers = 1;
            check(vkCreateFramebuffer(device, &fi, nullptr, &s.upscale_fb), "Create upscale framebuffer");
            bind_sampler(display_sets[index], s.upscaled.view);
        } else
            bind_sampler(display_sets[index], s.color.view);
    }
    void ensure_accel(Accel &a, VkDeviceSize size, VkAccelerationStructureTypeKHR type) {
        if (a.handle && a.size >= size)
            return;
        if (a.handle) {
            DestroyAccelerationStructureKHR(device, a.handle, nullptr);
            a.handle = VK_NULL_HANDLE;
        }
        if (a.buffer.capacity < size) {
            destroy_buffer(a.buffer);
            ensure_buffer(a.buffer, size,
                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true);
        }
        VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        ci.buffer = a.buffer.handle;
        ci.size = size;
        ci.type = type;
        check(CreateAccelerationStructureKHR(device, &ci, nullptr, &a.handle),
              "Create acceleration structure");
        a.size = size;
        VkAccelerationStructureDeviceAddressInfoKHR info{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        info.accelerationStructure = a.handle;
        a.address = GetAccelerationStructureDeviceAddressKHR(device, &info);
    }
    void as_barrier(VkPipelineStageFlags dst_stage, VkAccessFlags dst_access) {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask = dst_access;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, dst_stage, 0, 1,
                             &barrier, 0, nullptr, 0, nullptr);
    }
    bool build_scene_as(uint32_t index, const SceneRange &range, float rotation) {
        if (!rt_available || range.count < 3 || range.vertex_count == 0 || !mesh_buffer.address ||
            !index_buffer.address)
            return false;
        auto &s = scene_gpu[index];
        s.rt_ready = false;
        VkAccelerationStructureGeometryKHR geo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geo.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geo.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geo.geometry.triangles.vertexData.deviceAddress =
            mesh_buffer.address + static_cast<VkDeviceSize>(range.vertex) * sizeof(MeshVertex);
        geo.geometry.triangles.vertexStride = sizeof(MeshVertex);
        geo.geometry.triangles.maxVertex = range.vertex_count - 1;
        geo.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        geo.geometry.triangles.indexData.deviceAddress =
            index_buffer.address + static_cast<VkDeviceSize>(range.first) * sizeof(uint32_t);
        uint32_t primitives = range.count / 3;
        if (primitives == 0)
            return false;
        VkAccelerationStructureBuildGeometryInfoKHR build{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build.geometryCount = 1;
        build.pGeometries = &geo;
        VkAccelerationStructureBuildSizesInfoKHR sizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        GetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build,
                                              &primitives, &sizes);
        ensure_accel(s.blas, sizes.accelerationStructureSize,
                     VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);
        VkDeviceSize scratch_need =
            std::max(sizes.buildScratchSize, static_cast<VkDeviceSize>(scratch_alignment));
        VkAccelerationStructureGeometryKHR inst_geo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        inst_geo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        inst_geo.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        VkAccelerationStructureBuildGeometryInfoKHR tlas_build{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        tlas_build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        tlas_build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        tlas_build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        tlas_build.geometryCount = 1;
        tlas_build.pGeometries = &inst_geo;
        uint32_t instance_count = 1;
        VkAccelerationStructureBuildSizesInfoKHR tlas_sizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        GetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                              &tlas_build, &instance_count, &tlas_sizes);
        ensure_accel(s.tlas, tlas_sizes.accelerationStructureSize,
                     VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);
        scratch_need = std::max(scratch_need, tlas_sizes.buildScratchSize) + scratch_alignment;
        ensure_buffer(s.scratch, scratch_need,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true);
        float c = std::cos(rotation), sn = std::sin(rotation);
        VkAccelerationStructureInstanceKHR inst{};
        inst.transform.matrix[0][0] = c;
        inst.transform.matrix[0][2] = -sn;
        inst.transform.matrix[1][1] = 1;
        inst.transform.matrix[2][0] = sn;
        inst.transform.matrix[2][2] = c;
        inst.mask = 0xFF;
        inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        inst.accelerationStructureReference = s.blas.address;
        ensure_buffer(s.instances, sizeof(inst),
                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
        void *mapped{};
        check(vkMapMemory(device, s.instances.memory, 0, sizeof(inst), 0, &mapped), "Map instance buffer");
        std::memcpy(mapped, &inst, sizeof(inst));
        vkUnmapMemory(device, s.instances.memory);
        inst_geo.geometry.instances.data.deviceAddress = s.instances.address;
        VkDeviceAddress scratch_addr =
            (s.scratch.address + scratch_alignment - 1) & ~static_cast<VkDeviceAddress>(scratch_alignment - 1);
        build.dstAccelerationStructure = s.blas.handle;
        build.scratchData.deviceAddress = scratch_addr;
        VkAccelerationStructureBuildRangeInfoKHR blas_range{};
        blas_range.primitiveCount = primitives;
        const VkAccelerationStructureBuildRangeInfoKHR *blas_ranges = &blas_range;
        CmdBuildAccelerationStructuresKHR(command, 1, &build, &blas_ranges);
        as_barrier(VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                   VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
        tlas_build.dstAccelerationStructure = s.tlas.handle;
        tlas_build.scratchData.deviceAddress = scratch_addr;
        tlas_build.pGeometries = &inst_geo;
        VkAccelerationStructureBuildRangeInfoKHR tlas_range{};
        tlas_range.primitiveCount = 1;
        const VkAccelerationStructureBuildRangeInfoKHR *tlas_ranges = &tlas_range;
        CmdBuildAccelerationStructuresKHR(command, 1, &tlas_build, &tlas_ranges);
        VkWriteDescriptorSetAccelerationStructureKHR as_write{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
        as_write.accelerationStructureCount = 1;
        as_write.pAccelerationStructures = &s.tlas.handle;
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.pNext = &as_write;
        write.dstSet = rt_sets[index];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        s.rt_ready = true;
        return true;
    }
    void record_scene(const DrawList &draw, uint32_t index, bool use_rt, bool use_aa, bool use_dlss) {
        const auto &scene = draw.scenes[index];
        const auto &range = scene_ranges[index];
        auto &gpu = scene_gpu[index];
        VkRenderPass pass = use_aa ? scene_pass_msaa : scene_pass_1x;
        std::array<VkClearValue, 2> clear{};
        clear[0].color = {{0, 0, 0, 0}};
        clear[1].depthStencil = {1, 0};
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = pass;
        begin.framebuffer = gpu.scene_fb;
        begin.renderArea.extent = {gpu.w, gpu.h};
        begin.clearValueCount = 2;
        begin.pClearValues = clear.data();
        vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{0, 0, static_cast<float>(gpu.w), static_cast<float>(gpu.h), 0, 1};
        VkRect2D full{{0, 0}, {gpu.w, gpu.h}};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &full);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &mesh_buffer.handle, &offset);
        vkCmdBindIndexBuffer(command, index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
        glm::mat4 view = scene.camera.view(),
                  projection = scene.camera.projection(scene.bounds.w / scene.bounds.h);
        ScenePush push;
        push.eye = glm::inverse(view)[3];
        VkPipeline line_pipe = use_aa ? line_pipeline_msaa : line_pipeline_1x;
        VkPipeline mesh_pipe = use_aa ? scene_pipeline_msaa : scene_pipeline_1x;
        if (use_rt && !scene.wireframe) {
            auto rt_pipe = use_aa ? scene_rt_pipeline_msaa : scene_rt_pipeline_1x;
            if (rt_pipe)
                mesh_pipe = rt_pipe;
        }
        if (range.grid_count && line_pipe) {
            push.mvp = projection * view;
            push.tint = {0.30f, 0.34f, 0.43f, 0.35f};
            push.material = {0, 1, 1, 1};
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipe);
            vkCmdPushConstants(command, scene_layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push),
                               &push);
            vkCmdDrawIndexed(command, range.grid_count, 1, range.grid_first,
                             static_cast<int32_t>(range.vertex), 0);
        }
        if (range.count) {
            push.mvp = projection * view * glm::rotate(glm::mat4(1), scene.rotation, glm::vec3(0, 1, 0));
            push.rotation = {std::cos(scene.rotation), std::sin(scene.rotation), 0, 0};
            push.tint = {scene.tint.r, scene.tint.g, scene.tint.b, scene.tint.a};
            push.material = {scene.metallic, scene.roughness, scene.exposure, scene.wireframe ? 1.0f : 0.0f};
            VkPipelineLayout layout = scene_layout;
            if (use_rt && !scene.wireframe && mesh_pipe != line_pipe && scene_rt_layout)
                layout = scene_rt_layout;
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              scene.wireframe ? line_pipe : mesh_pipe);
            if (layout == scene_rt_layout)
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                                        &rt_sets[index], 0, nullptr);
            vkCmdPushConstants(command, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(push), &push);
            vkCmdDrawIndexed(command, scene.wireframe ? range.line_count : range.count, 1,
                             scene.wireframe ? range.line_first : range.first,
                             static_cast<int32_t>(range.vertex), 0);
        }
        vkCmdEndRenderPass(command);
        if (use_dlss && gpu.upscale_fb) {
            VkRenderPassBeginInfo up{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            up.renderPass = upscale_pass;
            up.framebuffer = gpu.upscale_fb;
            up.renderArea.extent = {gpu.out_w, gpu.out_h};
            vkCmdBeginRenderPass(command, &up, VK_SUBPASS_CONTENTS_INLINE);
            VkViewport up_vp{0, 0, static_cast<float>(gpu.out_w), static_cast<float>(gpu.out_h), 0, 1};
            VkRect2D up_sc{{0, 0}, {gpu.out_w, gpu.out_h}};
            vkCmdSetViewport(command, 0, 1, &up_vp);
            vkCmdSetScissor(command, 0, 1, &up_sc);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, upscale_pipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, upscale_layout, 0, 1,
                                    &color_sets[index], 0, nullptr);
            std::array<float, 2> texel{1.0f / static_cast<float>(gpu.w), 1.0f / static_cast<float>(gpu.h)};
            vkCmdPushConstants(command, upscale_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(texel),
                               texel.data());
            vkCmdDraw(command, 3, 1, 0, 0);
            vkCmdEndRenderPass(command);
        }
    }
    void blit_scene(const DrawList &draw, const DrawCommand &dc, float width, float height, float sx,
                    float sy) {
        if (dc.scene_index >= draw.scenes.size() || dc.scene_index >= scene_gpu.size())
            return;
        const auto &scene = draw.scenes[dc.scene_index];
        Rect clip{std::max(dc.clip.x, scene.bounds.x), std::max(dc.clip.y, scene.bounds.y), 0, 0};
        clip.w = std::min(dc.clip.x + dc.clip.w, scene.bounds.x + scene.bounds.w) - clip.x;
        clip.h = std::min(dc.clip.y + dc.clip.h, scene.bounds.y + scene.bounds.h) - clip.y;
        auto rect = scissor(clip, sx, sy);
        if (!rect.extent.width || !rect.extent.height)
            return;
        VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &rect);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, blit_pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, blit_layout, 0, 1,
                                &display_sets[dc.scene_index], 0, nullptr);
        BlitPush push;
        push.screen = {width, height};
        push.rect = {scene.bounds.x, scene.bounds.y, scene.bounds.w, scene.bounds.h};
        vkCmdPushConstants(command, blit_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDraw(command, 6, 1, 0, 0);
    }
    bool render(const Ui &ui, float width, float height) {
        int framebuffer_width = 0, framebuffer_height = 0;
        glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
        if (framebuffer_width <= 0 || framebuffer_height <= 0 || width <= 0 || height <= 0)
            return false;
        if (!swapchain || extent.width != static_cast<uint32_t>(framebuffer_width) ||
            extent.height != static_cast<uint32_t>(framebuffer_height))
            create_swapchain();
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "Wait for prior frame");
        uint32_t index = 0;
        VkResult acquired =
            vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available, VK_NULL_HANDLE, &index);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            create_swapchain();
            return false;
        }
        if (acquired != VK_SUBOPTIMAL_KHR)
            check(acquired, "Acquire swapchain image");
        upload_draw_data(ui.draw());
        bool capture = !screenshot_path.empty();
        if (capture && !transfer_supported)
            throw std::runtime_error("This graphics surface does not support screenshot readback");
        if (capture && format != VK_FORMAT_B8G8R8A8_UNORM && format != VK_FORMAT_R8G8B8A8_UNORM &&
            format != VK_FORMAT_B8G8R8A8_SRGB && format != VK_FORMAT_R8G8B8A8_SRGB)
            throw std::runtime_error("Screenshot capture requires an RGBA8 swapchain");
        if (capture)
            ensure_buffer(readback, static_cast<VkDeviceSize>(extent.width) * extent.height * 4,
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false);
        const bool use_aa = scene_aa && scene_aa_samples != VK_SAMPLE_COUNT_1_BIT && scene_pass_msaa;
        const bool use_rt = ray_tracing && rt_available;
        const bool use_dlss = dlss;
        float sx = static_cast<float>(extent.width) / width, sy = static_cast<float>(extent.height) / height;
        const auto &draw = ui.draw();
        const uint32_t scene_count =
            static_cast<uint32_t>(std::min(draw.scenes.size(), static_cast<size_t>(kMaxScenes)));
        for (uint32_t i = 0; i < scene_count; ++i) {
            const auto &scene = draw.scenes[i];
            uint32_t out_w =
                std::max(1u, static_cast<uint32_t>(std::ceil(std::max(0.0f, scene.bounds.w) * sx)));
            uint32_t out_h =
                std::max(1u, static_cast<uint32_t>(std::ceil(std::max(0.0f, scene.bounds.h) * sy)));
            uint32_t in_w = use_dlss ? std::max(1u, static_cast<uint32_t>(std::ceil(out_w * kDlssScale))) : out_w;
            uint32_t in_h = use_dlss ? std::max(1u, static_cast<uint32_t>(std::ceil(out_h * kDlssScale))) : out_h;
            ensure_scene_target(i, in_w, in_h, out_w, out_h,
                                use_aa ? scene_aa_samples : VK_SAMPLE_COUNT_1_BIT, use_dlss);
        }
        begin_command();
        bool built_as = false;
        if (use_rt) {
            for (auto &gpu : scene_gpu)
                gpu.rt_ready = false;
            for (uint32_t i = 0; i < scene_count; ++i)
                built_as = build_scene_as(i, scene_ranges[i], draw.scenes[i].rotation) || built_as;
            if (built_as)
                as_barrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
        }
        for (uint32_t i = 0; i < scene_count; ++i)
            record_scene(draw, i, use_rt && scene_gpu[i].rt_ready, use_aa, use_dlss);
        std::array<VkClearValue, 1> clear{};
        auto bg = ui.theme().background;
        if (srgb_surface()) {
            auto linear = [](float c) {
                return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            };
            bg.r = linear(bg.r);
            bg.g = linear(bg.g);
            bg.b = linear(bg.b);
        }
        clear[0].color = {{bg.r, bg.g, bg.b, bg.a}};
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = render_pass;
        begin.framebuffer = framebuffers[index];
        begin.renderArea.extent = extent;
        begin.clearValueCount = 1;
        begin.pClearValues = clear.data();
        vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
        for (const auto &dc : draw.commands) {
            if (dc.kind == DrawCommand::Kind::scene) {
                blit_scene(draw, dc, width, height, sx, sy);
                continue;
            }
            if (!dc.count)
                continue;
            auto rect = scissor(dc.clip, sx, sy);
            if (!rect.extent.width || !rect.extent.height)
                continue;
            VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height),
                                0, 1};
            vkCmdSetViewport(command, 0, 1, &viewport);
            vkCmdSetScissor(command, 0, 1, &rect);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline);
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(command, 0, 1, &ui_buffer.handle, &offset);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_layout, 0, 1, &descriptor, 0,
                                    nullptr);
            std::array<float, 2> screen{width, height};
            vkCmdPushConstants(command, ui_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(screen),
                               screen.data());
            vkCmdDraw(command, dc.count, 1, dc.first, 0);
        }
        vkCmdEndRenderPass(command);
        if (capture) {
            transition(images[index], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyImageToBuffer(command, images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   readback.handle, 1, &copy);
            VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = readback.handle;
            barrier.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                                 nullptr, 1, &barrier, 0, nullptr);
            transition(images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                       VK_ACCESS_TRANSFER_READ_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        }
        check(vkEndCommandBuffer(command), "Finish frame commands");
        check(vkResetFences(device, 1, &fence), "Reset frame fence");
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &image_available;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &render_finished;
        check(vkQueueSubmit(graphics, 1, &submit, fence), "Submit frame");
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &render_finished;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain;
        pi.pImageIndices = &index;
        VkResult presented = vkQueuePresentKHR(present, &pi);
        check(vkQueueWaitIdle(present), "Wait for presentation");
        if (present != graphics)
            check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "Wait for graphics completion");
        if (capture)
            save_screenshot();
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR ||
            acquired == VK_SUBOPTIMAL_KHR)
            create_swapchain();
        else
            check(presented, "Present frame");
        return true;
    }
    void save_screenshot() {
        size_t bytes = static_cast<size_t>(extent.width) * extent.height * 4;
        void *mapped{};
        check(vkMapMemory(device, readback.memory, 0, bytes, 0, &mapped), "Read screenshot");
        std::vector<unsigned char> pixels(bytes);
        std::memcpy(pixels.data(), mapped, bytes);
        vkUnmapMemory(device, readback.memory);
        if (format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB)
            for (size_t i = 0; i < bytes; i += 4)
                std::swap(pixels[i], pixels[i + 2]);
        for (size_t i = 3; i < bytes; i += 4)
            pixels[i] = 255;
        if (!stbi_write_png(screenshot_path.c_str(), static_cast<int>(extent.width),
                            static_cast<int>(extent.height), 4, pixels.data(),
                            static_cast<int>(extent.width) * 4))
            throw std::runtime_error("Cannot save screenshot: " + screenshot_path);
        std::cout << "Screenshot saved: " << screenshot_path << '\n';
        screenshot_path.clear();
    }
};
Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;
void Renderer::initialize(GLFWwindow *window, const application_cfg &app, const Ui &ui) {
    impl_->initialize(window, app, ui);
}
bool Renderer::render(const Ui &ui, float width, float height) { return impl_->render(ui, width, height); }
void Renderer::screenshot(const std::string &path) { impl_->screenshot_path = path; }
const std::string &Renderer::device_name() const { return impl_->gpu_name; }
void Renderer::set_scene_aa(bool enabled) { impl_->scene_aa = enabled; }
bool Renderer::scene_aa() const { return impl_->scene_aa; }
void Renderer::set_ray_tracing(bool enabled) { impl_->ray_tracing = enabled; }
bool Renderer::ray_tracing() const { return impl_->ray_tracing; }
bool Renderer::ray_tracing_available() const { return impl_->rt_available; }
void Renderer::set_dlss(bool enabled) { impl_->dlss = enabled; }
bool Renderer::dlss() const { return impl_->dlss; }
bool Renderer::dlss_available() const { return true; }
} // namespace Fluid
