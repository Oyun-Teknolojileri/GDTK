/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include "VulkanContext.h"

#include "../Logger.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

namespace ToolKit
{

  static const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

  static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                               VkDebugUtilsMessageTypeFlagsEXT,
                                                               const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                               void*)
  {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
      TK_ERR("[VK] %s", data->pMessage);
    }
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
      TK_WRN("[VK] %s", data->pMessage);
    }
    else
    {
      TK_LOG("[VK] %s", data->pMessage);
    }
    return VK_FALSE;
  }

  static bool ValidationLayerAvailable()
  {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& l : layers)
    {
      if (strcmp(l.layerName, kValidationLayerName) == 0)
      {
        return true;
      }
    }
    return false;
  }

  VulkanContext::VulkanContext() {}

  VulkanContext::~VulkanContext() { Destroy(); }

  bool VulkanContext::Init(const std::vector<const char*>& instanceExtensions,
                           const std::function<uint64 (void*)>& surfaceFactory)
  {
    if (instanceExtensions.empty() || !surfaceFactory)
    {
      TK_ERR("VulkanContext::Init missing instance extensions or surface factory");
      return false;
    }

#ifdef TK_DEBUG
    m_validationEnabled = ValidationLayerAvailable();
    if (!m_validationEnabled)
    {
      TK_WRN("VK_LAYER_KHRONOS_validation not available — continuing without validation");
    }
#endif

    if (!CreateInstance(instanceExtensions))
    {
      return false;
    }
    if (m_validationEnabled && !CreateDebugMessenger())
    {
      return false;
    }
    if (!CreateSurface(surfaceFactory))
    {
      return false;
    }
    if (!PickPhysicalDevice())
    {
      return false;
    }
    if (!CreateLogicalDevice())
    {
      return false;
    }
    if (!CreateAllocator())
    {
      return false;
    }
    if (!CreateDescriptorPool())
    {
      return false;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    TK_LOG("VulkanContext ready — GPU: %s (driver %u, api %u.%u)",
           props.deviceName,
           props.driverVersion,
           VK_VERSION_MAJOR(props.apiVersion),
           VK_VERSION_MINOR(props.apiVersion));
    return true;
  }

  void VulkanContext::Destroy()
  {
    if (m_device != VK_NULL_HANDLE)
    {
      vkDeviceWaitIdle(m_device);
    }

    if (m_descriptorPool != VK_NULL_HANDLE)
    {
      vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
      m_descriptorPool = VK_NULL_HANDLE;
    }

    if (m_allocator != nullptr)
    {
      vmaDestroyAllocator(m_allocator);
      m_allocator = nullptr;
    }

    if (m_device != VK_NULL_HANDLE)
    {
      vkDestroyDevice(m_device, nullptr);
      m_device = VK_NULL_HANDLE;
    }

    if (m_surface != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE)
    {
      vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
      m_surface = VK_NULL_HANDLE;
    }

    if (m_debugMessenger != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE)
    {
      auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
          vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
      if (fn != nullptr)
      {
        fn(m_instance, m_debugMessenger, nullptr);
      }
      m_debugMessenger = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE)
    {
      vkDestroyInstance(m_instance, nullptr);
      m_instance = VK_NULL_HANDLE;
    }
  }

  bool VulkanContext::CreateInstance(const std::vector<const char*>& requiredExtensions)
  {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName   = "ToolKit";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName        = "ToolKit";
    app.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion         = VK_API_VERSION_1_3;

    std::vector<const char*> extensions = requiredExtensions;
    if (m_validationEnabled)
    {
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = (uint32_t) extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();

    const char* layers[] = {kValidationLayerName};
    if (m_validationEnabled)
    {
      ci.enabledLayerCount   = 1;
      ci.ppEnabledLayerNames = layers;
    }

    VkResult r = vkCreateInstance(&ci, nullptr, &m_instance);
    if (r != VK_SUCCESS)
    {
      TK_ERR("vkCreateInstance failed: %d", r);
      return false;
    }
    return true;
  }

  bool VulkanContext::CreateDebugMessenger()
  {
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
    if (fn == nullptr)
    {
      TK_WRN("vkCreateDebugUtilsMessengerEXT not resolved — no messenger");
      return true;
    }

    VkDebugUtilsMessengerCreateInfoEXT ci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = DebugMessengerCallback;

    VkResult r = fn(m_instance, &ci, nullptr, &m_debugMessenger);
    if (r != VK_SUCCESS)
    {
      TK_ERR("vkCreateDebugUtilsMessengerEXT failed: %d", r);
      return false;
    }
    return true;
  }

  bool VulkanContext::CreateSurface(const std::function<uint64 (void*)>& factory)
  {
    const uint64 handle = factory((void*) m_instance);
    if (handle == 0)
    {
      TK_ERR("VulkanContext: surface factory returned null");
      return false;
    }
    m_surface = (VkSurfaceKHR) handle;
    return true;
  }

  static bool FindQueueFamilies(VkPhysicalDevice phys, VkSurfaceKHR surface, uint& graphics, uint& present)
  {
    graphics = (uint) -1;
    present  = (uint) -1;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, families.data());

    for (uint32_t i = 0; i < count; ++i)
    {
      if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
      {
        if (graphics == (uint) -1)
        {
          graphics = i;
        }
      }
      VkBool32 presentSupport = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &presentSupport);
      if (presentSupport && present == (uint) -1)
      {
        present = i;
      }
      if (graphics != (uint) -1 && present != (uint) -1)
      {
        break;
      }
    }
    return graphics != (uint) -1 && present != (uint) -1;
  }

  static bool DeviceSupportsSwapchain(VkPhysicalDevice phys)
  {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &count, exts.data());
    for (const auto& e : exts)
    {
      if (strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
      {
        return true;
      }
    }
    return false;
  }

  bool VulkanContext::PickPhysicalDevice()
  {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0)
    {
      TK_ERR("No Vulkan-capable physical devices found");
      return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    // Prefer discrete GPU that supports everything we need.
    VkPhysicalDevice discrete = VK_NULL_HANDLE;
    VkPhysicalDevice fallback = VK_NULL_HANDLE;
    uint g, p;

    for (VkPhysicalDevice dev : devices)
    {
      if (!DeviceSupportsSwapchain(dev))
      {
        continue;
      }
      if (!FindQueueFamilies(dev, m_surface, g, p))
      {
        continue;
      }
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(dev, &props);
      if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && discrete == VK_NULL_HANDLE)
      {
        discrete = dev;
      }
      else if (fallback == VK_NULL_HANDLE)
      {
        fallback = dev;
      }
    }

    m_physicalDevice = (discrete != VK_NULL_HANDLE) ? discrete : fallback;
    if (m_physicalDevice == VK_NULL_HANDLE)
    {
      TK_ERR("No suitable Vulkan device (need swapchain + graphics + present queue)");
      return false;
    }

    FindQueueFamilies(m_physicalDevice, m_surface, m_graphicsQueueFamily, m_presentQueueFamily);
    return true;
  }

  bool VulkanContext::CreateLogicalDevice()
  {
    std::set<uint> uniqueFamilies = {m_graphicsQueueFamily, m_presentQueueFamily};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    float priority = 1.0f;
    for (uint family : uniqueFamilies)
    {
      VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
      qi.queueFamilyIndex = family;
      qi.queueCount       = 1;
      qi.pQueuePriorities = &priority;
      queueInfos.push_back(qi);
    }

    const char* deviceExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.queueCreateInfoCount    = (uint32_t) queueInfos.size();
    ci.pQueueCreateInfos       = queueInfos.data();
    ci.enabledExtensionCount   = 1;
    ci.ppEnabledExtensionNames = deviceExts;
    ci.pEnabledFeatures        = &features;

    VkResult r = vkCreateDevice(m_physicalDevice, &ci, nullptr, &m_device);
    if (r != VK_SUCCESS)
    {
      TK_ERR("vkCreateDevice failed: %d", r);
      return false;
    }

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentQueueFamily, 0, &m_presentQueue);
    return true;
  }

  bool VulkanContext::CreateAllocator()
  {
    VmaAllocatorCreateInfo ci{};
    ci.physicalDevice   = m_physicalDevice;
    ci.device           = m_device;
    ci.instance         = m_instance;
    ci.vulkanApiVersion = VK_API_VERSION_1_3;

    VkResult r = vmaCreateAllocator(&ci, &m_allocator);
    if (r != VK_SUCCESS)
    {
      TK_ERR("vmaCreateAllocator failed: %d", r);
      return false;
    }
    return true;
  }

  bool VulkanContext::CreateDescriptorPool()
  {
    // Oversized pool shared across systems (ImGui etc). Sizes chosen to cover typical editor needs.
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 256},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 256},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 64},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 64},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 64},
    };

    VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ci.maxSets       = 2048;
    ci.poolSizeCount = (uint32_t) (sizeof(sizes) / sizeof(sizes[0]));
    ci.pPoolSizes    = sizes;

    VkResult r = vkCreateDescriptorPool(m_device, &ci, nullptr, &m_descriptorPool);
    if (r != VK_SUCCESS)
    {
      TK_ERR("vkCreateDescriptorPool failed: %d", r);
      return false;
    }
    return true;
  }

} // namespace ToolKit
