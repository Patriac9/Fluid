//
// Created by zagym on 03/08/2026.
//
#include "window.h"
#include <vector>

Fluid::window::window(window_cfg cfg) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    app_window = glfwCreateWindow(cfg.width, cfg.height, cfg.window_title.c_str(), nullptr, nullptr);
}

Fluid::window::~window() {
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(app_window);
    glfwTerminate();
}

void Fluid::window::Init(const application_cfg& app_cfg) {
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = app_cfg.app_name.c_str();
    app_info.applicationVersion = VK_MAKE_VERSION(app_cfg.app_version[0],
        app_cfg.app_version[1], app_cfg.app_version[2]);
    app_info.pEngineName = app_cfg.engine_name.c_str();
    app_info.engineVersion = VK_MAKE_VERSION(app_cfg.engine_version[0],
        app_cfg.engine_version[1], app_cfg.engine_version[2]);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;

    /*
    auto extensions = get_required_extensions();
    instance_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instance_info.ppEnabledExtensionNames = extensions.data();
    */
    uint32_t extension_count = 0;
    const char** glfw_extensions;
    glfw_extensions = glfwGetRequiredInstanceExtensions(&extension_count);
    instance_info.enabledExtensionCount = extension_count;
    instance_info.ppEnabledExtensionNames = glfw_extensions;

    if (enable_validation_layers) {
        instance_info.enabledLayerCount = validation_layers.size();
        instance_info.ppEnabledLayerNames = validation_layers.data();
    }
    else instance_info.enabledLayerCount = 0;

    if (enable_validation_layers && !check_validation_layers_support()) {
        std:throw std::runtime_error("validation layers are not available!");
    }

    //todo: retrieve supported extension by using vkEnumerateInstanceExtensionProperties
    std::vector<const char*> supported_extensions;


    VkResult result = vkCreateInstance(&instance_info,nullptr, &instance);
    if (result != VK_SUCCESS && result != VK_ERROR_INCOMPATIBLE_DRIVER) {
        throw std::runtime_error("failed to create vulkan instance");
        return;
    }

    //todo: deal with incompatible driver issue: macOS
    if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
        std::vector<const char*> required_extensions;
        for (uint32_t i = 0; i < extension_count; i++) {
            required_extensions.emplace_back(glfw_extensions[i]);
        }
        required_extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instance_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        instance_info.enabledExtensionCount = uint32_t(required_extensions.size());
        instance_info.ppEnabledExtensionNames = required_extensions.data();
        if (vkCreateInstance(&instance_info, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("failed to create instance!");
        }
    }

}

void Fluid::window::tick() {

}

void Fluid::window::loop() {
    while (!glfwWindowShouldClose(app_window)) {
        glfwPollEvents();
        tick();
    }
}

bool Fluid::window::check_validation_layers_support() {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (const char* layer_name : validation_layers) {
        bool layer_found = false;
        for (auto& layer : available_layers) {
            if (strcmp(layer_name, layer.layerName) == 0) {
                layer_found = true;
                break;
            }
        }
        if (!layer_found) {
            return false;
        }
    }
    return true;
}

std::vector<const char *> Fluid::window::get_required_extensions() {
    uint32_t extension_count = 0;
    const char** glfw_extensions;
    glfw_extensions = glfwGetRequiredInstanceExtensions(&extension_count);

    std::vector<const char*> extensions(glfw_extensions, glfw_extensions + extension_count);

    if (enable_validation_layers) {
        extensions.push_back("VK_EXT_DEBUG_UTILS_EXTENSION_NAME");
    }
    return extensions;
}

void Fluid::window::destroy() {
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(app_window);
    glfwTerminate();
}
