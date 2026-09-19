#include "third_party/kaliber/renderer/renderer_types.h"

#include <cstring>

#include "third_party/kaliber/base/log.h"

namespace {

// Used to parse the vertex layout,
// e.g. "p3f;c4b" for "position 3 floats, color 4 bytes".
const char kLayoutDelimiter[] = ";/ \t";

}  // namespace

namespace eng {

const char* ImageFormatToString(ImageFormat format) {
  switch (format) {
    case ImageFormat::kRGBA32:
      return "RGBA32";
    case ImageFormat::kDXT1:
      return "DXT1";
    case ImageFormat::kDXT5:
      return "DXT5";
    case ImageFormat::kETC1:
      return "ETC1";
    case ImageFormat::kATC:
      return "ATC";
    case ImageFormat::kATCIA:
      return "ATCIA";
    default:
      NOTREACHED() << "Unknown image format: " << static_cast<int>(format);
      return nullptr;
  }
}

bool IsCompressedFormat(ImageFormat format) {
  switch (format) {
    case ImageFormat::kRGBA32:
      return false;
    case ImageFormat::kDXT1:
    case ImageFormat::kDXT5:
    case ImageFormat::kETC1:
    case ImageFormat::kATC:
    case ImageFormat::kATCIA:
      return true;
    default:
      NOTREACHED() << "Unknown image format: " << static_cast<int>(format);
      return false;
  }
}

size_t GetImageSize(int width, int height, ImageFormat format) {
  switch (format) {
    case ImageFormat::kRGBA32:
      return static_cast<size_t>(width) * height * 4;
    case ImageFormat::kDXT1:
    case ImageFormat::kATC:
      return static_cast<size_t>((width + 3) / 4) * ((height + 3) / 4) * 8;
    case ImageFormat::kDXT5:
    case ImageFormat::kATCIA:
      return static_cast<size_t>((width + 3) / 4) * ((height + 3) / 4) * 16;
    case ImageFormat::kETC1:
      return static_cast<size_t>(width) * height * 4 / 8;
    default:
      NOTREACHED() << "Unknown image format: " << static_cast<int>(format);
      return 0;
  }
}

size_t GetVertexSize(const VertexDescription& vertex_description) {
  size_t size = 0;
  for (auto& attr : vertex_description) {
    size += std::get<2>(attr) * std::get<3>(attr);
  }
  return size;
}

size_t GetIndexSize(DataType index_description) {
  switch (index_description) {
    case kDataType_Byte:
      return sizeof(char);
    case kDataType_UShort:
      return sizeof(unsigned short);
    case kDataType_UInt:
      return sizeof(unsigned int);
    default:
      return 0;
  }
}

bool ParseVertexDescription(const std::string& vd_str, VertexDescription& out) {
  // Parse the description.
  char buffer[32];
  if (vd_str.size() >= sizeof(buffer))
    return false;
  memcpy(buffer, vd_str.c_str(), vd_str.size() + 1);
  char* token = strtok(buffer, kLayoutDelimiter);

  // Parse each encountered attribute.
  while (token) {
    // Check for invalid format.
    if (strlen(token) != 3) {
      DLOG(0) << "Invalid format: " << token;
      return false;
    }

    AttribType attrib_type = kAttribType_Invalid;
    ;
    switch (token[0]) {
      case 'c':
        attrib_type = kAttribType_Color;
        break;
      case 'n':
        attrib_type = kAttribType_Normal;
        break;
      case 'p':
        attrib_type = kAttribType_Position;
        break;
      case 't':
        attrib_type = kAttribType_TexCoord;
        break;
      default:
        DLOG(0) << "Unknown attribute: " << token;
        return false;
    }

    // There can be between 1 and 4 elements in an attribute.
    ElementCount num_elements = token[1] - '1' + 1;
    if (num_elements < 1 || num_elements > 4) {
      DLOG(0) << "Invalid number of elements: " << token;
      return false;
    }

    // The data type is needed, the most common ones are supported.
    DataType data_type = kDataType_Invalid;
    DataTypeSize type_size = 0;
    switch (token[2]) {
      case 'b':
        data_type = kDataType_Byte;
        type_size = sizeof(unsigned char);
        break;
      case 'f':
        data_type = kDataType_Float;
        type_size = sizeof(float);
        break;
      case 'i':
        data_type = kDataType_Int;
        type_size = sizeof(int);
        break;
      case 's':
        data_type = kDataType_Short;
        type_size = sizeof(short);
        break;
      case 'u':
        data_type = kDataType_UInt;
        type_size = sizeof(unsigned int);
        break;
      case 'w':
        data_type = kDataType_UShort;
        type_size = sizeof(unsigned short);
        break;
      default:
        DLOG(0) << "Unknown data type: " << token;
        return false;
    }

    out.emplace_back(attrib_type, data_type, num_elements, type_size);

    token = strtok(NULL, kLayoutDelimiter);
  }
  return true;
}

}  // namespace eng
