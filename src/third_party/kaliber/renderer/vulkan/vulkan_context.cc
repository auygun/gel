#include "third_party/kaliber/renderer/vulkan/vulkan_context.h"

#include <string.h>
#include <limits>
#include <string>

#include "third_party/kaliber/base/log.h"
#include "third_party/vulkan/vk_enum_string_helper.h"

using namespace base;

namespace eng {

VulkanContext::VulkanContext() {
#if defined(_DEBUG)
  use_validation_layers_ = true;
#endif
}

VulkanContext::~VulkanContext() {
  DCHECK(device_ == VK_NULL_HANDLE);

  if (instance_ != VK_NULL_HANDLE) {
    if (use_validation_layers_)
      vkDestroyDebugUtilsMessengerEXT(instance_, dbg_messenger_, nullptr);
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

bool VulkanContext::Initialize() {
  if (instance_ != VK_NULL_HANDLE)
    return true;

  if (volkInitialize() != VK_SUCCESS)
    return false;

  if (!CreatePhysicalDevice())
    return false;

  return true;
}

void VulkanContext::Shutdown() {
  DCHECK(window_.swapchain == VK_NULL_HANDLE);
  DCHECK(window_.surface == VK_NULL_HANDLE);

  if (device_ != VK_NULL_HANDLE) {
    for (int i = 0; i < kFrameLag; i++) {
      vkDestroyFence(device_, fences_[i], nullptr);
      vkDestroySemaphore(device_, image_acquired_semaphores_[i], nullptr);
      vkDestroySemaphore(device_, draw_complete_semaphores_[i], nullptr);
      if (separate_present_queue_) {
        vkDestroySemaphore(device_, image_ownership_semaphores_[i], nullptr);
      }
    }

    if (allocator_ != VK_NULL_HANDLE) {
      vmaDestroyAllocator(allocator_);
      allocator_ = VK_NULL_HANDLE;
    }

    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
  queues_initialized_ = false;
  separate_present_queue_ = false;
  swapchain_image_count_ = 0;
  command_buffers_.clear();
  window_ = {};
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::DebugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data) {
  // This error needs to be ignored because the AMD allocator will mix up memory
  // types on IGP processors.
  if (strstr(callback_data->pMessage, "Mapping an image with layout") !=
          nullptr &&
      strstr(callback_data->pMessage,
             "can result in undefined behavior if this memory is used by the "
             "device") != nullptr) {
    return VK_FALSE;
  }
  // This needs to be ignored because Validator is wrong here.
  if (strstr(callback_data->pMessage,
             "SPIR-V module not valid: Pointer operand") != nullptr &&
      strstr(callback_data->pMessage, "must be a memory object") != nullptr) {
    return VK_FALSE;
  }
  // Workaround for Vulkan-Loader usability bug:
  // https://github.com/KhronosGroup/Vulkan-Loader/issues/262.
  if (strstr(callback_data->pMessage, "wrong ELF class: ELFCLASS32") !=
      nullptr) {
    return VK_FALSE;
  }
  if (callback_data->pMessageIdName &&
      strstr(callback_data->pMessageIdName,
             "UNASSIGNED-CoreValidation-DrawState-ClearCmdBeforeDraw") !=
          nullptr) {
    return VK_FALSE;
  }

  std::string type_string;
  switch (message_type) {
    case (VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT):
      type_string = "GENERAL";
      break;
    case (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT):
      type_string = "VALIDATION";
      break;
    case (VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT):
      type_string = "PERFORMANCE";
      break;
    case (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT &
          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT):
      type_string = "VALIDATION|PERFORMANCE";
      break;
    default:
      type_string = "UNKNOWN";
      break;
  }

  std::string objects_string;
  if (callback_data->objectCount > 0) {
    objects_string =
        "\n\tObjects - " + std::to_string(callback_data->objectCount);
    for (uint32_t object = 0; object < callback_data->objectCount; ++object) {
      objects_string +=
          "\n\t\tObject[" + std::to_string(object) + "]" + " - " +
          string_VkObjectType(callback_data->pObjects[object].objectType) +
          ", Handle " +
          std::to_string(callback_data->pObjects[object].objectHandle);
      if (nullptr != callback_data->pObjects[object].pObjectName &&
          strlen(callback_data->pObjects[object].pObjectName) > 0) {
        objects_string +=
            ", Name \"" +
            std::string(callback_data->pObjects[object].pObjectName) + "\"";
      }
    }
  }

  std::string labels_string;
  if (callback_data->cmdBufLabelCount > 0) {
    labels_string = "\n\tCommand Buffer Labels - " +
                    std::to_string(callback_data->cmdBufLabelCount);
    for (uint32_t cmd_buf_label = 0;
         cmd_buf_label < callback_data->cmdBufLabelCount; ++cmd_buf_label) {
      labels_string +=
          "\n\t\tLabel[" + std::to_string(cmd_buf_label) + "]" + " - " +
          callback_data->pCmdBufLabels[cmd_buf_label].pLabelName + "{ ";
      for (int color_idx = 0; color_idx < 4; ++color_idx) {
        labels_string += std::to_string(
            callback_data->pCmdBufLabels[cmd_buf_label].color[color_idx]);
        if (color_idx < 3) {
          labels_string += ", ";
        }
      }
      labels_string += " }";
    }
  }

  std::string error_message(
      type_string + " - Message Id Number: " +
      std::to_string(callback_data->messageIdNumber) +
      " | Message Id Name: " + callback_data->pMessageIdName + "\n\t" +
      callback_data->pMessage + objects_string + labels_string);

  switch (message_severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
      DLOG(0) << error_message;
      break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
      DLOG(0) << error_message;
      break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
      DLOG(0) << error_message;
      break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
      DLOG(0) << error_message;
      break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
      break;
  }

  return VK_FALSE;
}

VkBool32 VulkanContext::CheckLayers(
    uint32_t check_count,
    const char** check_names,
    const std::vector<VkLayerProperties>& instance_layers) {
  for (uint32_t i = 0; i < check_count; i++) {
    VkBool32 found = 0;
    for (uint32_t j = 0; j < instance_layers.size(); j++) {
      if (!strcmp(check_names[i], instance_layers[j].layerName)) {
        found = 1;
        break;
      }
    }
    if (!found) {
      DLOG(0) << "Can't find layer: " << check_names[i];
      return 0;
    }
  }
  return 1;
}

bool VulkanContext::CreateValidationLayers() {
  VkResult err;
  const char* instance_validation_layers_alt1[] = {
      "VK_LAYER_KHRONOS_validation"};
  const char* instance_validation_layers_alt2[] = {
      "VK_LAYER_LUNARG_standard_validation"};
  const char* instance_validation_layers_alt3[] = {
      "VK_LAYER_GOOGLE_threading", "VK_LAYER_LUNARG_parameter_validation",
      "VK_LAYER_LUNARG_object_tracker", "VK_LAYER_LUNARG_core_validation",
      "VK_LAYER_GOOGLE_unique_objects"};

  uint32_t instance_layer_count = 0;
  err = vkEnumerateInstanceLayerProperties(&instance_layer_count, nullptr);
  if (err) {
    DLOG(0) << "vkEnumerateInstanceLayerProperties failed. Error: "
            << string_VkResult(err);
    return false;
  }

  VkBool32 validation_found = 0;
  uint32_t validation_layer_count = 0;
  const char** instance_validation_layers = nullptr;
  if (instance_layer_count > 0) {
    std::vector<VkLayerProperties> instance_layers(instance_layer_count);
    err = vkEnumerateInstanceLayerProperties(&instance_layer_count,
                                             instance_layers.data());
    if (err) {
      DLOG(0) << "vkEnumerateInstanceLayerProperties failed. Error: "
              << string_VkResult(err);
      return false;
    }

    validation_layer_count =
        static_cast<uint32_t>(std::size(instance_validation_layers_alt1));
    instance_validation_layers = instance_validation_layers_alt1;
    validation_found = CheckLayers(validation_layer_count,
                                   instance_validation_layers, instance_layers);

    // use alternative (deprecated, removed in SDK 1.1.126.0) set of validation
    // layers.
    if (!validation_found) {
      validation_layer_count =
          static_cast<uint32_t>(std::size(instance_validation_layers_alt2));
      instance_validation_layers = instance_validation_layers_alt2;
      validation_found = CheckLayers(
          validation_layer_count, instance_validation_layers, instance_layers);
    }

    // use alternative (deprecated, removed in SDK 1.1.121.1) set of validation
    // layers.
    if (!validation_found) {
      validation_layer_count =
          static_cast<uint32_t>(std::size(instance_validation_layers_alt3));
      instance_validation_layers = instance_validation_layers_alt3;
      validation_found = CheckLayers(
          validation_layer_count, instance_validation_layers, instance_layers);
    }
  }

  if (validation_found) {
    enabled_layer_count_ = validation_layer_count;
    for (uint32_t i = 0; i < validation_layer_count; i++) {
      enabled_layers_[i] = instance_validation_layers[i];
    }
  } else {
    return false;
  }

  return true;
}

bool VulkanContext::InitializeExtensions() {
  VkResult err;
  uint32_t instance_extension_count = 0;

  enabled_extension_count_ = 0;
  enabled_layer_count_ = 0;
  memset(extension_names_, 0, sizeof(extension_names_));

  // Get the extensions required by the platform (e.g. GLFW).
  const char** required_extensions = nullptr;
  uint32_t required_extension_count = 0;
  GetRequiredInstanceExtensions(required_extensions, required_extension_count);

  if (!required_extensions || required_extension_count == 0) {
    DLOG(0) << "No required instance extensions provided by platform.";
    return false;
  }

  err = vkEnumerateInstanceExtensionProperties(
      nullptr, &instance_extension_count, nullptr);
  if (err) {
    DLOG(0) << "vkEnumerateInstanceExtensionProperties failed. Error: "
            << string_VkResult(err);
    return false;
  }

  if (instance_extension_count > 0) {
    std::vector<VkExtensionProperties> instance_extensions(
        instance_extension_count);
    err = vkEnumerateInstanceExtensionProperties(
        nullptr, &instance_extension_count, instance_extensions.data());
    if (err) {
      DLOG(0) << "vkEnumerateInstanceExtensionProperties failed. Error: "
              << string_VkResult(err);
      return false;
    }

    // Enable all required platform extensions.
    for (uint32_t r = 0; r < required_extension_count; r++) {
      bool found = false;
      for (uint32_t i = 0; i < instance_extension_count; i++) {
        if (!strcmp(required_extensions[r],
                    instance_extensions[i].extensionName)) {
          found = true;
          break;
        }
      }
      if (!found) {
        DLOG(0) << "Required extension not found: " << required_extensions[r];
        return false;
      }
      extension_names_[enabled_extension_count_++] = required_extensions[r];
      if (enabled_extension_count_ >= kMaxExtensions) {
        DLOG(0) << "Enabled extension count reaches kMaxExtensions";
        return false;
      }
    }

    // Enable debug extensions if validation layers are active.
    for (uint32_t i = 0; i < instance_extension_count; i++) {
      if (!strcmp(VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
                  instance_extensions[i].extensionName)) {
        if (use_validation_layers_) {
          extension_names_[enabled_extension_count_++] =
              VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
        }
      }
      if (!strcmp(VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
                  instance_extensions[i].extensionName)) {
        if (use_validation_layers_) {
          extension_names_[enabled_extension_count_++] =
              VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        }
      }
      if (enabled_extension_count_ >= kMaxExtensions) {
        DLOG(0) << "Enabled extension count reaches kMaxExtensions";
        return false;
      }
    }
  }

  return true;
}

bool VulkanContext::CreatePhysicalDevice() {
  if (use_validation_layers_) {
    CreateValidationLayers();
  }

  if (!InitializeExtensions())
    return false;

  // Set application version to the Vulkan version we're developing against
  // except when we're on Vulkan 1.0.
  uint32_t application_api_version =
      volkGetInstanceVersion() == VK_API_VERSION_1_0 ? VK_API_VERSION_1_0
                                                     : VK_API_VERSION_1_2;

  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "gel";
  app_info.applicationVersion = 0;
  app_info.pEngineName = "kaliber";
  app_info.engineVersion = 0;
  app_info.apiVersion = application_api_version;

  VkInstanceCreateInfo inst_create_info{};
  inst_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  inst_create_info.pNext = nullptr;
  inst_create_info.flags = 0;
  inst_create_info.pApplicationInfo = &app_info;
  inst_create_info.enabledLayerCount = enabled_layer_count_;
  inst_create_info.ppEnabledLayerNames = (const char* const*)enabled_layers_;
  inst_create_info.enabledExtensionCount = enabled_extension_count_;
  inst_create_info.ppEnabledExtensionNames =
      (const char* const*)extension_names_;

  // This is info for a temp callback to use during CreateInstance. After the
  // instance is created, we use the instance-based function to register the
  // final callback.
  VkDebugUtilsMessengerCreateInfoEXT dbg_messenger_create_info{};
  if (use_validation_layers_) {
    dbg_messenger_create_info.sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dbg_messenger_create_info.pNext = nullptr;
    dbg_messenger_create_info.flags = 0;
    dbg_messenger_create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dbg_messenger_create_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dbg_messenger_create_info.pfnUserCallback = DebugMessengerCallback;
    dbg_messenger_create_info.pUserData = this;
    inst_create_info.pNext = &dbg_messenger_create_info;
  }

  uint32_t gpu_count;

  if (instance_ == VK_NULL_HANDLE) {
    VkResult err = vkCreateInstance(&inst_create_info, nullptr, &instance_);
    if (err == VK_ERROR_INCOMPATIBLE_DRIVER) {
      DLOG(0)
          << "Cannot find a compatible Vulkan installable client driver (ICD).";
      return false;
    }
    if (err == VK_ERROR_EXTENSION_NOT_PRESENT) {
      DLOG(0)
          << "Cannot find a specified extension library. Make sure your layers "
             "path is set appropriately. ";
      return false;
    }
    if (err) {
      DLOG(0) << "vkCreateInstance failed. Error: " << string_VkResult(err);
      return false;
    }
  }

  volkLoadInstanceOnly(instance_);

  // Make initial call to query gpu_count.
  VkResult err = vkEnumeratePhysicalDevices(instance_, &gpu_count, nullptr);
  if (err) {
    DLOG(0) << "vkEnumeratePhysicalDevices failed. Error: "
            << string_VkResult(err);
    return false;
  }

  if (gpu_count == 0) {
    DLOG(0) << "vkEnumeratePhysicalDevices reported zero accessible devices.";
    return false;
  }

  std::vector<VkPhysicalDevice> physical_devices(gpu_count);
  err = vkEnumeratePhysicalDevices(instance_, &gpu_count,
                                   physical_devices.data());
  if (err) {
    DLOG(0) << "vkEnumeratePhysicalDevices failed. Error: "
            << string_VkResult(err);
    return false;
  }

  // Enumerate all device names and select the preferred one if set.
  device_names_.clear();
  device_names_.reserve(gpu_count);
  selected_device_index_ = 0;
  for (uint32_t i = 0; i < gpu_count; i++) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_devices[i], &props);
    device_names_.emplace_back(props.deviceName);
    if (!preferred_device_name_.empty() &&
        preferred_device_name_ == props.deviceName) {
      selected_device_index_ = static_cast<int>(i);
    }
  }
  gpu_ = physical_devices[selected_device_index_];

  // Look for device extensions.
  uint32_t device_extension_count = 0;
  VkBool32 swapchain_ext_found = 0;
  enabled_extension_count_ = 0;
  memset(extension_names_, 0, sizeof(extension_names_));

  err = vkEnumerateDeviceExtensionProperties(gpu_, nullptr,
                                             &device_extension_count, nullptr);
  if (err) {
    DLOG(0) << "vkEnumerateDeviceExtensionProperties failed. Error: "
            << string_VkResult(err);
    return false;
  }

  if (device_extension_count > 0) {
    std::vector<VkExtensionProperties> device_extensions(
        device_extension_count);
    err = vkEnumerateDeviceExtensionProperties(
        gpu_, nullptr, &device_extension_count, device_extensions.data());
    if (err) {
      DLOG(0) << "vkEnumerateDeviceExtensionProperties failed. Error: "
              << string_VkResult(err);
      return false;
    }

    for (uint32_t i = 0; i < device_extension_count; i++) {
      if (!strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                  device_extensions[i].extensionName)) {
        swapchain_ext_found = 1;
        extension_names_[enabled_extension_count_++] =
            VK_KHR_SWAPCHAIN_EXTENSION_NAME;
      }
      if (enabled_extension_count_ >= kMaxExtensions) {
        DLOG(0) << "Enabled extension count reaches kMaxExtensions";
        return false;
      }
    }

    // Enable VK_KHR_maintenance1 extension for old vulkan drivers.
    for (uint32_t i = 0; i < device_extension_count; i++) {
      if (!strcmp(VK_KHR_MAINTENANCE1_EXTENSION_NAME,
                  device_extensions[i].extensionName)) {
        extension_names_[enabled_extension_count_++] =
            VK_KHR_MAINTENANCE1_EXTENSION_NAME;
      }
      if (enabled_extension_count_ >= kMaxExtensions) {
        DLOG(0) << "Enabled extension count reaches kMaxExtensions";
        return false;
      }
    }
  }

  if (!swapchain_ext_found) {
    DLOG(0) << "vkEnumerateDeviceExtensionProperties failed to find "
               "the " VK_KHR_SWAPCHAIN_EXTENSION_NAME " extension.";
    return false;
  }

  if (use_validation_layers_) {
    err = vkCreateDebugUtilsMessengerEXT(instance_, &dbg_messenger_create_info,
                                         nullptr, &dbg_messenger_);
    switch (err) {
      case VK_SUCCESS:
        break;
      case VK_ERROR_OUT_OF_HOST_MEMORY:
        DLOG(0) << "vkCreateDebugUtilsMessengerEXT: out of host memory";
        return false;
      default:
        DLOG(0) << "vkCreateDebugUtilsMessengerEXT: unknown failure";
        return false;
        break;
    }
  }
  vkGetPhysicalDeviceProperties(gpu_, &gpu_props_);

  DLOG(0) << "Vulkan:";
  DLOG(0) << "  Name: " << gpu_props_.deviceName;
  DLOG(0) << "  Type: " << string_VkPhysicalDeviceType(gpu_props_.deviceType);
  DLOG(0) << "  Vendor ID: " << gpu_props_.vendorID;
  DLOG(0) << "  API version: " << VK_VERSION_MAJOR(gpu_props_.apiVersion) << "."
          << VK_VERSION_MINOR(gpu_props_.apiVersion) << "."
          << VK_VERSION_PATCH(gpu_props_.apiVersion);
  DLOG(0) << "  Driver version: " << VK_VERSION_MAJOR(gpu_props_.driverVersion)
          << "." << VK_VERSION_MINOR(gpu_props_.driverVersion) << "."
          << VK_VERSION_PATCH(gpu_props_.driverVersion);
  DLOG(0) << "  maxPushConstantsSize: "
          << gpu_props_.limits.maxPushConstantsSize;
  DLOG(0) << "  optimalBufferCopyOffsetAlignment: "
          << gpu_props_.limits.optimalBufferCopyOffsetAlignment;

  // Call with NULL data to get count,
  vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &queue_family_count_, nullptr);
  if (queue_family_count_ == 0) {
    DLOG(0) << "Failed to query queue family count.";
    return false;
  }

  queue_props_.resize(queue_family_count_);
  vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &queue_family_count_,
                                           queue_props_.data());

  // Query fine-grained feature support for this device.
  // If app has specific feature requirements it should check supported features
  // based on this query.
  vkGetPhysicalDeviceFeatures(gpu_, &physical_device_features_);

  return true;
}

bool VulkanContext::CreateDevice() {
  VkResult err;
  float queue_priorities[1] = {0.0};
  std::vector<VkDeviceQueueCreateInfo> queues;

  VkDeviceQueueCreateInfo queue{};
  queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue.queueFamilyIndex = graphics_queue_family_index_;
  queue.queueCount = 1;
  queue.pQueuePriorities = queue_priorities;
  queues.push_back(queue);

  if (separate_present_queue_) {
    queue.queueFamilyIndex = present_queue_family_index_;
    queues.push_back(queue);
  }

  // Excluded unused features.
  // - robustBufferAccess: can hamper performance on some hardware
  // - shaderStorageImageMultisample: unsupported by Intel Arc, prevents from
  //                                  using MSAA storage accidentally
  // - sparse* stuff: we don't use sparse features and enabling them cause extra
  //                  internal allocations inside the Vulkan driver we don't
  //                  need
  VkPhysicalDeviceFeatures requested_device_features{physical_device_features_};
  requested_device_features.robustBufferAccess = 0;
  requested_device_features.occlusionQueryPrecise = 0;
  requested_device_features.pipelineStatisticsQuery = 0;
  requested_device_features.shaderStorageImageMultisample = 0;
  requested_device_features.shaderResourceResidency = 0;
  requested_device_features.sparseBinding = 0;
  requested_device_features.sparseResidencyBuffer = 0;
  requested_device_features.sparseResidencyImage2D = 0;
  requested_device_features.sparseResidencyImage3D = 0;
  requested_device_features.sparseResidency2Samples = 0;
  requested_device_features.sparseResidency4Samples = 0;
  requested_device_features.sparseResidency8Samples = 0;
  requested_device_features.sparseResidency16Samples = 0;
  requested_device_features.sparseResidencyAliased = 0;
  requested_device_features.inheritedQueries = 0;

  VkDeviceCreateInfo sdevice{};
  sdevice.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  sdevice.pNext = nullptr;
  sdevice.flags = 0;
  sdevice.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
  sdevice.pQueueCreateInfos = queues.data();
  sdevice.enabledLayerCount = 0;
  sdevice.ppEnabledLayerNames = nullptr;
  sdevice.enabledExtensionCount = enabled_extension_count_;
  sdevice.ppEnabledExtensionNames = (const char* const*)extension_names_;
  sdevice.pEnabledFeatures = &requested_device_features;

  err = vkCreateDevice(gpu_, &sdevice, nullptr, &device_);
  if (err) {
    DLOG(0) << "vkCreateDevice failed. Error: " << string_VkResult(err);
    return false;
  }

  volkLoadDevice(device_);

  // Initialize allocator
  VmaAllocatorCreateInfo allocator_info{};
  allocator_info.physicalDevice = gpu_;
  allocator_info.device = device_;
  allocator_info.instance = instance_;
  allocator_info.vulkanApiVersion = gpu_props_.apiVersion;
  const bool use_1_3_features = gpu_props_.apiVersion >= VK_API_VERSION_1_3;
  if (use_1_3_features)
    allocator_info.flags |= VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;

  VmaVulkanFunctions vulkan_functions{};
  const VkResult import_result =
      vmaImportVulkanFunctionsFromVolk(&allocator_info, &vulkan_functions);
  if (import_result != VK_SUCCESS) {
    DLOG(0) << "vmaImportVulkanFunctionsFromVolk failed. Error: "
            << string_VkResult(import_result);
    return false;
  }
  allocator_info.pVulkanFunctions = &vulkan_functions;

  err = vmaCreateAllocator(&allocator_info, &allocator_);
  if (err) {
    DLOG(0) << "vmaCreateAllocator failed. Error: " << string_VkResult(err);
    return false;
  }

  return true;
}

bool VulkanContext::InitializeQueues(VkSurfaceKHR surface) {
  // Iterate over each queue to learn whether it supports presenting:
  std::vector<VkBool32> supports_present(queue_family_count_);
  for (uint32_t i = 0; i < queue_family_count_; i++) {
    vkGetPhysicalDeviceSurfaceSupportKHR(gpu_, i, surface,
                                         &supports_present[i]);
  }

  // Search for a graphics and a present queue in the array of queue families,
  // try to find one that supports both.
  uint32_t graphics_queue_family_index = std::numeric_limits<uint32_t>::max();
  uint32_t present_queue_family_index = std::numeric_limits<uint32_t>::max();
  for (uint32_t i = 0; i < queue_family_count_; i++) {
    if ((queue_props_[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      if (graphics_queue_family_index == std::numeric_limits<uint32_t>::max()) {
        graphics_queue_family_index = i;
      }

      if (supports_present[i] == VK_TRUE) {
        graphics_queue_family_index = i;
        present_queue_family_index = i;
        break;
      }
    }
  }

  if (present_queue_family_index == std::numeric_limits<uint32_t>::max()) {
    // If didn't find a queue that supports both graphics and present, then find
    // a separate present queue.
    for (uint32_t i = 0; i < queue_family_count_; ++i) {
      if (supports_present[i] == VK_TRUE) {
        present_queue_family_index = i;
        break;
      }
    }
  }

  // Generate error if could not find both a graphics and a present queue
  if (graphics_queue_family_index == std::numeric_limits<uint32_t>::max() ||
      present_queue_family_index == std::numeric_limits<uint32_t>::max()) {
    DLOG(0) << "Could not find both graphics and present queues.";
    return false;
  }

  graphics_queue_family_index_ = graphics_queue_family_index;
  present_queue_family_index_ = present_queue_family_index;
  separate_present_queue_ =
      (graphics_queue_family_index_ != present_queue_family_index_);
  DLOG(0) << "  separate_present_queue: " << separate_present_queue_;

  if (!CreateDevice())
    return false;

  vkGetDeviceQueue(device_, graphics_queue_family_index_, 0, &graphics_queue_);

  if (!separate_present_queue_) {
    present_queue_ = graphics_queue_;
  } else {
    vkGetDeviceQueue(device_, present_queue_family_index_, 0, &present_queue_);
  }

  // Get the list of VkFormat's that are supported.
  uint32_t format_count;
  VkResult err = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface,
                                                      &format_count, nullptr);
  if (err) {
    DLOG(0) << "vkGetPhysicalDeviceSurfaceFormatsKHR failed. Error: "
            << string_VkResult(err);
    return false;
  }
  std::vector<VkSurfaceFormatKHR> surf_formats(format_count);
  err = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface, &format_count,
                                             surf_formats.data());
  if (err) {
    DLOG(0) << "vkGetPhysicalDeviceSurfaceFormatsKHR failed. Error: "
            << string_VkResult(err);
    return false;
  }

  VkFormat desired_format = VK_FORMAT_B8G8R8A8_UNORM;

  // If the format list includes just one entry of VK_FORMAT_UNDEFINED, the
  // surface has no preferred format. Otherwise, at least one supported format
  // will be returned.
  if (format_count == 1 && surf_formats[0].format == VK_FORMAT_UNDEFINED) {
    format_ = desired_format;
    color_space_ = surf_formats[0].colorSpace;
  } else if (format_count < 1) {
    DLOG(0) << "Format count less than 1.";
    return false;
  } else {
    // Find the first format that we support.
    format_ = VK_FORMAT_UNDEFINED;
    const VkFormat allowed_formats[] = {VK_FORMAT_B8G8R8A8_UNORM,
                                        VK_FORMAT_R8G8B8A8_UNORM};
    for (uint32_t afi = 0; afi < std::size(allowed_formats); afi++) {
      for (uint32_t sfi = 0; sfi < format_count; sfi++) {
        if (surf_formats[sfi].format == allowed_formats[afi]) {
          format_ = surf_formats[sfi].format;
          color_space_ = surf_formats[sfi].colorSpace;
          goto end_of_find_format;
        }
      }
    }

  end_of_find_format:
    if (format_ == VK_FORMAT_UNDEFINED) {
      DLOG(0) << "No usable surface format found.";
      return false;
    }
  }

  DLOG(0) << "  color_space: " << string_VkColorSpaceKHR(color_space_);
  DLOG(0) << "  format_: " << string_VkFormat(format_);

  if (!CreateSemaphores())
    return false;

  queues_initialized_ = true;
  return true;
}

bool VulkanContext::CreateSemaphores() {
  VkResult err;

  // Create semaphores to synchronize acquiring presentable buffers before
  // rendering and waiting for drawing to be complete before presenting.
  VkSemaphoreCreateInfo semaphore_create_info{};
  semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  // Create fences that we can use to throttle if we get too far ahead of the
  // image presents.
  VkFenceCreateInfo fence_create_info{};
  fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (uint32_t i = 0; i < kFrameLag; i++) {
    err = vkCreateFence(device_, &fence_create_info, nullptr, &fences_[i]);
    if (err) {
      DLOG(0) << "vkCreateFence failed. Error: " << string_VkResult(err);
      return false;
    }
    err = vkCreateSemaphore(device_, &semaphore_create_info, nullptr,
                            &image_acquired_semaphores_[i]);
    if (err) {
      DLOG(0) << "vkCreateSemaphore failed. Error: " << string_VkResult(err);
      return false;
    }
    err = vkCreateSemaphore(device_, &semaphore_create_info, nullptr,
                            &draw_complete_semaphores_[i]);
    if (err) {
      DLOG(0) << "vkCreateSemaphore failed. Error: " << string_VkResult(err);
      return false;
    }
    if (separate_present_queue_) {
      err = vkCreateSemaphore(device_, &semaphore_create_info, nullptr,
                              &image_ownership_semaphores_[i]);
      if (err) {
        DLOG(0) << "vkCreateSemaphore failed. Error: " << string_VkResult(err);
        return false;
      }
    }
  }
  frame_index_ = 0;

  // Get Memory information and properties.
  vkGetPhysicalDeviceMemoryProperties(gpu_, &memory_properties_);

  return true;
}

void VulkanContext::ResizeSurface(int width, int height) {
  window_.width = width;
  window_.height = height;
  UpdateSwapChain(&window_);
}

void VulkanContext::DestroySurface() {
  CleanUpSwapChain(&window_);
  if (window_.surface) {
    vkDestroySurfaceKHR(instance_, window_.surface, nullptr);
    window_.surface = VK_NULL_HANDLE;
  }
}

VkFramebuffer VulkanContext::GetFramebuffer() {
  return window_.swapchain_image_resources[window_.current_buffer].frame_buffer;
}

VkImage VulkanContext::GetSwapchainImage() {
  return window_.swapchain_image_resources[window_.current_buffer].image;
}

void VulkanContext::DeinitSwapChainResources(Window* window) {
  vkDeviceWaitIdle(device_);

  vkDestroyRenderPass(device_, window->render_pass, nullptr);
  window->render_pass = VK_NULL_HANDLE;

  if (!window->swapchain_image_resources.empty()) {
    for (uint32_t i = 0; i < swapchain_image_count_; i++) {
      vkDestroyImageView(device_, window->swapchain_image_resources[i].view,
                         nullptr);
      vkDestroyFramebuffer(
          device_, window->swapchain_image_resources[i].frame_buffer, nullptr);
    }

    window->swapchain_image_resources.clear();
  }

  if (separate_present_queue_) {
    vkDestroyCommandPool(device_, window->present_cmd_pool, nullptr);
    window->present_cmd_pool = VK_NULL_HANDLE;
  }
}

bool VulkanContext::CleanUpSwapChain(Window* window) {
  if (!window->swapchain)
    return true;

  DeinitSwapChainResources(window);

  vkDestroySwapchainKHR(device_, window->swapchain, nullptr);
  window->swapchain = VK_NULL_HANDLE;

  return true;
}

bool VulkanContext::UpdateSwapChain(Window* window) {
  VkResult err;

  VkSwapchainKHR old_swapchain = window->swapchain;

  if (old_swapchain)
    DeinitSwapChainResources(window);

  // Check the surface capabilities and formats.
  VkSurfaceCapabilitiesKHR surface_capabilities{};
  err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, window->surface,
                                                  &surface_capabilities);
  if (err) {
    DLOG(0) << "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed. Error: "
            << string_VkResult(err);
    return false;
  }

  uint32_t present_mode_count = 0;
  err = vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, window->surface,
                                                  &present_mode_count, nullptr);
  if (err) {
    DLOG(0) << "vkGetPhysicalDeviceSurfacePresentModesKHR failed. Error: "
            << string_VkResult(err);
    return false;
  }

  std::vector<VkPresentModeKHR> present_modes(present_mode_count);
  err = vkGetPhysicalDeviceSurfacePresentModesKHR(
      gpu_, window->surface, &present_mode_count, present_modes.data());
  if (err) {
    DLOG(0) << "vkGetPhysicalDeviceSurfacePresentModesKHR failed. Error: "
            << string_VkResult(err);
    return false;
  }

  // width and height are either both 0xFFFFFFFF, or both not 0xFFFFFFFF.
  if (surface_capabilities.currentExtent.width == 0xFFFFFFFF) {
    // If the surface size is undefined, the size is set to the size of the
    // images requested, which must fit within the minimum and maximum values.
    window->swapchain_extent.width = window->width;
    window->swapchain_extent.height = window->height;

    if (window->swapchain_extent.width <
        surface_capabilities.minImageExtent.width) {
      window->swapchain_extent.width =
          surface_capabilities.minImageExtent.width;
    } else if (window->swapchain_extent.width >
               surface_capabilities.maxImageExtent.width) {
      window->swapchain_extent.width =
          surface_capabilities.maxImageExtent.width;
    }

    if (window->swapchain_extent.height <
        surface_capabilities.minImageExtent.height) {
      window->swapchain_extent.height =
          surface_capabilities.minImageExtent.height;
    } else if (window->swapchain_extent.height >
               surface_capabilities.maxImageExtent.height) {
      window->swapchain_extent.height =
          surface_capabilities.maxImageExtent.height;
    }
  } else {
    // If the surface size is defined, the swap chain size must match
    window->swapchain_extent = surface_capabilities.currentExtent;
    window->width = surface_capabilities.currentExtent.width;
    window->height = surface_capabilities.currentExtent.height;
  }

  if (window->width == 0 || window->height == 0) {
    // likely window minimized, no swapchain created
    if (old_swapchain) {
      vkDestroySwapchainKHR(device_, old_swapchain, nullptr);
      window->swapchain = VK_NULL_HANDLE;
    }
    return true;
  }

  // The application will render an image, then pass it to the presentation
  // engine via vkQueuePresentKHR. The presentation engine will display the
  // image for the next VSync cycle, and then it will make it available to the
  // application again. The only present modes which support VSync are:
  //
  // VK_PRESENT_MODE_FIFO_KHR: At each VSync signal, the image in front of the
  // queue displays on screen and is then released. The application will acquire
  // one of the available ones, draw to it and then hand it over to the
  // presentation engine, which will push it to the back of the queue. If
  // rendering is fast the queue can become full. The CPU and the GPU will idle
  // until an image is available again. This behavior works well on mobile
  // because it limits overheating and saves battery life.
  //
  // VK_PRESENT_MODE_MAILBOX_KHR: The application can acquire a new image
  // straight away, render to it, and hand it over to the presentation engine.
  // If an image is queued for presentation, it will be discarded. Being able to
  // keep submitting new frames lets the application ensure it has the latest
  // user input, so input latency can be lower versus FIFO. If the application
  // doesn't throttle CPU and GPU, one of them may be fully utilized, resulting
  // in higher power consumption.
  VkPresentModeKHR swapchain_present_mode = VK_PRESENT_MODE_FIFO_KHR;
  VkPresentModeKHR fallback_present_mode = VK_PRESENT_MODE_FIFO_KHR;
  if (swapchain_present_mode != fallback_present_mode) {
    for (size_t i = 0; i < present_modes.size(); ++i) {
      if (present_modes[i] == swapchain_present_mode) {
        // Supported.
        fallback_present_mode = swapchain_present_mode;
        break;
      }
    }
  }

  if (swapchain_present_mode != fallback_present_mode) {
    DLOG(0) << "Present mode " << swapchain_present_mode << " is not supported";
    swapchain_present_mode = fallback_present_mode;
  }

  // 2 for double buffering, 3 for triple buffering.
  // Double buffering works well if frames can be processed within the interval
  // between VSync signals, which is 16.6ms at a rate of 60 fps. The rendered
  // image is presented to the swapchain, and the previously presented one is
  // made available to the application again. If the GPU cannot process frames
  // fast enough, VSync will be missed and the application will have to wait for
  // another whole VSync cycle, which caps framerate at 30 fps. This may be ok,
  // but triple buffering can deliver higher framerate.
  uint32_t desired_num_of_swapchain_images = 2;
  if (desired_num_of_swapchain_images < surface_capabilities.minImageCount) {
    desired_num_of_swapchain_images = surface_capabilities.minImageCount;
  }
  // If maxImageCount is 0, we can ask for as many images as we want; otherwise
  // we're limited to maxImageCount.
  if ((surface_capabilities.maxImageCount > 0) &&
      (desired_num_of_swapchain_images > surface_capabilities.maxImageCount)) {
    // Application must settle for fewer images than desired.
    desired_num_of_swapchain_images = surface_capabilities.maxImageCount;
  }

  VkSurfaceTransformFlagBitsKHR pre_transform;
  if (surface_capabilities.supportedTransforms &
      VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
    pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  } else {
    pre_transform = surface_capabilities.currentTransform;
  }

  // Find a supported composite alpha mode. One of these is guaranteed to be
  // set.
  VkCompositeAlphaFlagBitsKHR composite_alpha =
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  VkCompositeAlphaFlagBitsKHR composite_alpha_flags[4] = {
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
  };
  for (uint32_t i = 0; i < std::size(composite_alpha_flags); i++) {
    if (surface_capabilities.supportedCompositeAlpha &
        composite_alpha_flags[i]) {
      composite_alpha = composite_alpha_flags[i];
      break;
    }
  }

  VkSwapchainCreateInfoKHR swapchain_create_info{};
  swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchain_create_info.pNext = nullptr;
  swapchain_create_info.flags = 0;
  swapchain_create_info.surface = window->surface;
  swapchain_create_info.minImageCount = desired_num_of_swapchain_images;
  swapchain_create_info.imageFormat = format_;
  swapchain_create_info.imageColorSpace = color_space_;
  swapchain_create_info.imageExtent = window->swapchain_extent;
  swapchain_create_info.imageArrayLayers = 1;
  swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchain_create_info.queueFamilyIndexCount = 0;
  swapchain_create_info.pQueueFamilyIndices = nullptr;
  swapchain_create_info.preTransform = pre_transform;
  swapchain_create_info.compositeAlpha = composite_alpha;
  swapchain_create_info.presentMode = swapchain_present_mode;
  swapchain_create_info.clipped = true;
  swapchain_create_info.oldSwapchain = old_swapchain;

  err = vkCreateSwapchainKHR(device_, &swapchain_create_info, nullptr,
                             &window->swapchain);
  if (err) {
    DLOG(0) << "vkCreateSwapchainKHR failed. Error: " << string_VkResult(err);
    return false;
  }

  // Destroy the retired old swapchain now that the new one is created.
  if (old_swapchain)
    vkDestroySwapchainKHR(device_, old_swapchain, nullptr);

  uint32_t image_count;
  err = vkGetSwapchainImagesKHR(device_, window->swapchain, &image_count,
                                nullptr);
  if (err) {
    DLOG(0) << "vkGetSwapchainImagesKHR failed. Error: "
            << string_VkResult(err);
    return false;
  }

  if (swapchain_image_count_ == 0) {
    // Assign for the first time.
    swapchain_image_count_ = image_count;
  } else if (swapchain_image_count_ != image_count) {
    DLOG(0) << "Swapchain image count mismatch";
    return false;
  }

  std::vector<VkImage> swapchain_images(swapchain_image_count_);
  err =
      vkGetSwapchainImagesKHR(device_, window->swapchain,
                              &swapchain_image_count_, swapchain_images.data());
  if (err) {
    DLOG(0) << "vkGetSwapchainImagesKHR failed. Error: "
            << string_VkResult(err);
    return false;
  }

  window->swapchain_image_resources.resize(swapchain_image_count_);

  for (uint32_t i = 0; i < swapchain_image_count_; i++) {
    window->swapchain_image_resources[i].image = swapchain_images[i];

    VkImageViewCreateInfo image_view_create_info{};
    image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_create_info.image = swapchain_images[i];
    image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_create_info.format = format_;
    image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_R;
    image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_G;
    image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_B;
    image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_A;
    image_view_create_info.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_create_info.subresourceRange.baseMipLevel = 0;
    image_view_create_info.subresourceRange.levelCount = 1;
    image_view_create_info.subresourceRange.baseArrayLayer = 0;
    image_view_create_info.subresourceRange.layerCount = 1;

    err = vkCreateImageView(device_, &image_view_create_info, nullptr,
                            &window->swapchain_image_resources[i].view);
    if (err) {
      DLOG(0) << "vkCreateImageView failed. Error: " << string_VkResult(err);
      return false;
    }
  }

  // Framebuffer

  {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = format_;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_reference{};
    color_reference.attachment = 0;
    color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_reference;

    VkRenderPassCreateInfo render_pass_create_info{};
    render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_create_info.attachmentCount = 1;
    render_pass_create_info.pAttachments = &color_attachment;
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;

    err = vkCreateRenderPass(device_, &render_pass_create_info, nullptr,
                             &window->render_pass);
    if (err) {
      DLOG(0) << "vkCreateRenderPass failed. Error: " << string_VkResult(err);
      return false;
    }

    for (uint32_t i = 0; i < swapchain_image_count_; i++) {
      VkFramebufferCreateInfo framebuffer_create_info{};
      framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      framebuffer_create_info.renderPass = window->render_pass;
      framebuffer_create_info.attachmentCount = 1;
      framebuffer_create_info.pAttachments =
          &window->swapchain_image_resources[i].view;
      framebuffer_create_info.width = (uint32_t)window->width;
      framebuffer_create_info.height = (uint32_t)window->height;
      framebuffer_create_info.layers = 1;

      err = vkCreateFramebuffer(
          device_, &framebuffer_create_info, nullptr,
          &window->swapchain_image_resources[i].frame_buffer);
      if (err) {
        DLOG(0) << "vkCreateFramebuffer failed. Error: "
                << string_VkResult(err);
        return false;
      }
    }
  }

  // Separate present queue

  if (separate_present_queue_) {
    VkCommandPoolCreateInfo cmd_pool_create_info{};
    cmd_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_create_info.queueFamilyIndex = present_queue_family_index_;
    err = vkCreateCommandPool(device_, &cmd_pool_create_info, nullptr,
                              &window->present_cmd_pool);
    if (err) {
      DLOG(0) << "vkCreateCommandPool failed. Error: " << string_VkResult(err);
      return false;
    }

    VkCommandBufferAllocateInfo cmd_buffer_create_info{};
    cmd_buffer_create_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_buffer_create_info.commandPool = window->present_cmd_pool;
    cmd_buffer_create_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_buffer_create_info.commandBufferCount = 1;
    for (uint32_t i = 0; i < swapchain_image_count_; i++) {
      err = vkAllocateCommandBuffers(
          device_, &cmd_buffer_create_info,
          &window->swapchain_image_resources[i].graphics_to_present_cmd);
      if (err) {
        DLOG(0) << "vkAllocateCommandBuffers failed. Error: "
                << string_VkResult(err);
        return false;
      }

      VkCommandBufferBeginInfo cmd_buffer_begin_info{};
      cmd_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      cmd_buffer_begin_info.flags =
          VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
      err = vkBeginCommandBuffer(
          window->swapchain_image_resources[i].graphics_to_present_cmd,
          &cmd_buffer_begin_info);
      if (err) {
        DLOG(0) << "vkBeginCommandBuffer failed. Error: "
                << string_VkResult(err);
        return false;
      }

      VkImageMemoryBarrier image_ownership_barrier{};
      image_ownership_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      image_ownership_barrier.pNext = nullptr;
      image_ownership_barrier.srcAccessMask = 0;
      image_ownership_barrier.dstAccessMask =
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      image_ownership_barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      image_ownership_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      image_ownership_barrier.srcQueueFamilyIndex =
          graphics_queue_family_index_;
      image_ownership_barrier.dstQueueFamilyIndex = present_queue_family_index_;
      image_ownership_barrier.image =
          window->swapchain_image_resources[i].image;
      image_ownership_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                                  1, 0, 1};

      vkCmdPipelineBarrier(
          window->swapchain_image_resources[i].graphics_to_present_cmd,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
          nullptr, 1, &image_ownership_barrier);
      err = vkEndCommandBuffer(
          window->swapchain_image_resources[i].graphics_to_present_cmd);
      if (err) {
        DLOG(0) << "vkEndCommandBuffer failed. Error: " << string_VkResult(err);
        return false;
      }
    }
  }

  // Reset current buffer.
  window->current_buffer = 0;

  return true;
}

void VulkanContext::AppendCommandBuffer(const VkCommandBuffer& command_buffer,
                                        bool front) {
  if (front)
    command_buffers_.insert(command_buffers_.begin(), command_buffer);
  else
    command_buffers_.push_back(command_buffer);
}

void VulkanContext::Flush(bool all) {
  // Ensure everything else pending is executed.
  vkDeviceWaitIdle(device_);

  // Flush the current frame.
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount =
      static_cast<uint32_t>(command_buffers_.size() - (all ? 0 : 1));
  submit_info.pCommandBuffers = command_buffers_.data();
  VkResult err =
      vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
  command_buffers_[0] = nullptr;
  if (err) {
    DLOG(0) << "vkQueueSubmit failed. Error: " << string_VkResult(err);
    return;
  }

  auto draw_command_buffer = command_buffers_.back();
  command_buffers_.clear();
  if (!all)
    command_buffers_.push_back(draw_command_buffer);

  vkDeviceWaitIdle(device_);
}

bool VulkanContext::PrepareBuffers() {
  if (!queues_initialized_)
    return true;

  VkResult err;

  // Ensure no more than kFrameLag renderings are outstanding.
  vkWaitForFences(device_, 1, &fences_[frame_index_], VK_TRUE,
                  std::numeric_limits<uint64_t>::max());

  DCHECK(window_.swapchain != VK_NULL_HANDLE);
  if (window_.swapchain == VK_NULL_HANDLE)
    return false;

  // Use a finite timeout to avoid blocking the main thread indefinitely when
  // the driver stalls (observed with NVIDIA after long idle periods).
  constexpr uint64_t kAcquireTimeoutNs = 100'000'000;  // 100 ms

  do {
    // Get the index of the next available swapchain image:
    err = vkAcquireNextImageKHR(device_, window_.swapchain, kAcquireTimeoutNs,
                                image_acquired_semaphores_[frame_index_],
                                VK_NULL_HANDLE, &window_.current_buffer);

    if (err == VK_ERROR_OUT_OF_DATE_KHR) {
      // swapchain is out of date (e.g. the window was resized) and must be
      // recreated:
      DLOG(0) << "Swapchain is out of date, recreating.";
      UpdateSwapChain(&window_);
    } else if (err == VK_SUBOPTIMAL_KHR) {
      // swapchain is not as optimal as it could be, but the platform's
      // presentation engine will still present the image correctly.
      DLOG(0) << "Swapchain is suboptimal, recreating.";
      UpdateSwapChain(&window_);
      break;
    } else if (err == VK_TIMEOUT || err == VK_NOT_READY) {
      return false;
    } else if (err != VK_SUCCESS) {
      DLOG(0) << "vkAcquireNextImageKHR failed. Error: "
              << string_VkResult(err);
      return false;
    }
  } while (err != VK_SUCCESS);

  // Reset the fence only after successfully acquiring an image. If acquire
  // fails (timeout), the fence stays signaled so the next attempt doesn't
  // deadlock.
  vkResetFences(device_, 1, &fences_[frame_index_]);

  return true;
}

bool VulkanContext::SwapBuffers() {
  if (!queues_initialized_)
    return true;

  VkResult err;

  // Wait for the image acquired semaphore to be signaled to ensure that the
  // image won't be rendered to until the presentation engine has fully released
  // ownership to the application, and it is okay to render to the image.
  VkPipelineStageFlags pipe_stage_flags;
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.pWaitDstStageMask = &pipe_stage_flags;
  pipe_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = &image_acquired_semaphores_[frame_index_];
  submit_info.commandBufferCount =
      static_cast<uint32_t>(command_buffers_.size());
  submit_info.pCommandBuffers = command_buffers_.data();
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = &draw_complete_semaphores_[frame_index_];
  err = vkQueueSubmit(graphics_queue_, 1, &submit_info, fences_[frame_index_]);
  if (err) {
    DLOG(0) << "vkQueueSubmit failed. Error: " << string_VkResult(err);
    return false;
  }

  command_buffers_.clear();

  if (separate_present_queue_) {
    // If we are using separate queues, change image ownership to the present
    // queue before presenting, waiting for the draw complete semaphore and
    // signalling the ownership released semaphore when finished.
    VkFence null_fence = VK_NULL_HANDLE;
    pipe_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &draw_complete_semaphores_[frame_index_];
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers =
        &window_.swapchain_image_resources[window_.current_buffer]
             .graphics_to_present_cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &image_ownership_semaphores_[frame_index_];

    err = vkQueueSubmit(present_queue_, 1, &submit_info, null_fence);
    if (err) {
      DLOG(0) << "vkQueueSubmit failed. Error: " << string_VkResult(err);
      return false;
    }
  }

  DCHECK(window_.swapchain != VK_NULL_HANDLE);

  // If we are using separate queues we have to wait for image ownership,
  // otherwise wait for draw complete.
  VkPresentInfoKHR present{};
  present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = (separate_present_queue_)
                                ? &image_ownership_semaphores_[frame_index_]
                                : &draw_complete_semaphores_[frame_index_];
  present.swapchainCount = 1;
  present.pSwapchains = &window_.swapchain;
  present.pImageIndices = &window_.current_buffer;

  err = vkQueuePresentKHR(present_queue_, &present);

  frame_index_ += 1;
  frame_index_ %= kFrameLag;

  if (err == VK_ERROR_OUT_OF_DATE_KHR) {
    // Swapchain is out of date (e.g. the window was resized) and must be
    // recreated.
    DLOG(0) << "Swapchain is out of date.";
  } else if (err == VK_SUBOPTIMAL_KHR) {
    // Swapchain is not as optimal as it could be, but the platform's
    // presentation engine will still present the image correctly.
    DLOG(0) << "Swapchain is Suboptimal.";
  } else if (err) {
    DLOG(0) << "vkQueuePresentKHR failed. Error: " << string_VkResult(err);
    return false;
  }

  return true;
}

}  // namespace eng
