//
// Created by Frank Zha on 8/25/26.
//
#pragma once

#ifndef FLUID_DEVICE_MANAGER_H
#define FLUID_DEVICE_MANAGER_H

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

class device_manager {
    public:
    device_manager();
    void init(VkInstance* instance);

private:
    VkPhysicalDevice physical_device;

    bool is_device_suitable(VkPhysicalDevice device);
};

#endif //FLUID_DEVICE_MANAGER_H