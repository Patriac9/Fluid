//
// Created by Frank Zha on 8/25/26.
//
#include "device_manager.h"

#include <stdexcept>
#include <exception>
#include <vector>

device_manager::device_manager() {
    physical_device = VK_NULL_HANDLE;
}

void device_manager::init(VkInstance* instance) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(*instance, &device_count, nullptr);
    if (device_count == 0) {
        throw std::runtime_error("failed to find a suitable GPU device");
        return;
    }
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(*instance, &device_count, devices.data());

    for (auto& device : devices) {
        if (is_device_suitable(device)) {
            physical_device = device;
            break;
        }
    }
    if (physical_device == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU device");
    }
}

bool device_manager::is_device_suitable(VkPhysicalDevice device) {
    return true;
}
