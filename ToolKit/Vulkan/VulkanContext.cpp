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
#include "VulkanBindings.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

namespace ToolKit
{

  static const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

  static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                               VkDebugUtilsMessageTypeFlagsEXT type,
                                                               const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                               void*)
  {
    const char* typeStr = "GENERAL";
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
    {
      typeStr = "VALIDATION";
    }
    else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
    {
      typeStr = "PERFORMANCE";
    }

    const char* idName = data->pMessageIdName ? data->pMessageIdName : "None";

    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
      TK_ERR("[VK-%s] %s: %s", typeStr, idName, data->pMessage);
    }
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
      TK_WRN("[VK-%s] %s: %s", typeStr, idName, data->pMessage);
    }
    else
    {
      TK_LOG("[VK-%s] %s: %s", typeStr, idName, data->pMessage);
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

  static bool InstanceExtensionAvailable(const char* name)
  {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    for (const auto& e : extensions)
    {
      if (strcmp(e.extensionName, name) == 0)
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
    if (!CreateGlobalDescriptorSetLayout())
    {
      return false;
    }
    if (!CreateFrameDescriptorPools())
    {
      return false;
    }
    if (!CreatePerDrawUboRing())
    {
      return false;
    }

    m_vkCmdBeginDebugUtilsLabelEXT =
        (PFN_vkCmdBeginDebugUtilsLabelEXT) vkGetInstanceProcAddr(m_instance, "vkCmdBeginDebugUtilsLabelEXT");
    m_vkCmdEndDebugUtilsLabelEXT =
        (PFN_vkCmdEndDebugUtilsLabelEXT) vkGetInstanceProcAddr(m_instance, "vkCmdEndDebugUtilsLabelEXT");

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

    // Drop any GPU work that was queued but never flushed (no frame ever started, or shutdown
    // landed mid-frame with a render pass open). The cb-side recorders are no-ops if not
    // replayed; the cleanup callbacks still need to run to release any staging resources
    // captured in their closures.
    for (PendingGpuWork& w : m_pendingGpuWork)
    {
      if (w.postFlushCleanup)
      {
        w.postFlushCleanup();
      }
    }
    m_pendingGpuWork.clear();

    for (PendingGpuWork& w : m_pendingDuringRpWork)
    {
      if (w.postFlushCleanup)
      {
        w.postFlushCleanup();
      }
    }
    m_pendingDuringRpWork.clear();

    if (m_descriptorPool != VK_NULL_HANDLE)
    {
      vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
      m_descriptorPool = VK_NULL_HANDLE;
    }

    for (VkDescriptorPool& p : m_perFrameDescriptorPools)
    {
      if (p != VK_NULL_HANDLE)
      {
        vkDestroyDescriptorPool(m_device, p, nullptr);
        p = VK_NULL_HANDLE;
      }
    }

    // Per-draw UBO ring — destroy before the allocator goes away (VulkanBuffer::Destroy uses VMA).
    if (m_perDrawUboRing.handle != VK_NULL_HANDLE)
    {
      VulkanBuffer::Destroy(this, m_perDrawUboRing);
    }

    if (m_globalDescriptorSetLayout != VK_NULL_HANDLE)
    {
      vkDestroyDescriptorSetLayout(m_device, m_globalDescriptorSetLayout, nullptr);
      m_globalDescriptorSetLayout = VK_NULL_HANDLE;
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
    bool validationFeaturesSupported   = false;
    if (m_validationEnabled)
    {
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      if (InstanceExtensionAvailable(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME))
      {
        extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        validationFeaturesSupported = true;
      }
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

    // pNext chain for validation features and early debug messenger
    VkValidationFeaturesEXT validationFeatures{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    VkValidationFeatureEnableEXT enabledFeatures[] = {
        VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};

    VkDebugUtilsMessengerCreateInfoEXT debugCi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};

    if (m_validationEnabled)
    {
      debugCi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      debugCi.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      debugCi.pfnUserCallback = DebugMessengerCallback;

      if (validationFeaturesSupported)
      {
        validationFeatures.enabledValidationFeatureCount = 2;
        validationFeatures.pEnabledValidationFeatures    = enabledFeatures;
        validationFeatures.pNext                         = &debugCi;
        ci.pNext                                         = &validationFeatures;
      }
      else
      {
        ci.pNext = &debugCi;
      }
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
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
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

  bool VulkanContext::CreateGlobalDescriptorSetLayout()
  {
    // Stage 7d-3 — kitchen-sink layout shared by every VulkanGpuProgram. Reserves every binding
    // the binding-map convention exposes (see VulkanBindings.h). Reasoning:
    //   - Combined image samplers at 0..7 cover the GL texture slot range as-is (no remap).
    //   - Fixed UBOs sit at the post-remap positions of the GL UBO slots ToolKit actually uses
    //     (camera/graphic constants/lights).
    //   - The per-draw dynamic UBO lives at @ref kPerDrawUboBinding so vkCmdBindDescriptorSets'
    //     dynamicOffset advances it without rewriting the descriptor.
    //   - All bindings flagged ALL_GRAPHICS so any shader stage can reference any binding without
    //     us tracking which is used where; unused bindings are ignored at descriptor write time.
    using namespace VulkanBindings;

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(kTextureBindingCount + 7);

    auto pushBinding = [&](uint binding, VkDescriptorType type)
    {
      VkDescriptorSetLayoutBinding b{};
      b.binding         = binding;
      b.descriptorType  = type;
      b.descriptorCount = 1;
      b.stageFlags      = VK_SHADER_STAGE_ALL_GRAPHICS;
      bindings.push_back(b);
    };

    for (uint i = 0; i < kTextureBindingCount; ++i)
    {
      pushBinding(kTextureBindingBase + i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    }
    // GL UBO slots ToolKit currently uses — see VulkanBindings.h header table. Slot 5 is the
    // shared "pass-specific" slot: each engine pass (OutlinePass/DilatePassData, future
    // SsaoCalc/Bloom/etc.) owns its own buffer instance and rebinds this slot at render time.
    for (uint glSlot : {3u, 4u, 5u, 7u, 8u, 9u, 10u})
    {
      pushBinding(UboBindingFor(glSlot), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    }
    // Per-draw dynamic UBO. Backing buffer is VulkanContext::m_perDrawUboRing; SubmitPerDrawData
    // bumps the dynamic offset each draw, so a single descriptor write covers every per-draw
    // payload during a frame.
    pushBinding(kPerDrawUboBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);

    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = (uint32_t) bindings.size();
    ci.pBindings    = bindings.data();

    VkResult r = vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_globalDescriptorSetLayout);
    if (r != VK_SUCCESS)
    {
      TK_ERR("vkCreateDescriptorSetLayout (global) failed: %d", r);
      return false;
    }
    return true;
  }

  bool VulkanContext::CreateFrameDescriptorPools()
  {
    // Stage 7d-4. One pool per frame-in-flight slot. Each pool reserves enough for ~256 sets
    // (current backend allocates one descriptor set per draw worst-case; a frame with 256 unique
    // BindPipeline sites is far above any expected sub-system load). Sized for the global
    // descriptor set layout's bindings: kTextureBindingCount sampled images + 7 UBOs (slots
    // 3,4,5,7,8,9,10) + 1 dynamic UBO (perDraw) per set, multiplied by max sets to give
    // descriptor count budgets.
    const uint32_t kMaxSetsPerFrame = 256;
    VkDescriptorPoolSize sizes[]    = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxSetsPerFrame * VulkanBindings::kTextureBindingCount},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         kMaxSetsPerFrame * 7},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kMaxSetsPerFrame * 1},
    };

    VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    // No FREE_DESCRIPTOR_SET_BIT — the only release path is vkResetDescriptorPool at frame begin.
    ci.flags         = 0;
    ci.maxSets       = kMaxSetsPerFrame;
    ci.poolSizeCount = (uint32_t) (sizeof(sizes) / sizeof(sizes[0]));
    ci.pPoolSizes    = sizes;

    for (size_t i = 0; i < m_perFrameDescriptorPools.size(); ++i)
    {
      VkResult r = vkCreateDescriptorPool(m_device, &ci, nullptr, &m_perFrameDescriptorPools[i]);
      if (r != VK_SUCCESS)
      {
        TK_ERR("vkCreateDescriptorPool (frame %zu) failed: %d", i, r);
        return false;
      }
    }
    return true;
  }

  VkDescriptorPool VulkanContext::GetFrameDescriptorPool(uint frameIndex) const
  {
    if (frameIndex >= m_perFrameDescriptorPools.size())
    {
      return VK_NULL_HANDLE;
    }
    return m_perFrameDescriptorPools[frameIndex];
  }

  void VulkanContext::ResetFrameDescriptorPool(uint frameIndex)
  {
    if (frameIndex >= m_perFrameDescriptorPools.size())
    {
      return;
    }
    if (m_perFrameDescriptorPools[frameIndex] != VK_NULL_HANDLE)
    {
      vkResetDescriptorPool(m_device, m_perFrameDescriptorPools[frameIndex], 0);
    }
  }

  VkDescriptorSet VulkanContext::AllocateFrameDescriptorSet(uint frameIndex, VkDescriptorSetLayout layout)
  {
    if (frameIndex >= m_perFrameDescriptorPools.size() || layout == VK_NULL_HANDLE)
    {
      return VK_NULL_HANDLE;
    }
    VkDescriptorPool pool = m_perFrameDescriptorPools[frameIndex];
    if (pool == VK_NULL_HANDLE)
    {
      return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool     = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (VkResult r = vkAllocateDescriptorSets(m_device, &ai, &set); r != VK_SUCCESS)
    {
      // Out-of-pool errors (FRAGMENTED_POOL / OUT_OF_POOL_MEMORY) here mean a frame allocated
      // more sets than kMaxSetsPerFrame. Increase the cap if it ever happens in real workloads.
      TK_ERR("VulkanContext::AllocateFrameDescriptorSet failed: %d (pool exhausted?)", r);
      return VK_NULL_HANDLE;
    }
    return set;
  }

  bool VulkanContext::CreatePerDrawUboRing()
  {
    // Stage 7d-4b. Single host-visible buffer reused for every per-draw uniform payload across
    // FRAMES_IN_FLIGHT. Sized 1 MiB \u2014 with sizeof(PerDrawUniforms) ~600 bytes that's room for
    // ~1700 draws per frame at the highest alignment (256 B). If a real workload pushes past
    // this we'll see the AllocatePerDrawSlot warning once and bump the cap.
    constexpr VkDeviceSize kRingBytes = 1u << 20;

    // Cache the alignment requirement so we don't query it on every Submit.
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    m_minUniformBufferAlignment = props.limits.minUniformBufferOffsetAlignment;
    if (m_minUniformBufferAlignment == 0)
    {
      m_minUniformBufferAlignment = 1; // Spec allows 0 meaning "no requirement"; treat as 1.
    }

    m_perDrawUboRing = VulkanBuffer::CreateHostVisibleMapped(this, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kRingBytes);
    if (m_perDrawUboRing.handle == VK_NULL_HANDLE)
    {
      TK_ERR("VulkanContext::CreatePerDrawUboRing: ring allocation failed");
      return false;
    }
    m_perDrawUboHead           = 0;
    m_perDrawUboOverflowLogged = false;
    return true;
  }

  bool VulkanContext::AllocatePerDrawSlot(VkDeviceSize size, VkDeviceSize& outOffset, void*& outMappedPtr)
  {
    outOffset    = 0;
    outMappedPtr = nullptr;
    if (size == 0 || m_perDrawUboRing.handle == VK_NULL_HANDLE || m_perDrawUboRing.mapped == nullptr)
    {
      return false;
    }

    // Round size up to the alignment so successive slots stay aligned and the next AllocateSlot
    // can land directly at head without a separate align step. Same effect as aligning head
    // before the bump.
    const VkDeviceSize aligned =
        (size + m_minUniformBufferAlignment - 1) & ~(m_minUniformBufferAlignment - 1);
    if (m_perDrawUboHead + aligned > m_perDrawUboRing.size)
    {
      if (!m_perDrawUboOverflowLogged)
      {
        TK_ERR("VulkanContext::AllocatePerDrawSlot: ring full (%llu/%llu B). Increase ring size.",
               (unsigned long long) m_perDrawUboHead,
               (unsigned long long) m_perDrawUboRing.size);
        m_perDrawUboOverflowLogged = true;
      }
      return false;
    }

    outOffset    = m_perDrawUboHead;
    outMappedPtr = static_cast<uint8_t*>(m_perDrawUboRing.mapped) + m_perDrawUboHead;
    m_perDrawUboHead += aligned;
    return true;
  }

  void VulkanContext::EnqueueGpuWork(std::function<void(VkCommandBuffer)> recorder,
                                     std::function<void()> postFlushCleanup)
  {
    if (!recorder)
    {
      return;
    }

    // Frame active path. Inline recording is only legal *outside* a render pass —
    // vkCmdPipelineBarrier and vkCmdCopy* in our render passes lack self-dependency, so
    // running them mid-pass trips VUID-vkCmdPipelineBarrier-None-07889. When a pass is open
    // we park the work and let VulkanBackend flush it the moment it closes the pass — same cb,
    // same submission, just on the safe side of vkCmdEndRenderPass.
    if (m_currentRecordingCb != VK_NULL_HANDLE)
    {
      const bool rpActive = m_renderPassActiveQuery && m_renderPassActiveQuery();
      if (!rpActive)
      {
        recorder(m_currentRecordingCb);
        if (postFlushCleanup && m_inlineCleanupSink)
        {
          m_inlineCleanupSink(std::move(postFlushCleanup));
        }
        return;
      }

      m_pendingDuringRpWork.push_back({std::move(recorder), std::move(postFlushCleanup)});
      return;
    }

    // No frame active: queue for the next BeginFrame to replay.
    m_pendingGpuWork.push_back({std::move(recorder), std::move(postFlushCleanup)});
  }

  std::vector<std::function<void()>> VulkanContext::FlushPendingGpuWork(VkCommandBuffer cb)
  {
    std::vector<std::function<void()>> cleanups;
    if (cb == VK_NULL_HANDLE || m_pendingGpuWork.empty())
    {
      return cleanups;
    }

    cleanups.reserve(m_pendingGpuWork.size());
    for (PendingGpuWork& w : m_pendingGpuWork)
    {
      w.recorder(cb);
      if (w.postFlushCleanup)
      {
        cleanups.emplace_back(std::move(w.postFlushCleanup));
      }
    }
    m_pendingGpuWork.clear();
    return cleanups;
  }

  std::vector<std::function<void()>> VulkanContext::FlushDuringRenderPassWork(VkCommandBuffer cb)
  {
    std::vector<std::function<void()>> cleanups;
    if (cb == VK_NULL_HANDLE || m_pendingDuringRpWork.empty())
    {
      return cleanups;
    }

    cleanups.reserve(m_pendingDuringRpWork.size());
    for (PendingGpuWork& w : m_pendingDuringRpWork)
    {
      w.recorder(cb);
      if (w.postFlushCleanup)
      {
        cleanups.emplace_back(std::move(w.postFlushCleanup));
      }
    }
    m_pendingDuringRpWork.clear();
    return cleanups;
  }

  void VulkanContext::SetCurrentRecordingCb(VkCommandBuffer cb,
                                            std::function<void(std::function<void()>)> inlineCleanupSink,
                                            std::function<bool()> renderPassActiveQuery)
  {
    m_currentRecordingCb     = cb;
    m_inlineCleanupSink      = std::move(inlineCleanupSink);
    m_renderPassActiveQuery  = std::move(renderPassActiveQuery);
  }

} // namespace ToolKit
