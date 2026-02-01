#include <cstdint>
#include <google/protobuf/message.h>
#include <vector>

template <typename T>
bool message_to_vector(const T &message, std::vector<uint8_t> &buffer)
{
  size_t size = message.ByteSizeLong();
  buffer.resize(size);
  return message.SerializeToArray(buffer.data(), static_cast<int>(size));
}