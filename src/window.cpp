//
// Created by zagym on 03/08/2026.
//
#include "window.h"
#include <vector>

Fluid::window::window(uint32_t width, uint32_t height, std::string title, bool resizable, bool fullscreen) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    app_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
}

Fluid::window::~window() {
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(app_window);
    glfwTerminate();
}

void Fluid::window::Init(std::string app_name) {
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = app_name.c_str();
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Fluid";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;

    uint32_t extension_count = 0;
    const char** glfw_extensions;
    glfw_extensions = glfwGetRequiredInstanceExtensions(&extension_count);
    instance_info.enabledExtensionCount = extension_count;
    instance_info.ppEnabledExtensionNames = glfw_extensions;
    instance_info.enabledLayerCount = 0;

    //todo: retrieve supported extension
    std::vector<const char*> supported_extensions;

    VkResult result = vkCreateInstance(&instance_info,nullptr, &instance);
    if (result != VK_SUCCESS && result != VK_ERROR_INCOMPATIBLE_DRIVER) {
        throw std::runtime_error("failed to create vulkan instance");
        return;
    }

    //todo: deal with incompatible driver issue
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
