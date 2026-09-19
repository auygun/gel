// volk.h must be included before VMA so that VOLK_HEADER_VERSION is
// defined. VMA 3.3.0 uses this to enable its volk integration API
// (vmaImportVulkanFunctionsFromVolk). volk.h also provides the Vulkan
// type definitions and extern PFN_vk... function pointer variables.
#include "third_party/volk/volk/volk.h"
#define VMA_IMPLEMENTATION
#include "vma/include/vk_mem_alloc.h"
