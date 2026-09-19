#include "third_party/kaliber/renderer/vulkan/renderer_vulkan.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <utility>

#include "third_party/kaliber/base/hash.h"
#include "third_party/kaliber/base/log.h"
#include "third_party/kaliber/base/vecmath.h"
#include "third_party/kaliber/renderer/shader_source.h"
#include "third_party/glslang/glslang/SPIRV/GlslangToSpv.h"
#include "third_party/glslang/glslang/glslang/Include/ResourceLimits.h"
#include "third_party/glslang/glslang/glslang/Include/Types.h"
#include "third_party/glslang/glslang/glslang/Public/ResourceLimits.h"
#include "third_party/glslang/glslang/glslang/Public/ShaderLang.h"
#include "third_party/spirv-reflect/spirv-reflect/spirv_reflect.h"
#include "third_party/vulkan/vk_enum_string_helper.h"

using namespace base;

namespace eng {

namespace {

using VertexInputDescription =
    std::pair<std::vector<VkVertexInputBindingDescription>,
              std::vector<VkVertexInputAttributeDescription>>;

constexpr VkPrimitiveTopology kVkPrimitiveType[kPrimitive_Max] = {
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
};

constexpr VkFormat kVkDataType[kDataType_Max][4] = {
    {
        VK_FORMAT_R8_UNORM,
        VK_FORMAT_R8G8_UNORM,
        VK_FORMAT_R8G8B8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
    },
    {
        VK_FORMAT_R32_SFLOAT,
        VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_R32G32B32_SFLOAT,
        VK_FORMAT_R32G32B32A32_SFLOAT,
    },
    {
        VK_FORMAT_R32_SINT,
        VK_FORMAT_R32G32_SINT,
        VK_FORMAT_R32G32B32_SINT,
        VK_FORMAT_R32G32B32A32_SINT,
    },
    {
        VK_FORMAT_R16_SINT,
        VK_FORMAT_R16G16_SINT,
        VK_FORMAT_R16G16B16_SINT,
        VK_FORMAT_R16G16B16A16_SINT,
    },
    {
        VK_FORMAT_R32_UINT,
        VK_FORMAT_R32G32_UINT,
        VK_FORMAT_R32G32B32_UINT,
        VK_FORMAT_R32G32B32A32_UINT,
    },
    {
        VK_FORMAT_R16_UINT,
        VK_FORMAT_R16G16_UINT,
        VK_FORMAT_R16G16B16_UINT,
        VK_FORMAT_R16G16B16A16_UINT,
    },
};

constexpr size_t kMaxDescriptorsPerPool = 64;

std::vector<uint8_t> CompileGlsl(EShLanguage stage,
                                 const char* source_code,
                                 std::string* error) {
  const int kClientInputSemanticsVersion = 100;  // maps to #define VULKAN 100
  const int kDefaultVersion = 450;

  std::vector<uint8_t> ret;

  glslang::EShTargetClientVersion vulkan_client_version =
      glslang::EShTargetVulkan_1_0;
  glslang::EShTargetLanguageVersion target_version = glslang::EShTargetSpv_1_0;
  glslang::TShader::ForbidIncluder includer;

  glslang::TShader shader(stage);
  const char* cs_strings = source_code;

  shader.setStrings(&cs_strings, 1);
  shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan,
                     kClientInputSemanticsVersion);
  shader.setEnvClient(glslang::EShClientVulkan, vulkan_client_version);
  shader.setEnvTarget(glslang::EShTargetSpv, target_version);

  EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
  std::string pre_processed_code;

  // Preprocess
  if (!shader.preprocess(GetDefaultResources(), kDefaultVersion, ENoProfile,
                         false, false, messages, &pre_processed_code,
                         includer)) {
    if (error) {
      (*error) = "Failed pre-process:\n";
      (*error) += shader.getInfoLog();
      (*error) += "\n";
      (*error) += shader.getInfoDebugLog();
    }

    return ret;
  }
  cs_strings = pre_processed_code.c_str();
  shader.setStrings(&cs_strings, 1);

  // Parse
  if (!shader.parse(GetDefaultResources(), kDefaultVersion, false, messages)) {
    if (error) {
      (*error) = "Failed parse:\n";
      (*error) += shader.getInfoLog();
      (*error) += "\n";
      (*error) += shader.getInfoDebugLog();
    }
    return ret;
  }

  // link
  glslang::TProgram program;
  program.addShader(&shader);

  if (!program.link(messages)) {
    if (error) {
      (*error) = "Failed link:\n";
      (*error) += program.getInfoLog();
      (*error) += "\n";
      (*error) += program.getInfoDebugLog();
    }

    return ret;
  }

  std::vector<uint32_t> spirv;
  spv::SpvBuildLogger logger;
  glslang::SpvOptions spv_options;
  glslang::GlslangToSpv(*program.getIntermediate(stage), spirv, &logger,
                        &spv_options);

  ret.resize(spirv.size() * sizeof(uint32_t));
  {
    uint8_t* w = ret.data();
    memcpy(w, &spirv[0], spirv.size() * sizeof(uint32_t));
  }

  return ret;
}

VertexInputDescription GetVertexInputDescription(const VertexDescription& vd) {
  unsigned vertex_offset = 0;
  unsigned location = 0;

  std::vector<VkVertexInputAttributeDescription> attributes;

  for (auto& attr : vd) {
    auto [attrib_type, data_type, num_elements, type_size] = attr;

    VkVertexInputAttributeDescription attribute{};
    attribute.location = location++;
    attribute.binding = 0;
    attribute.format = kVkDataType[data_type][num_elements - 1];
    attribute.offset = vertex_offset;
    attributes.push_back(attribute);

    vertex_offset += static_cast<unsigned>(num_elements * type_size);
  }

  std::vector<VkVertexInputBindingDescription> bindings(1);
  bindings[0].binding = 0;
  bindings[0].stride = vertex_offset;
  bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  return std::make_pair(std::move(bindings), std::move(attributes));
}

VkIndexType GetIndexType(DataType data_type) {
  switch (data_type) {
    case kDataType_Invalid:
      return VK_INDEX_TYPE_NONE_KHR;
    case kDataType_UInt:
      return VK_INDEX_TYPE_UINT32;
    case kDataType_UShort:
      return VK_INDEX_TYPE_UINT16;
    default:
      break;
  }
  NOTREACHED() << "Invalid index type: " << data_type;
  return VK_INDEX_TYPE_NONE_KHR;
}

VkFormat GetImageFormat(ImageFormat format) {
  switch (format) {
    case ImageFormat::kRGBA32:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case ImageFormat::kDXT1:
      return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    case ImageFormat::kDXT5:
      return VK_FORMAT_BC3_UNORM_BLOCK;
    case ImageFormat::kETC1:
    case ImageFormat::kATC:
    case ImageFormat::kATCIA:
    default:
      break;
  }
  NOTREACHED() << "Invalid format: " << ImageFormatToString(format);
  return VK_FORMAT_R8G8B8A8_UNORM;
}

std::pair<int, int> GetBlockSizeForImageFormat(VkFormat format) {
  switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
      return {4, 1};
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
      return {8, 4};
    case VK_FORMAT_BC3_UNORM_BLOCK:
      return {16, 4};
    default:
      break;
  }
  NOTREACHED() << "Invalid format: " << string_VkFormat(format);
  return {0, 0};
}

std::pair<int, int> GetNumBlocksForImageFormat(VkFormat format,
                                               int width,
                                               int height) {
  switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
      return {width, height};
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
      return {(width + 3) / 4, (height + 3) / 4};
    default:
      break;
  }
  NOTREACHED() << "Invalid format: " << string_VkFormat(format);
  return {width, height};
}

}  // namespace

RendererVulkan::RendererVulkan() = default;

RendererVulkan::~RendererVulkan() {
  Shutdown();
}

void RendererVulkan::OnFramebufferResized(int width, int height) {
  context_.ResizeSurface(width, height);
}

int RendererVulkan::GetFramebufferWidth() const {
  return context_.GetFramebufferWidth();
}

int RendererVulkan::GetFramebufferHeight() const {
  return context_.GetFramebufferHeight();
}

void RendererVulkan::SetViewport(int x, int y, int width, int height) {
  VkViewport viewport{};
  viewport.x = 0;
  viewport.y = (float)height;
  viewport.width = (float)width;
  viewport.height = -(float)height;
  viewport.minDepth = 0;
  viewport.maxDepth = 1.0;
  vkCmdSetViewport(frames_[current_frame_].draw_command_buffer, 0, 1,
                   &viewport);
}

void RendererVulkan::ResetViewport() {
  VkViewport viewport{};
  viewport.x = 0;
  viewport.y = (float)context_.GetFramebufferHeight();
  viewport.width = (float)context_.GetFramebufferWidth();
  viewport.height = -(float)context_.GetFramebufferHeight();
  viewport.minDepth = 0;
  viewport.maxDepth = 1.0;
  vkCmdSetViewport(frames_[current_frame_].draw_command_buffer, 0, 1,
                   &viewport);
}

void RendererVulkan::SetScissor(int x, int y, int width, int height) {
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (x + width > GetFramebufferWidth())
    width = GetFramebufferWidth() - x;
  if (y + height > GetFramebufferHeight())
    height = GetFramebufferHeight() - y;

  VkRect2D scissor{};
  scissor.offset.x = x;
  scissor.offset.y = y;
  scissor.extent.width = width;
  scissor.extent.height = height;
  vkCmdSetScissor(frames_[current_frame_].draw_command_buffer, 0, 1, &scissor);
}

void RendererVulkan::ResetScissor() {
  VkRect2D scissor{};
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  scissor.extent.width = context_.GetFramebufferWidth();
  scissor.extent.height = context_.GetFramebufferHeight();
  vkCmdSetScissor(frames_[current_frame_].draw_command_buffer, 0, 1, &scissor);
}

Renderer::ResourceId RendererVulkan::CreateGeometry(
    Primitive primitive,
    VertexDescription vertex_description,
    DataType index_description) {
  auto& geometry = geometries_[++last_resource_id_] = {};
  geometry.vertex_size = GetVertexSize(vertex_description);
  geometry.index_type = GetIndexType(index_description);
  geometry.index_type_size = GetIndexSize(index_description);
  return last_resource_id_;
}

void RendererVulkan::UpdateGeometry(ResourceId resource_id,
                                    size_t num_vertices,
                                    const void* vertices,
                                    size_t num_indices,
                                    const void* indices) {
  auto it = geometries_.find(resource_id);
  if (it == geometries_.end())
    return;

  it->second.num_vertices = static_cast<uint32_t>(num_vertices);
  size_t vertex_data_size = it->second.vertex_size * it->second.num_vertices;
  size_t index_data_size = 0;

  if (indices) {
    DCHECK(it->second.index_type != VK_INDEX_TYPE_NONE_KHR);
    it->second.num_indices = static_cast<uint32_t>(num_indices);
    index_data_size = it->second.index_type_size * it->second.num_indices;
  }

  size_t data_size = vertex_data_size + index_data_size;
  if (it->second.buffer_size < data_size) {
    DLOG(1) << __func__ << "Reallocate buffer " << data_size;
    if (it->second.buffer_size > 0)
      FreeBuffer(std::move(it->second.buffer));
    AllocateBuffer(it->second.buffer, static_cast<uint32_t>(data_size),
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                       (indices ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : 0),
                   VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    it->second.buffer_size = data_size;
  }

  task_runner_.PostTask(HERE, std::bind(&RendererVulkan::UpdateBuffer, this,
                                        std::get<0>(it->second.buffer), 0,
                                        vertices, vertex_data_size));
  if (it->second.num_indices > 0) {
    it->second.index_data_offset = vertex_data_size;
    task_runner_.PostTask(HERE, std::bind(&RendererVulkan::UpdateBuffer, this,
                                          std::get<0>(it->second.buffer),
                                          it->second.index_data_offset, indices,
                                          index_data_size));
  }
  task_runner_.PostTask(HERE,
                        std::bind(&RendererVulkan::BufferMemoryBarrier, this,
                                  std::get<0>(it->second.buffer), 0, data_size,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                  VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT));
  semaphore_.release();
}

void RendererVulkan::DestroyGeometry(ResourceId resource_id) {
  auto it = geometries_.find(resource_id);
  if (it == geometries_.end())
    return;

  FreeBuffer(std::move(it->second.buffer));
  geometries_.erase(it);
}

void RendererVulkan::Draw(ResourceId resource_id,
                          size_t num_indices,
                          size_t start_offset) {
  auto it = geometries_.find(resource_id);
  if (it == geometries_.end())
    return;

  // Update the values of push constants for the active shader if dirty.
  if (active_shader_id_ != kInvalidId) {
    auto active_shader = shaders_.find(active_shader_id_);
    if (active_shader != shaders_.end() &&
        active_shader->second.push_constants_dirty) {
      active_shader->second.push_constants_dirty = false;
      vkCmdPushConstants(
          frames_[current_frame_].draw_command_buffer,
          active_shader->second.pipeline_layout,
          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
          static_cast<uint32_t>(active_shader->second.push_constants_size),
          active_shader->second.push_constants.get());
    }
  }

  uint64_t data_offset = start_offset * it->second.index_type_size;
  if (num_indices == 0)
    num_indices = it->second.num_indices;

  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(frames_[current_frame_].draw_command_buffer, 0, 1,
                         &std::get<0>(it->second.buffer), &offset);
  if (num_indices > 0) {
    vkCmdBindIndexBuffer(frames_[current_frame_].draw_command_buffer,
                         std::get<0>(it->second.buffer),
                         it->second.index_data_offset + data_offset,
                         it->second.index_type);
    vkCmdDrawIndexed(frames_[current_frame_].draw_command_buffer,
                     static_cast<uint32_t>(num_indices), 1, 0, 0, 0);
  } else {
    vkCmdDraw(frames_[current_frame_].draw_command_buffer,
              it->second.num_vertices, 1, 0, 0);
  }
}

Renderer::ResourceId RendererVulkan::CreateTexture() {
  textures_.insert({++last_resource_id_, {}});
  return last_resource_id_;
}

void RendererVulkan::UpdateTexture(ResourceId resource_id,
                                   int width,
                                   int height,
                                   ImageFormat format,
                                   size_t data_size,
                                   uint8_t* image_data) {
  auto it = textures_.find(resource_id);
  if (it == textures_.end())
    return;

  VkImageLayout old_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkFormat vk_format = GetImageFormat(format);

  if (it->second.view != VK_NULL_HANDLE &&
      (it->second.width != width || it->second.height != height)) {
    // Size mismatch. Recreate the texture.
    FreeImage(std::move(it->second.image), it->second.view,
              std::move(it->second.desc_set), it->second.frame_buffer_);
    it->second = {};
  }

  if (it->second.view == VK_NULL_HANDLE) {
    AllocateImage(it->second.image, it->second.view, it->second.desc_set,
                  vk_format, width, height,
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                  VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    it->second.width = width;
    it->second.height = height;
  }

  task_runner_.PostTask(
      HERE,
      std::bind(&RendererVulkan::ImageMemoryBarrier, this,
                frames_[current_frame_].setup_command_buffer,
                std::get<0>(it->second.image),
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                old_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
  task_runner_.PostTask(HERE, std::bind(&RendererVulkan::UpdateImage, this,
                                        std::get<0>(it->second.image),
                                        vk_format, image_data, width, height));
  task_runner_.PostTask(
      HERE,
      std::bind(&RendererVulkan::ImageMemoryBarrier, this,
                frames_[current_frame_].setup_command_buffer,
                std::get<0>(it->second.image), VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
  semaphore_.release();
}

void RendererVulkan::UpdateTextureSubRegion(ResourceId resource_id,
                                            int x_offset,
                                            int y_offset,
                                            int width,
                                            int height,
                                            ImageFormat format,
                                            int src_pitch,
                                            uint8_t* image_data) {
  auto it = textures_.find(resource_id);
  if (it == textures_.end())
    return;

  VkFormat vk_format = GetImageFormat(format);

  task_runner_.PostTask(
      HERE,
      std::bind(&RendererVulkan::ImageMemoryBarrier, this,
                frames_[current_frame_].setup_command_buffer,
                std::get<0>(it->second.image),
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
  task_runner_.PostTask(
      HERE, std::bind(&RendererVulkan::UpdateImageSubRegion, this,
                      std::get<0>(it->second.image), vk_format, image_data,
                      x_offset, y_offset, width, height, src_pitch));
  task_runner_.PostTask(
      HERE,
      std::bind(&RendererVulkan::ImageMemoryBarrier, this,
                frames_[current_frame_].setup_command_buffer,
                std::get<0>(it->second.image), VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
  semaphore_.release();
}

void RendererVulkan::DestroyTexture(ResourceId resource_id) {
  auto it = textures_.find(resource_id);
  if (it == textures_.end())
    return;

  FreeImage(std::move(it->second.image), it->second.view,
            std::move(it->second.desc_set), it->second.frame_buffer_);
  textures_.erase(it);
}

void RendererVulkan::ActivateTexture(ResourceId resource_id,
                                     size_t texture_unit) {
  auto it = textures_.find(resource_id);
  if (it == textures_.end())
    return;

  if (active_descriptor_sets_[texture_unit] !=
      std::get<0>(it->second.desc_set)) {
    active_descriptor_sets_[texture_unit] = std::get<0>(it->second.desc_set);
    if (active_shader_id_ != kInvalidId) {
      auto active_shader = shaders_.find(active_shader_id_);
      if (active_shader != shaders_.end() &&
          active_shader->second.desc_set_count > texture_unit) {
        vkCmdBindDescriptorSets(frames_[current_frame_].draw_command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                active_shader->second.pipeline_layout,
                                static_cast<uint32_t>(texture_unit), 1,
                                &active_descriptor_sets_[texture_unit], 0,
                                nullptr);
      }
    }
  }
}

Renderer::ResourceId RendererVulkan::CreateShader(
    std::unique_ptr<ShaderSource> source,
    const VertexDescription& vertex_description,
    Primitive primitive,
    bool enable_depth_test) {
  std::string error;
  auto spirv_vertex =
      CompileGlsl(EShLangVertex, source->GetVertexSource(), &error);
  if (!error.empty())
    DLOG(0) << source->name() << " vertex shader compile error: " << error;
  auto spirv_fragment =
      CompileGlsl(EShLangFragment, source->GetFragmentSource(), &error);
  if (!error.empty())
    DLOG(0) << source->name() << " fragment shader compile error: " << error;

  VkShaderModule vert_shader_module;
  {
    VkShaderModuleCreateInfo shader_module_info{};
    shader_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_module_info.codeSize = spirv_vertex.size();
    shader_module_info.pCode =
        reinterpret_cast<const uint32_t*>(spirv_vertex.data());

    if (vkCreateShaderModule(device_, &shader_module_info, nullptr,
                             &vert_shader_module) != VK_SUCCESS) {
      DLOG(0) << "vkCreateShaderModule failed!";
      return 0;
    }
  }

  VkShaderModule frag_shader_module;
  {
    VkShaderModuleCreateInfo shader_module_info{};
    shader_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_module_info.codeSize = spirv_fragment.size();
    shader_module_info.pCode =
        reinterpret_cast<const uint32_t*>(spirv_fragment.data());

    if (vkCreateShaderModule(device_, &shader_module_info, nullptr,
                             &frag_shader_module) != VK_SUCCESS) {
      DLOG(0) << "vkCreateShaderModule failed!";
      vkDestroyShaderModule(device_, vert_shader_module, nullptr);
      return 0;
    }
  }

  auto& shader = shaders_[++last_resource_id_] = {};

  if (!CreatePipelineLayout(shader, spirv_vertex, spirv_fragment))
    DLOG(0) << "Failed to create pipeline layout!";

  VkPipelineShaderStageCreateInfo vert_shader_stage_info{};
  vert_shader_stage_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vert_shader_stage_info.module = vert_shader_module;
  vert_shader_stage_info.pName = "main";

  VkPipelineShaderStageCreateInfo frag_shader_stage_info{};
  frag_shader_stage_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  frag_shader_stage_info.module = frag_shader_module;
  frag_shader_stage_info.pName = "main";

  VkPipelineShaderStageCreateInfo shaderStages[] = {vert_shader_stage_info,
                                                    frag_shader_stage_info};

  VertexInputDescription vertex_input =
      GetVertexInputDescription(vertex_description);

  VkPipelineVertexInputStateCreateInfo vertex_input_info{};
  vertex_input_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input_info.vertexBindingDescriptionCount =
      static_cast<uint32_t>(std::get<0>(vertex_input).size());
  vertex_input_info.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(std::get<1>(vertex_input).size());
  vertex_input_info.pVertexBindingDescriptions =
      std::get<0>(vertex_input).data();
  vertex_input_info.pVertexAttributeDescriptions =
      std::get<1>(vertex_input).data();

  VkPipelineInputAssemblyStateCreateInfo input_assembly{};
  input_assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = kVkPrimitiveType[primitive];
  input_assembly.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = nullptr;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = nullptr;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachment{};
  color_blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  color_blend_attachment.blendEnable = VK_TRUE;
  color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  color_blend_attachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo color_blending{};
  color_blending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blending.logicOpEnable = VK_FALSE;
  color_blending.logicOp = VK_LOGIC_OP_COPY;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;
  color_blending.blendConstants[0] = 0.0f;
  color_blending.blendConstants[1] = 0.0f;
  color_blending.blendConstants[2] = 0.0f;
  color_blending.blendConstants[3] = 0.0f;

  VkPipelineDepthStencilStateCreateInfo depth_stencil{};
  depth_stencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_stencil.depthTestEnable = enable_depth_test ? VK_TRUE : VK_FALSE;
  depth_stencil.depthWriteEnable = enable_depth_test ? VK_TRUE : VK_FALSE;
  depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depth_stencil.depthBoundsTestEnable = VK_FALSE;
  depth_stencil.stencilTestEnable = VK_FALSE;

  std::vector<VkDynamicState> dynamic_states;
  dynamic_states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
  dynamic_states.push_back(VK_DYNAMIC_STATE_SCISSOR);

  VkPipelineDynamicStateCreateInfo dynamic_state_create_info{};
  dynamic_state_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state_create_info.dynamicStateCount =
      static_cast<uint32_t>(dynamic_states.size());
  dynamic_state_create_info.pDynamicStates = dynamic_states.data();

  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = shaderStages;
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDepthStencilState = &depth_stencil;
  pipeline_info.pDynamicState = &dynamic_state_create_info;
  pipeline_info.layout = shader.pipeline_layout;
  pipeline_info.renderPass = context_.GetRenderPass();
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

  if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info,
                                nullptr, &shader.pipeline) != VK_SUCCESS)
    DLOG(0) << "failed to create graphics pipeline.";

  vkDestroyShaderModule(device_, frag_shader_module, nullptr);
  vkDestroyShaderModule(device_, vert_shader_module, nullptr);
  return last_resource_id_;
}

void RendererVulkan::DestroyShader(ResourceId resource_id) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  frames_[current_frame_].pipelines_to_destroy.emplace_back(
      it->second.pipeline, it->second.pipeline_layout);
  shaders_.erase(it);
}

void RendererVulkan::ActivateShader(ResourceId resource_id) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  if (active_shader_id_ != resource_id) {
    active_shader_id_ = resource_id;
    vkCmdBindPipeline(frames_[current_frame_].draw_command_buffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS, it->second.pipeline);

    for (size_t i = 0; i < it->second.desc_set_count; ++i) {
      if (active_descriptor_sets_[i] != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(frames_[current_frame_].draw_command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                it->second.pipeline_layout,
                                static_cast<uint32_t>(i), 1,
                                &active_descriptor_sets_[i], 0, nullptr);
      }
    }
  }
}

void RendererVulkan::SetUniform(ResourceId resource_id,
                                const std::string& name,
                                const base::Vector2f& val) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  SetUniformInternal(it->second, name, val);
}

void RendererVulkan::SetUniform(ResourceId resource_id,
                                const std::string& name,
                                const base::Vector3f& val) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  SetUniformInternal(it->second, name, val);
}

void RendererVulkan::SetUniform(ResourceId resource_id,
                                const std::string& name,
                                const base::Vector4f& val) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  SetUniformInternal(it->second, name, val);
}

void RendererVulkan::SetUniform(ResourceId resource_id,
                                const std::string& name,
                                const base::Matrix4f& val) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  SetUniformInternal(it->second, name, val);
}

void RendererVulkan::SetUniform(ResourceId resource_id,
                                const std::string& name,
                                float val) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  SetUniformInternal(it->second, name, val);
}

void RendererVulkan::SetUniform(ResourceId resource_id,
                                const std::string& name,
                                int val) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  for (auto& sampler_name : it->second.sampler_uniform_names) {
    if (name == sampler_name)
      return;
  }
  SetUniformInternal(it->second, name, val);
}

void RendererVulkan::SetClearColor(const base::Vector4f& color) {
  clear_color_ = {{color.x, color.y, color.z, color.w}};
}

void RendererVulkan::PrepareForDrawing() {
  frame_prepared_ = false;

  if (GetFramebufferWidth() == 0 || GetFramebufferHeight() == 0)
    return;

  if (!context_.PrepareBuffers())
    return;

  frame_prepared_ = true;
  DrawListBegin(onscreen_clear_pass_);
}

void RendererVulkan::Present() {
  if (!frame_prepared_)
    return;

  DrawListEnd();
  SwapBuffers();
}

void RendererVulkan::BeginRenderToTexture(ResourceId texture_id) {
  DCHECK(!rendering_offscreen_);

  auto it = textures_.find(texture_id);
  if (it == textures_.end()) {
    DLOG(0) << "Texture not found";
    return;
  }

  vkCmdEndRenderPass(frames_[current_frame_].draw_command_buffer);

  // Ensure previous onscreen pass's read from the texture is complete before
  // offscreen pass writes to it and preform the Read -> Write transition.
  ImageMemoryBarrier(
      frames_[current_frame_].draw_command_buffer,
      std::get<0>(it->second.image), VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  if (it->second.frame_buffer_ == VK_NULL_HANDLE) {
    // Create a new framebuffer with the texture as the color attachment.
    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = offscreen_write_pass_;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &it->second.view;
    framebuffer_info.width = it->second.width;
    framebuffer_info.height = it->second.height;
    framebuffer_info.layers = 1;

    VkResult err = vkCreateFramebuffer(device_, &framebuffer_info, nullptr,
                                       &it->second.frame_buffer_);
    if (err) {
      DLOG(0) << "vkCreateFramebuffer failed with error "
              << string_VkResult(err);
      return;
    }
  }

  // Begin the render pass with the new framebuffer
  VkRenderPassBeginInfo render_pass_begin{};
  render_pass_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_begin.renderPass = offscreen_write_pass_;
  render_pass_begin.framebuffer = it->second.frame_buffer_;
  render_pass_begin.renderArea.extent.width = it->second.width;
  render_pass_begin.renderArea.extent.height = it->second.height;
  render_pass_begin.renderArea.offset.x = 0;
  render_pass_begin.renderArea.offset.y = 0;

  VkClearValue clear_value{};
  clear_value.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  render_pass_begin.clearValueCount = 1;
  render_pass_begin.pClearValues = &clear_value;

  vkCmdBeginRenderPass(frames_[current_frame_].draw_command_buffer,
                       &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
  rendering_offscreen_ = true;

  VkViewport viewport{};
  viewport.x = 0;
  viewport.y = 0;
  viewport.width = (float)it->second.width;
  viewport.height = (float)it->second.height;
  viewport.minDepth = 0;
  viewport.maxDepth = 1.0;
  vkCmdSetViewport(frames_[current_frame_].draw_command_buffer, 0, 1,
                   &viewport);

  SetScissor(0, 0, it->second.width, it->second.height);
}

void RendererVulkan::EndRenderToTexture(ResourceId texture_id) {
  vkCmdEndRenderPass(frames_[current_frame_].draw_command_buffer);
  rendering_offscreen_ = false;

  auto it = textures_.find(texture_id);
  if (it == textures_.end()) {
    DLOG(0) << "Texture not found";
    return;
  }

  // Ensure previous onscreen pass's write to the Swapchain is complete before
  // the next onscreen pass potentially loads it later.
  ImageMemoryBarrier(
      frames_[current_frame_].draw_command_buffer, context_.GetSwapchainImage(),
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      VK_ACCESS_NONE, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  // Ensure offscreen pass's write to texture is complete and transitions it to
  // a readable state for the next onscreen pass's fragment shader.
  ImageMemoryBarrier(frames_[current_frame_].draw_command_buffer,
                     std::get<0>(it->second.image),
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_ACCESS_SHADER_READ_BIT,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  DrawListBegin(onscreen_load_pass_);
}

bool RendererVulkan::InitializeInternal() {
  glslang::InitializeProcess();

  device_ = context_.GetDevice();

  // Allocate one extra frame to ensure it's unused at any time without having
  // to use a fence.
  int frame_count = context_.GetSwapchainImageCount() + 1;
  frames_.resize(frame_count);
  frames_drawn_ = frame_count;

  for (size_t i = 0; i < frames_.size(); i++) {
    // Create command pool, one per frame is recommended.
    VkCommandPoolCreateInfo cmd_pool_info{};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.queueFamilyIndex = context_.GetGraphicsQueue();

    VkResult err = vkCreateCommandPool(device_, &cmd_pool_info, nullptr,
                                       &frames_[i].setup_command_pool);
    if (err) {
      DLOG(0) << "vkCreateCommandPool failed with error "
              << string_VkResult(err);
      return false;
    }

    err = vkCreateCommandPool(device_, &cmd_pool_info, nullptr,
                              &frames_[i].draw_command_pool);
    if (err) {
      DLOG(0) << "vkCreateCommandPool failed with error "
              << string_VkResult(err);
      return false;
    }

    // Create command buffers.
    VkCommandBufferAllocateInfo cmd_buf_info{};
    cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_buf_info.commandPool = frames_[i].setup_command_pool;
    cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_buf_info.commandBufferCount = 1;

    err = vkAllocateCommandBuffers(device_, &cmd_buf_info,
                                   &frames_[i].setup_command_buffer);
    if (err) {
      DLOG(0) << "vkAllocateCommandBuffers failed with error "
              << string_VkResult(err);
      continue;
    }

    cmd_buf_info.commandPool = frames_[i].draw_command_pool;
    err = vkAllocateCommandBuffers(device_, &cmd_buf_info,
                                   &frames_[i].draw_command_buffer);
    if (err) {
      DLOG(0) << "vkAllocateCommandBuffers failed with error "
              << string_VkResult(err);
      continue;
    }
  }

  // In this simple engine we use only one descriptor set layout that is for
  // textures. We use push constants for everything else.
  VkDescriptorSetLayoutBinding ds_layout_binding{};
  ds_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  ds_layout_binding.descriptorCount = 1;
  ds_layout_binding.binding = 0;
  ds_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  ds_layout_binding.pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo ds_layout_info{};
  ds_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  ds_layout_info.bindingCount = 1;
  ds_layout_info.pBindings = &ds_layout_binding;

  VkResult err = vkCreateDescriptorSetLayout(device_, &ds_layout_info, nullptr,
                                             &descriptor_set_layout_);
  if (err) {
    DLOG(0) << "Error (" << string_VkResult(err)
            << ") creating descriptor set layout for set";
    return false;
  }

  // Create sampler.
  VkSamplerCreateInfo sampler_info{};
  sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.mipLodBias = 0;
  sampler_info.anisotropyEnable = VK_FALSE;
  sampler_info.maxAnisotropy = 0;
  sampler_info.compareEnable = VK_FALSE;
  sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
  sampler_info.minLod = 0;
  sampler_info.maxLod = 0;
  sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  sampler_info.unnormalizedCoordinates = VK_FALSE;

  err = vkCreateSampler(device_, &sampler_info, nullptr, &sampler_);
  if (err) {
    DLOG(0) << "vkCreateSampler failed with error " << string_VkResult(err);
    return false;
  }

  // Render Passes

  // Shared dependency for onscreen pass start Sync. Ensures all previous
  // commands on the queue have completed their execution before the render pass
  // starts.
  VkSubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  // Shared References
  VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;

  // Set common values for color attachment.
  VkAttachmentDescription color_attachment{};
  color_attachment.format = context_.GetScreenFormat();
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkRenderPassCreateInfo rp_info{};
  rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rp_info.attachmentCount = 1;
  rp_info.pAttachments = &color_attachment;
  rp_info.subpassCount = 1;
  rp_info.pSubpasses = &subpass;
  rp_info.dependencyCount = 1;
  rp_info.pDependencies = &dependency;

  // First onscreen render pass clears the screen. The required layout
  // transition is performed implicitly at the start of the render pass.
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  err = vkCreateRenderPass(device_, &rp_info, nullptr, &onscreen_clear_pass_);
  if (err) {
    DLOG(0) << "vkCreateRenderPass failed. Error: " << string_VkResult(err);
    return false;
  }

  // Following onscreen render pass loads the result from the previous pass.
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  err = vkCreateRenderPass(device_, &rp_info, nullptr, &onscreen_load_pass_);
  if (err) {
    DLOG(0) << "vkCreateRenderPass failed. Error: " << string_VkResult(err);
    return false;
  }

  // Offscreen pass doesn't care about the Swapchain Image and doesn't have to
  // wait for any specific previous stage.
  dependency.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  dependency.srcAccessMask = VK_ACCESS_NONE;
  // Offscreen render pass will cover the target texture and an external
  // pipeline barrier is executed for layout transition before it begins.
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  err = vkCreateRenderPass(device_, &rp_info, nullptr, &offscreen_write_pass_);
  if (err) {
    DLOG(0) << "vkCreateRenderPass failed. Error: " << string_VkResult(err);
    return false;
  }

  texture_compression_.dxt1 = IsFormatSupported(VK_FORMAT_BC1_RGB_UNORM_BLOCK);
  texture_compression_.s3tc = IsFormatSupported(VK_FORMAT_BC3_UNORM_BLOCK);

  DLOG(0) << "TextureCompression:";
  DLOG(0) << "  atc:   " << texture_compression_.atc;
  DLOG(0) << "  dxt1:  " << texture_compression_.dxt1;
  DLOG(0) << "  etc1:  " << texture_compression_.etc1;
  DLOG(0) << "  s3tc:  " << texture_compression_.s3tc;

  current_staging_buffer_ = 0;
  staging_buffer_used_ = false;

  if (max_staging_buffer_size_ <
      static_cast<uint64_t>(staging_buffer_size_) * 4)
    max_staging_buffer_size_ = static_cast<uint64_t>(staging_buffer_size_) * 4;

  for (int i = 0; i < frame_count; i++) {
    bool err = InsertStagingBuffer();
    LOG_IF(0, !err) << "Failed to create staging buffer.";
  }

  // Use a background thread for filling up staging buffers and recording setup
  // commands.
  quit_.store(false, std::memory_order_relaxed);
  setup_thread_ = std::thread(&RendererVulkan::SetupThreadMain, this);

  // Begin the first command buffer for the first frame.
  BeginFrame();

  return true;
}

void RendererVulkan::Shutdown() {
  DLOG(0) << "Shutting down renderer.";
  if (device_ != VK_NULL_HANDLE) {
    task_runner_.CancelTasks();
    quit_.store(true, std::memory_order_relaxed);
    semaphore_.release();
    setup_thread_.join();

    for (size_t i = 0; i < staging_buffers_.size(); i++) {
      auto [buffer, allocation] = staging_buffers_[i].buffer;
      vmaDestroyBuffer(context_.GetAllocator(), buffer, allocation);
    }

    DestroyAllResources();

    vkDeviceWaitIdle(device_);

    for (size_t i = 0; i < frames_.size(); ++i) {
      FreePendingResources(static_cast<int>(i));
      vkDestroyCommandPool(device_, frames_[i].setup_command_pool, nullptr);
      vkDestroyCommandPool(device_, frames_[i].draw_command_pool, nullptr);
    }

    vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
    vkDestroySampler(device_, sampler_, nullptr);

    device_ = VK_NULL_HANDLE;
    frames_drawn_ = 0;
    frames_.clear();
    current_frame_ = 0;

    staging_buffers_.clear();
    current_staging_buffer_ = 0;
    staging_buffer_used_ = false;

    glslang::FinalizeProcess();
  }

  context_.DestroySurface();
  context_.Shutdown();
}

void RendererVulkan::BeginFrame() {
  FreePendingResources(current_frame_);

  context_.AppendCommandBuffer(frames_[current_frame_].setup_command_buffer);
  context_.AppendCommandBuffer(frames_[current_frame_].draw_command_buffer);

  vkResetCommandPool(device_, frames_[current_frame_].setup_command_pool, 0);
  vkResetCommandPool(device_, frames_[current_frame_].draw_command_pool, 0);

  VkCommandBufferBeginInfo cmd_buf_begin{};
  cmd_buf_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cmd_buf_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  VkResult err = vkBeginCommandBuffer(
      frames_[current_frame_].setup_command_buffer, &cmd_buf_begin);
  if (err) {
    DLOG(0) << "vkBeginCommandBuffer failed with error "
            << string_VkResult(err);
    return;
  }

  err = vkBeginCommandBuffer(frames_[current_frame_].draw_command_buffer,
                             &cmd_buf_begin);
  if (err) {
    DLOG(0) << "vkBeginCommandBuffer failed with error "
            << string_VkResult(err);
    return;
  }

  // Advance current frame.
  frames_drawn_++;

  // Advance staging buffer if used. All tasks in bg thread are complete so this
  // is thread safe.
  if (staging_buffer_used_) {
    current_staging_buffer_ =
        (current_staging_buffer_ + 1) % staging_buffers_.size();
    staging_buffer_used_ = false;
  }
}

void RendererVulkan::FlushSetupBuffer() {
  vkEndCommandBuffer(frames_[current_frame_].setup_command_buffer);

  context_.Flush(false);

  vkResetCommandPool(device_, frames_[current_frame_].setup_command_pool, 0);

  VkCommandBufferBeginInfo cmd_buf_begin{};
  cmd_buf_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cmd_buf_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  VkResult err = vkBeginCommandBuffer(
      frames_[current_frame_].setup_command_buffer, &cmd_buf_begin);
  if (err) {
    DLOG(0) << "vkBeginCommandBuffer failed with error "
            << string_VkResult(err);
    return;
  }
  context_.AppendCommandBuffer(frames_[current_frame_].setup_command_buffer,
                               true);
}

void RendererVulkan::FreePendingResources(int frame) {
  if (!frames_[frame].pipelines_to_destroy.empty()) {
    for (auto& pipeline : frames_[frame].pipelines_to_destroy) {
      vkDestroyPipeline(device_, std::get<0>(pipeline), nullptr);
      vkDestroyPipelineLayout(device_, std::get<1>(pipeline), nullptr);
    }
    frames_[frame].pipelines_to_destroy.clear();
  }

  if (!frames_[frame].images_to_destroy.empty()) {
    for (auto& image : frames_[frame].images_to_destroy) {
      auto [buffer, view, frame_buffer] = image;
      vkDestroyImageView(device_, view, nullptr);
      vmaDestroyImage(context_.GetAllocator(), std::get<0>(buffer),
                      std::get<1>(buffer));
      if (frame_buffer != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, frame_buffer, nullptr);
    }
    frames_[frame].images_to_destroy.clear();
  }

  if (!frames_[frame].buffers_to_destroy.empty()) {
    for (auto& buffer : frames_[frame].buffers_to_destroy)
      vmaDestroyBuffer(context_.GetAllocator(), std::get<0>(buffer),
                       std::get<1>(buffer));
    frames_[frame].buffers_to_destroy.clear();
  }

  if (!frames_[frame].desc_sets_to_destroy.empty()) {
    for (auto& desc_set : frames_[frame].desc_sets_to_destroy) {
      auto [set, pool] = desc_set;
      vkFreeDescriptorSets(device_, std::get<0>(*pool), 1, &set);
      FreeDescriptorPool(pool);
    }
    frames_[frame].desc_sets_to_destroy.clear();
  }
}

void RendererVulkan::MemoryBarrier(VkPipelineStageFlags src_stage_mask,
                                   VkPipelineStageFlags dst_stage_mask,
                                   VkAccessFlags src_access,
                                   VkAccessFlags dst_access) {
  VkMemoryBarrier mem_barrier{};
  mem_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  mem_barrier.srcAccessMask = src_access;
  mem_barrier.dstAccessMask = dst_access;

  vkCmdPipelineBarrier(frames_[current_frame_].draw_command_buffer,
                       src_stage_mask, dst_stage_mask, 0, 1, &mem_barrier, 0,
                       nullptr, 0, nullptr);
}

void RendererVulkan::FullBarrier() {
  // Used for debug.
  MemoryBarrier(
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
          VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT |
          VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
          VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
          VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
          VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT,
      VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
          VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT |
          VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
          VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
          VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
          VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT);
}

bool RendererVulkan::AllocateStagingBuffer(uint32_t amount,
                                           uint32_t segment,
                                           uint32_t alignment,
                                           uint32_t& alloc_offset,
                                           uint32_t& alloc_size) {
  DCHECK(std::this_thread::get_id() == setup_thread_.get_id());

  alloc_size = amount;

  while (true) {
    alloc_offset = 0;

    // See if we can use the current block.
    if (staging_buffers_[current_staging_buffer_].frame_used == frames_drawn_) {
      // We used this block this frame, let's see if there is still room.
      uint32_t write_from =
          staging_buffers_[current_staging_buffer_].fill_amount;
      uint32_t align_remainder = write_from % alignment;
      if (align_remainder != 0)
        write_from += alignment - align_remainder;
      int32_t available_bytes =
          int32_t(staging_buffer_size_) - int32_t(write_from);

      if ((int32_t)amount < available_bytes) {
        // All will fit.
        alloc_offset = write_from;
      } else if (segment > 0 && available_bytes >= (int32_t)segment) {
        // All won't fit but at least we can fit a chunk.
        alloc_offset = write_from;
        alloc_size = available_bytes - (available_bytes % segment);
      } else {
        // Try next buffer.
        current_staging_buffer_ =
            (current_staging_buffer_ + 1) % staging_buffers_.size();

        if (staging_buffers_[current_staging_buffer_].frame_used ==
            frames_drawn_) {
          // We manage to fill all blocks possible in a single frame. Check if
          // we can insert a new block.
          if (staging_buffers_.size() * staging_buffer_size_ <
              max_staging_buffer_size_) {
            if (!InsertStagingBuffer())
              return false;

            // Claim for current frame.
            staging_buffers_[current_staging_buffer_].frame_used =
                frames_drawn_;
          } else {
            // All the staging buffers belong to this frame and we can't create
            // more. Flush setup buffer to make room.
            FlushSetupBuffer();

            // Clear the whole staging buffer.
            for (size_t i = 0; i < staging_buffers_.size(); i++) {
              staging_buffers_[i].frame_used = 0;
              staging_buffers_[i].fill_amount = 0;
            }
            // Claim for current frame.
            staging_buffers_[current_staging_buffer_].frame_used =
                frames_drawn_;
          }
        } else {
          // Block is not from current frame, so continue and try again.
          continue;
        }
      }
    } else if (staging_buffers_[current_staging_buffer_].frame_used <=
               frames_drawn_ - frames_.size()) {
      // This is an old block, which was already processed, let's reuse.
      staging_buffers_[current_staging_buffer_].frame_used = frames_drawn_;
      staging_buffers_[current_staging_buffer_].fill_amount = 0;
    } else if (staging_buffers_[current_staging_buffer_].frame_used >
               frames_drawn_ - frames_.size()) {
      // This block may still be in use, let's not touch it unless we have to.
      // Check if we can insert a new block.
      if (staging_buffers_.size() * staging_buffer_size_ <
          max_staging_buffer_size_) {
        if (!InsertStagingBuffer())
          return false;

        // Claim for current frame.
        staging_buffers_[current_staging_buffer_].frame_used = frames_drawn_;
      } else {
        // We are out of room and we can't create more. Ensure older frames are
        // executed.
        vkDeviceWaitIdle(device_);

        for (size_t i = 0; i < staging_buffers_.size(); i++) {
          // Clear all blocks but the ones from this frame.
          size_t block_idx =
              (i + current_staging_buffer_) % staging_buffers_.size();
          if (staging_buffers_[block_idx].frame_used == frames_drawn_) {
            break;  // We reached something from this frame, abort.
          }

          staging_buffers_[block_idx].frame_used = 0;
          staging_buffers_[block_idx].fill_amount = 0;
        }
        // Claim for current frame.
        staging_buffers_[current_staging_buffer_].frame_used = frames_drawn_;
      }
    }

    // Done.
    break;
  }

  staging_buffers_[current_staging_buffer_].fill_amount =
      alloc_offset + alloc_size;
  staging_buffer_used_ = true;
  return true;
}

bool RendererVulkan::InsertStagingBuffer() {
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = staging_buffer_size_;
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo alloc_info{};
  alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;  // Stay mapped.
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
  alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

  StagingBuffer block;

  VkResult err = vmaCreateBuffer(context_.GetAllocator(), &buffer_info,
                                 &alloc_info, &std::get<0>(block.buffer),
                                 &std::get<1>(block.buffer), &block.alloc_info);
  if (err) {
    DLOG(0) << "vmaCreateBuffer failed with error " << string_VkResult(err);
    return false;
  }

  block.frame_used = 0;
  block.fill_amount = 0;

  staging_buffers_.insert(staging_buffers_.begin() + current_staging_buffer_,
                          block);
  return true;
}

RendererVulkan::DescPool* RendererVulkan::AllocateDescriptorPool() {
  DescPool* selected_pool = nullptr;

  for (auto& dp : desc_pools_) {
    if (std::get<1>(*dp) < kMaxDescriptorsPerPool) {
      selected_pool = dp.get();
      break;
    }
  }

  if (!selected_pool) {
    VkDescriptorPoolSize sizes{};
    sizes.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes.descriptorCount = kMaxDescriptorsPerPool;

    VkDescriptorPoolCreateInfo descriptor_pool_create_info{};
    descriptor_pool_create_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_create_info.flags =
        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptor_pool_create_info.maxSets = kMaxDescriptorsPerPool;
    descriptor_pool_create_info.poolSizeCount = 1;
    descriptor_pool_create_info.pPoolSizes = &sizes;

    VkDescriptorPool desc_pool;
    VkResult err = vkCreateDescriptorPool(device_, &descriptor_pool_create_info,
                                          nullptr, &desc_pool);
    if (err) {
      DLOG(0) << "vkCreateDescriptorPool failed with error "
              << string_VkResult(err);
      return VK_NULL_HANDLE;
    }

    auto pool = std::make_unique<DescPool>(std::make_tuple(desc_pool, 0));
    selected_pool = pool.get();
    desc_pools_.push_back(std::move(pool));
  }

  ++std::get<1>(*selected_pool);
  return selected_pool;
}

void RendererVulkan::FreeDescriptorPool(DescPool* desc_pool) {
  if (--std::get<1>(*desc_pool) == 0) {
    for (auto it = desc_pools_.begin(); it != desc_pools_.end(); ++it) {
      if (std::get<0>(**it) == std::get<0>(*desc_pool)) {
        vkDestroyDescriptorPool(device_, std::get<0>(*desc_pool), nullptr);
        desc_pools_.erase(it);
        return;
      }
    }
    NOTREACHED();
  }
}

bool RendererVulkan::AllocateBuffer(Buffer<VkBuffer>& buffer,
                                    uint32_t size,
                                    uint32_t usage,
                                    VmaMemoryUsage mapping) {
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo alloc_info{};
  alloc_info.usage = mapping;

  VkBuffer vk_buffer;
  VmaAllocation allocation = nullptr;

  VkResult err = vmaCreateBuffer(context_.GetAllocator(), &buffer_info,
                                 &alloc_info, &vk_buffer, &allocation, nullptr);
  if (err) {
    DLOG(0) << "Can't create buffer of size: " << std::to_string(size)
            << ", error " << string_VkResult(err);
    return false;
  }

  buffer = std::make_tuple(vk_buffer, allocation);

  return true;
}

void RendererVulkan::FreeBuffer(Buffer<VkBuffer> buffer) {
  frames_[current_frame_].buffers_to_destroy.push_back(std::move(buffer));
}

void RendererVulkan::UpdateBuffer(VkBuffer buffer,
                                  size_t offset,
                                  const void* data,
                                  size_t data_size) {
  size_t to_submit = data_size;
  size_t submit_from = 0;

  while (to_submit > 0) {
    uint32_t write_offset;
    uint32_t write_amount;

    if (!AllocateStagingBuffer(
            std::min((uint32_t)to_submit, staging_buffer_size_), 32, 32,
            write_offset, write_amount))
      return;
    Buffer<VkBuffer> staging_buffer =
        staging_buffers_[current_staging_buffer_].buffer;

    // Copy to staging buffer.
    void* data_ptr =
        staging_buffers_[current_staging_buffer_].alloc_info.pMappedData;
    memcpy(((uint8_t*)data_ptr) + write_offset, (char*)data + submit_from,
           write_amount);

    // Insert a command to copy to GPU buffer.
    VkBufferCopy region{};
    region.srcOffset = write_offset;
    region.dstOffset = submit_from + offset;
    region.size = write_amount;

    vkCmdCopyBuffer(frames_[current_frame_].setup_command_buffer,
                    std::get<0>(staging_buffer), buffer, 1, &region);

    to_submit -= write_amount;
    submit_from += write_amount;
  }
}

void RendererVulkan::BufferMemoryBarrier(VkBuffer buffer,
                                         uint64_t from,
                                         uint64_t size,
                                         VkPipelineStageFlags src_stage_mask,
                                         VkPipelineStageFlags dst_stage_mask,
                                         VkAccessFlags src_access,
                                         VkAccessFlags dst_access) {
  VkBufferMemoryBarrier buffer_mem_barrier{};
  buffer_mem_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  buffer_mem_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  buffer_mem_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  buffer_mem_barrier.srcAccessMask = src_access;
  buffer_mem_barrier.dstAccessMask = dst_access;
  buffer_mem_barrier.buffer = buffer;
  buffer_mem_barrier.offset = from;
  buffer_mem_barrier.size = size;

  vkCmdPipelineBarrier(frames_[current_frame_].setup_command_buffer,
                       src_stage_mask, dst_stage_mask, 0, 0, nullptr, 1,
                       &buffer_mem_barrier, 0, nullptr);
}

bool RendererVulkan::AllocateImage(Buffer<VkImage>& image,
                                   VkImageView& view,
                                   DescSet& desc_set,
                                   VkFormat format,
                                   int width,
                                   int height,
                                   VkImageUsageFlags usage,
                                   VmaMemoryUsage mapping) {
  VkImageCreateInfo image_create_info{};
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.extent.width = width;
  image_create_info.extent.height = height;
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.format = format;
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_create_info.usage = usage;
  image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo alloc_info{};
  alloc_info.usage = mapping;

  VkImage vk_image;
  VmaAllocation allocation = nullptr;

  VkResult err = vmaCreateImage(context_.GetAllocator(), &image_create_info,
                                &alloc_info, &vk_image, &allocation, nullptr);
  if (err) {
    DLOG(0) << "vmaCreateImage failed with error " << string_VkResult(err);
    return false;
  }

  VkImageViewCreateInfo image_view_create_info{};
  image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  image_view_create_info.image = vk_image;
  image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  image_view_create_info.format = format;
  image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.subresourceRange.baseMipLevel = 0;
  image_view_create_info.subresourceRange.levelCount = 1;
  image_view_create_info.subresourceRange.baseArrayLayer = 0;
  image_view_create_info.subresourceRange.layerCount = 1;
  image_view_create_info.subresourceRange.aspectMask =
      VK_IMAGE_ASPECT_COLOR_BIT;

  err = vkCreateImageView(device_, &image_view_create_info, nullptr, &view);

  if (err) {
    vmaDestroyImage(context_.GetAllocator(), vk_image, allocation);
    DLOG(0) << "vkCreateImageView failed with error " << string_VkResult(err);
    return false;
  }

  image = {vk_image, allocation};

  DescPool* desc_pool = AllocateDescriptorPool();

  VkDescriptorSetAllocateInfo descriptor_set_allocate_info{};
  descriptor_set_allocate_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptor_set_allocate_info.descriptorPool = std::get<0>(*desc_pool);
  descriptor_set_allocate_info.descriptorSetCount = 1;
  descriptor_set_allocate_info.pSetLayouts = &descriptor_set_layout_;

  VkDescriptorSet descriptor_set;
  err = vkAllocateDescriptorSets(device_, &descriptor_set_allocate_info,
                                 &descriptor_set);
  if (err) {
    --std::get<1>(*desc_pool);
    DLOG(0) << "Cannot allocate descriptor sets, error "
            << string_VkResult(err);
    return false;
  }

  desc_set = {descriptor_set, desc_pool};

  VkDescriptorImageInfo image_info{};
  image_info.sampler = sampler_;
  image_info.imageView = view;
  image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = descriptor_set;
  write.dstBinding = 0;
  write.dstArrayElement = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &image_info;

  vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

  return true;
}

void RendererVulkan::FreeImage(Buffer<VkImage> image,
                               VkImageView image_view,
                               DescSet desc_set,
                               VkFramebuffer frame_buffer) {
  frames_[current_frame_].images_to_destroy.emplace_back(
      std::move(image), image_view, frame_buffer);
  frames_[current_frame_].desc_sets_to_destroy.emplace_back(
      std::move(desc_set));
}

void RendererVulkan::UpdateImage(VkImage image,
                                 VkFormat format,
                                 const uint8_t* data,
                                 int width,
                                 int height) {
  auto [num_blocks_x, num_blocks_y] =
      GetNumBlocksForImageFormat(format, width, height);

  auto [block_size, block_height] = GetBlockSizeForImageFormat(format);

  size_t to_submit =
      static_cast<size_t>(num_blocks_x) * num_blocks_y * block_size;
  size_t submit_from = 0;
  uint32_t segment = num_blocks_x * block_size;
  uint32_t max_size = staging_buffer_size_ - (staging_buffer_size_ % segment);
  uint32_t region_offset_y = 0;
  uint32_t alignment = std::max(
      (VkDeviceSize)16,
      context_.GetDeviceProperties().limits.optimalBufferCopyOffsetAlignment);

  // A segment must fit in a single staging buffer.
  DCHECK(staging_buffer_size_ >= segment);

  while (to_submit > 0) {
    uint32_t write_offset;
    uint32_t write_amount;
    if (!AllocateStagingBuffer(std::min((uint32_t)to_submit, max_size), segment,
                               alignment, write_offset, write_amount))
      return;
    Buffer<VkBuffer> staging_buffer =
        staging_buffers_[current_staging_buffer_].buffer;

    // Copy to staging buffer.
    void* data_ptr =
        staging_buffers_[current_staging_buffer_].alloc_info.pMappedData;
    memcpy(((uint8_t*)data_ptr) + write_offset, (char*)data + submit_from,
           write_amount);

    uint32_t region_height = (write_amount / segment) * block_height;

    // Insert a command to copy to GPU buffer.
    VkBufferImageCopy buffer_image_copy{};
    buffer_image_copy.bufferOffset = write_offset;
    buffer_image_copy.bufferRowLength = 0;
    buffer_image_copy.bufferImageHeight = 0;
    buffer_image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    buffer_image_copy.imageSubresource.mipLevel = 0;
    buffer_image_copy.imageSubresource.baseArrayLayer = 0;
    buffer_image_copy.imageSubresource.layerCount = 1;
    buffer_image_copy.imageOffset.x = 0;
    buffer_image_copy.imageOffset.y = region_offset_y;
    buffer_image_copy.imageOffset.z = 0;
    buffer_image_copy.imageExtent.width = width;
    buffer_image_copy.imageExtent.height = region_height;
    buffer_image_copy.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(frames_[current_frame_].setup_command_buffer,
                           std::get<0>(staging_buffer), image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &buffer_image_copy);

    to_submit -= write_amount;
    submit_from += write_amount;
    region_offset_y += region_height;
  }
}

void RendererVulkan::UpdateImageSubRegion(VkImage image,
                                          VkFormat format,
                                          const uint8_t* data,
                                          int x_offset,
                                          int y_offset,
                                          int width,
                                          int height,
                                          int src_pitch) {
  auto [num_blocks_x, num_blocks_y] =
      GetNumBlocksForImageFormat(format, width, height);
  auto [block_size, block_height] = GetBlockSizeForImageFormat(format);

  uint32_t row_bytes = num_blocks_x * block_size;
  uint32_t total = row_bytes * num_blocks_y;
  uint32_t max_size = staging_buffer_size_ - (staging_buffer_size_ % row_bytes);
  int current_block_row = 0;
  uint32_t region_offset_y = 0;
  uint32_t alignment = std::max(
      (VkDeviceSize)16,
      context_.GetDeviceProperties().limits.optimalBufferCopyOffsetAlignment);

  DCHECK(staging_buffer_size_ >= row_bytes);

  while (total > 0) {
    uint32_t write_offset;
    uint32_t write_amount;
    if (!AllocateStagingBuffer(std::min(total, max_size), row_bytes, alignment,
                               write_offset, write_amount))
      return;
    Buffer<VkBuffer> staging_buffer =
        staging_buffers_[current_staging_buffer_].buffer;

    // Copy block rows from strided source to tightly packed staging buffer.
    uint8_t* dst = ((uint8_t*)staging_buffers_[current_staging_buffer_]
                        .alloc_info.pMappedData) +
                   write_offset;
    int num_rows = write_amount / row_bytes;
    for (int r = 0; r < num_rows; ++r)
      memcpy(dst + static_cast<size_t>(r) * row_bytes,
             data + static_cast<size_t>(current_block_row + r) * src_pitch,
             row_bytes);

    uint32_t region_height = num_rows * block_height;

    VkBufferImageCopy buffer_image_copy{};
    buffer_image_copy.bufferOffset = write_offset;
    buffer_image_copy.bufferRowLength = 0;
    buffer_image_copy.bufferImageHeight = 0;
    buffer_image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    buffer_image_copy.imageSubresource.mipLevel = 0;
    buffer_image_copy.imageSubresource.baseArrayLayer = 0;
    buffer_image_copy.imageSubresource.layerCount = 1;
    buffer_image_copy.imageOffset.x = x_offset;
    buffer_image_copy.imageOffset.y = y_offset + region_offset_y;
    buffer_image_copy.imageOffset.z = 0;
    buffer_image_copy.imageExtent.width = width;
    buffer_image_copy.imageExtent.height = region_height;
    buffer_image_copy.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(frames_[current_frame_].setup_command_buffer,
                           std::get<0>(staging_buffer), image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &buffer_image_copy);

    total -= write_amount;
    current_block_row += num_rows;
    region_offset_y += region_height;
  }
}

void RendererVulkan::ImageMemoryBarrier(VkCommandBuffer command_buffer,
                                        VkImage image,
                                        VkPipelineStageFlags src_stage_mask,
                                        VkPipelineStageFlags dst_stage_mask,
                                        VkAccessFlags src_access,
                                        VkAccessFlags dst_access,
                                        VkImageLayout old_layout,
                                        VkImageLayout new_layout) {
  VkImageMemoryBarrier image_mem_barrier{};
  image_mem_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  image_mem_barrier.srcAccessMask = src_access;
  image_mem_barrier.dstAccessMask = dst_access;
  image_mem_barrier.oldLayout = old_layout;
  image_mem_barrier.newLayout = new_layout;
  image_mem_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_mem_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_mem_barrier.image = image;
  image_mem_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  image_mem_barrier.subresourceRange.baseMipLevel = 0;
  image_mem_barrier.subresourceRange.levelCount = 1;
  image_mem_barrier.subresourceRange.baseArrayLayer = 0;
  image_mem_barrier.subresourceRange.layerCount = 1;

  vkCmdPipelineBarrier(command_buffer, src_stage_mask, dst_stage_mask, 0, 0,
                       nullptr, 0, nullptr, 1, &image_mem_barrier);
}

bool RendererVulkan::CreatePipelineLayout(
    ShaderVulkan& shader,
    const std::vector<uint8_t>& spirv_vertex,
    const std::vector<uint8_t>& spirv_fragment) {
  SpvReflectShaderModule module_vertex;
  SpvReflectResult result = spvReflectCreateShaderModule(
      spirv_vertex.size(), spirv_vertex.data(), &module_vertex);
  if (result != SPV_REFLECT_RESULT_SUCCESS) {
    DLOG(0) << "SPIR-V reflection failed to parse vertex shader.";
    return false;
  }

  SpvReflectShaderModule module_fragment;
  result = spvReflectCreateShaderModule(
      spirv_fragment.size(), spirv_fragment.data(), &module_fragment);
  if (result != SPV_REFLECT_RESULT_SUCCESS) {
    DLOG(0) << "SPIR-V reflection failed to parse fragment shader.";
    spvReflectDestroyShaderModule(&module_vertex);
    return false;
  }

  bool ret = false;

  // Parse descriptor bindings.
  do {
    uint32_t binding_count = 0;

    // Validate that the vertex shader has no descriptor binding.
    result = spvReflectEnumerateDescriptorBindings(&module_vertex,
                                                   &binding_count, nullptr);
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
      DLOG(0) << "SPIR-V reflection failed to enumerate fragment shader "
                 "descriptor bindings.";
      break;
    }
    if (binding_count > 0) {
      DLOG(0) << "SPIR-V reflection found " << binding_count
              << " descriptor bindings in vertex shader.";
      break;
    }

    // Validate that the fragment shader has max 1 desriptor binding.
    result = spvReflectEnumerateDescriptorBindings(&module_fragment,
                                                   &binding_count, nullptr);
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
      DLOG(0) << "SPIR-V reflection failed to enumerate fragment shader "
                 "descriptor bindings.";
      break;
    }

    DLOG(0) << __func__ << " binding_count: " << binding_count;

    if (binding_count > 0) {
      // Collect binding names and validate that only COMBINED_IMAGE_SAMPLER
      // desriptor type is used.
      std::vector<SpvReflectDescriptorBinding*> bindings;
      bindings.resize(binding_count);
      result = spvReflectEnumerateDescriptorBindings(
          &module_fragment, &binding_count, bindings.data());

      if (result != SPV_REFLECT_RESULT_SUCCESS) {
        DLOG(0) << "SPIR-V reflection failed to get descriptor bindings for "
                   "fragment shader.";
        break;
      }

      for (size_t i = 0; i < binding_count; ++i) {
        const SpvReflectDescriptorBinding& binding = *bindings[i];

        DLOG(0) << __func__ << " name: " << binding.name
                << " descriptor_type: " << binding.descriptor_type
                << " set: " << binding.set << " binding: " << binding.binding;

        if (binding.binding > 0) {
          DLOG(0) << "SPIR-V reflection found " << binding_count
                  << " bindings in vertex shader. Only one binding per set is "
                     "supported";
          break;
        }

        if (binding.descriptor_type !=
            SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
          DLOG(0) << "SPIR-V reflection found descriptor type "
                  << binding.descriptor_type
                  << " in fragment shader. Only COMBINED_IMAGE_SAMPLER type is "
                     "supported.";
          break;
        }

        shader.sampler_uniform_names.emplace_back(binding.name);
        shader.desc_set_count++;
      }
    }

    if (active_descriptor_sets_.size() < shader.desc_set_count)
      active_descriptor_sets_.resize(shader.desc_set_count);

    // Parse push constants.
    auto enumerate_pc = [&](SpvReflectShaderModule& module, uint32_t& pc_count,
                            std::vector<SpvReflectBlockVariable*>& pconstants,
                            EShLanguage stage) {
      result =
          spvReflectEnumeratePushConstantBlocks(&module, &pc_count, nullptr);
      if (result != SPV_REFLECT_RESULT_SUCCESS) {
        DLOG(0) << "SPIR-V reflection failed to enumerate push constats in "
                   "shader stage "
                << stage;
        return false;
      }

      if (pc_count) {
        if (pc_count > 1) {
          DLOG(0) << "SPIR-V reflection found " << pc_count
                  << " push constats blocks in shader stage " << stage
                  << ". Only one push constant block is supported.";
          return false;
        }

        pconstants.resize(pc_count);
        result = spvReflectEnumeratePushConstantBlocks(&module, &pc_count,
                                                       pconstants.data());
        if (result != SPV_REFLECT_RESULT_SUCCESS) {
          DLOG(0) << "SPIR-V reflection failed to obtaining push constants.";
          return false;
        }
      }

      return true;
    };

    uint32_t pc_count_vertex = 0;
    std::vector<SpvReflectBlockVariable*> pconstants_vertex;
    if (!enumerate_pc(module_vertex, pc_count_vertex, pconstants_vertex,
                      EShLangVertex))
      break;

    uint32_t pc_count_fragment = 0;
    std::vector<SpvReflectBlockVariable*> pconstants_fragment;
    if (!enumerate_pc(module_fragment, pc_count_fragment, pconstants_fragment,
                      EShLangVertex))
      break;

    if (pc_count_vertex != pc_count_fragment) {
      DLOG(0) << "SPIR-V reflection found different push constant blocks "
                 "across shader stages.";
      break;
    }

    if (pc_count_vertex) {
      DLOG(0) << __func__
              << " PushConstants size: " << pconstants_vertex[0]->size
              << " count: " << pconstants_vertex[0]->member_count;

      if (pconstants_vertex[0]->size != pconstants_fragment[0]->size) {
        DLOG(0) << "SPIR-V reflection found different push constant blocks "
                   "across shader stages.";
        break;
      }

      CHECK(pconstants_vertex[0]->size <=
            context_.GetDeviceProperties().limits.maxPushConstantsSize)
          << "Required push constants size is bigger than the maximum "
             "supported size by device.";
      shader.push_constants_size = pconstants_vertex[0]->size;
      shader.push_constants =
          std::make_unique<char[]>(shader.push_constants_size);
      memset(shader.push_constants.get(), 0, shader.push_constants_size);

      size_t offset = 0;
      for (uint32_t j = 0; j < pconstants_vertex[0]->member_count; j++) {
        DCHECK(std::find_if(
                   shader.variables.begin(), shader.variables.end(),
                   [hash = KR2Hash(pconstants_vertex[0]->members[j].name)](
                       auto& r) { return hash == std::get<0>(r); }) ==
               shader.variables.end())
            << "Hash collision";

        DLOG(0) << __func__
                << " name: " << pconstants_vertex[0]->members[j].name
                << " size: " << pconstants_vertex[0]->members[j].size
                << " padded_size: "
                << pconstants_vertex[0]->members[j].padded_size;

        shader.variables.emplace_back(
            KR2Hash(pconstants_vertex[0]->members[j].name),
            pconstants_vertex[0]->members[j].size, offset);
        offset += pconstants_vertex[0]->members[j].padded_size;
      }
    }

    // Use the same layout for all descriptor sets.
    std::vector<VkDescriptorSetLayout> desc_set_layouts(binding_count,
                                                        descriptor_set_layout_);

    VkPipelineLayoutCreateInfo pipeline_layout_create_info{};
    pipeline_layout_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (binding_count > 0) {
      pipeline_layout_create_info.setLayoutCount = binding_count;
      pipeline_layout_create_info.pSetLayouts = desc_set_layouts.data();
    } else {
      pipeline_layout_create_info.setLayoutCount = 0;
      pipeline_layout_create_info.pSetLayouts = nullptr;
    }

    VkPushConstantRange push_constant_range{};
    if (shader.push_constants_size) {
      push_constant_range.stageFlags =
          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      push_constant_range.offset = 0;
      push_constant_range.size =
          static_cast<uint32_t>(shader.push_constants_size);

      pipeline_layout_create_info.pushConstantRangeCount = 1;
      pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;
    } else {
      pipeline_layout_create_info.pushConstantRangeCount = 0;
      pipeline_layout_create_info.pPushConstantRanges = nullptr;
    }

    if (vkCreatePipelineLayout(device_, &pipeline_layout_create_info, nullptr,
                               &shader.pipeline_layout) != VK_SUCCESS) {
      DLOG(0) << "Failed to create pipeline layout!";
      break;
    }

    ret = true;
  } while (false);

  spvReflectDestroyShaderModule(&module_vertex);
  spvReflectDestroyShaderModule(&module_fragment);
  return ret;
}

void RendererVulkan::DrawListBegin(VkRenderPass render_pass) {
  VkRenderPassBeginInfo render_pass_begin{};
  render_pass_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_begin.renderPass = render_pass;
  render_pass_begin.framebuffer = context_.GetFramebuffer();
  render_pass_begin.renderArea.extent.width = context_.GetFramebufferWidth();
  render_pass_begin.renderArea.extent.height = context_.GetFramebufferHeight();
  render_pass_begin.renderArea.offset.x = 0;
  render_pass_begin.renderArea.offset.y = 0;

  VkClearValue clear_value{};
  clear_value.color = clear_color_;
  render_pass_begin.clearValueCount = 1;
  render_pass_begin.pClearValues = &clear_value;

  vkCmdBeginRenderPass(frames_[current_frame_].draw_command_buffer,
                       &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

  ResetViewport();
  ResetScissor();
}

void RendererVulkan::DrawListEnd() {
  DCHECK(!rendering_offscreen_);

  vkCmdEndRenderPass(frames_[current_frame_].draw_command_buffer);

  // Prepare the Swapchain Image for display.
  ImageMemoryBarrier(frames_[current_frame_].draw_command_buffer,
                     context_.GetSwapchainImage(),
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

#ifdef FORCE_FULL_BARRIER
  FullBarrier(true);
#endif
}

void RendererVulkan::SwapBuffers() {
  // Ensure all tasks in the background thread are complete.
  task_runner_.WaitForCompletion();

  vkEndCommandBuffer(frames_[current_frame_].setup_command_buffer);
  vkEndCommandBuffer(frames_[current_frame_].draw_command_buffer);

  context_.SwapBuffers();
  current_frame_ = (current_frame_ + 1) % frames_.size();

  active_shader_id_ = kInvalidId;
  for (auto& ds : active_descriptor_sets_)
    ds = VK_NULL_HANDLE;

  BeginFrame();
}

void RendererVulkan::SetupThreadMain() {
  for (;;) {
    semaphore_.acquire();
    if (quit_.load(std::memory_order_relaxed))
      break;

    task_runner_.RunTasks<Consumer::Single>();
  }
}

template <typename T>
bool RendererVulkan::SetUniformInternal(ShaderVulkan& shader,
                                        const std::string& name,
                                        T val) {
  auto hash = KR2Hash(name);
  auto it = std::find_if(shader.variables.begin(), shader.variables.end(),
                         [&](auto& r) { return hash == std::get<0>(r); });
  if (it == shader.variables.end()) {
    DLOG(0) << "No variable found with name " << name;
    return false;
  }

  if (std::get<1>(*it) != sizeof(val)) {
    DLOG(0) << "Size mismatch for variable " << name;
    return false;
  }

  auto* dst =
      reinterpret_cast<T*>(shader.push_constants.get() + std::get<2>(*it));
  *dst = val;
  shader.push_constants_dirty = true;
  return true;
}

bool RendererVulkan::IsFormatSupported(VkFormat format) {
  VkFormatProperties properties;
  vkGetPhysicalDeviceFormatProperties(context_.GetPhysicalDevice(), format,
                                      &properties);
  return properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
}

std::vector<std::string> RendererVulkan::GetAvailableGpus() const {
  return context_.GetDeviceNames();
}

int RendererVulkan::GetSelectedGpuIndex() const {
  return context_.GetSelectedDeviceIndex();
}

void RendererVulkan::SetPreferredGpu(const std::string& name) {
  context_.SetPreferredDevice(name);
}

void RendererVulkan::DestroyAllResources() {
  std::vector<ResourceId> resource_ids;
  resource_ids.reserve(geometries_.size());
  for (auto& r : geometries_)
    resource_ids.push_back(r.first);
  for (auto& r : resource_ids)
    DestroyGeometry(r);

  resource_ids.clear();
  for (auto& r : shaders_)
    resource_ids.push_back(r.first);
  for (auto& r : resource_ids)
    DestroyShader(r);

  resource_ids.clear();
  for (auto& r : textures_)
    resource_ids.push_back(r.first);
  for (auto& r : resource_ids)
    DestroyTexture(r);

  DCHECK(geometries_.size() == 0);
  DCHECK(shaders_.size() == 0);
  DCHECK(textures_.size() == 0);
}

}  // namespace eng
