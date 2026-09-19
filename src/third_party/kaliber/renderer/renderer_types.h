#ifndef ENGINE_RENDERER_RENDERER_TYPES_H
#define ENGINE_RENDERER_RENDERER_TYPES_H

#include <string>
#include <tuple>
#include <vector>

namespace eng {

enum class ImageFormat { kRGBA32, kDXT1, kDXT5, kETC1, kATC, kATCIA };

enum Primitive {
  kPrimitive_Invalid = -1,
  kPrimitive_Triangles,
  kPrimitive_TriangleStrip,
  kPrimitive_Max
};

enum AttribType {
  kAttribType_Invalid = -1,
  kAttribType_Color,
  kAttribType_Normal,
  kAttribType_Position,
  kAttribType_TexCoord,
  kAttribType_Max
};

enum DataType {
  kDataType_Invalid = -1,
  kDataType_Byte,
  kDataType_Float,
  kDataType_Int,
  kDataType_Short,
  kDataType_UInt,
  kDataType_UShort,
  kDataType_Max
};

// Make tuple elements verbose.
using ElementCount = size_t;
using DataTypeSize = size_t;

using VertexDescription =
    std::vector<std::tuple<AttribType, DataType, ElementCount, DataTypeSize>>;

const char* ImageFormatToString(ImageFormat format);

bool IsCompressedFormat(ImageFormat format);

size_t GetImageSize(int width, int height, ImageFormat format);

size_t GetVertexSize(const VertexDescription& vertex_description);

size_t GetIndexSize(DataType index_description);

bool ParseVertexDescription(const std::string& vd_str, VertexDescription& out);

}  // namespace eng

#endif  // ENGINE_RENDERER_RENDERER_TYPES_H
