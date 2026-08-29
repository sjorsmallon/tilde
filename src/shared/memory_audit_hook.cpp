// The global operator new / delete replacement. THE ONLY FILE THAT DOES THIS.
//
// It is compiled DIRECTLY INTO EACH MODULE -- game_client, game_server, and
// every executable -- rather than into game_shared, and both halves of that
// matter:
//
//   NOT game_shared, because a replacement sitting in a static ARCHIVE is only
//   pulled into the link if something already references a symbol in its object
//   file. Nothing does; operator new is resolved by the CRT long before the
//   archive is searched. That is the same linker-drop that silently emptied the
//   static-init registries this codebase used to have, and it fails the same
//   way: no error, no hook, an audit reporting zero.
//
//   EVERY module, because operator-new replacement on Windows is per module.
//   game_client.dll and game_server.dll each resolve their own calls; a hook in
//   the exe alone would see the launcher and nothing else.
//
// All modules point at the ONE state the launcher owns (see memory_audit.hpp),
// so a block allocated in the client and freed in the server is an insert and a
// matching erase in the same table rather than a miss.
//
// Aligned allocations must go through _aligned_malloc / _aligned_free as a
// PAIR. The language guarantees an aligned new is matched by an aligned delete,
// which is what makes routing them to a different pair of primitives safe.

#if TILDE_MEMORY_AUDIT

#include "memory_audit.hpp"

#include <cstdlib>
#include <new>

namespace
{

void* allocate_bytes(size_t bytes)
{
  return std::malloc(bytes == 0 ? 1 : bytes);
}

void* allocate_aligned_bytes(size_t bytes, size_t alignment)
{
  if (bytes == 0)
    bytes = 1;
#ifdef _WIN32
  return _aligned_malloc(bytes, alignment);
#else
  if (bytes % alignment != 0)
    bytes += alignment - (bytes % alignment);
  return std::aligned_alloc(alignment, bytes);
#endif
}

void free_aligned_bytes(void* pointer)
{
#ifdef _WIN32
  _aligned_free(pointer);
#else
  std::free(pointer);
#endif
}

// The standard's retry loop: on failure, run the installed new_handler and try
// again; throw only once there is no handler left to run.
template <typename Allocate_Fn> void* allocate_or_throw(Allocate_Fn allocate)
{
  for (;;)
  {
    void* block = allocate();
    if (block != nullptr)
      return block;

    std::new_handler handler = std::get_new_handler();
    if (handler == nullptr)
      throw std::bad_alloc();
    handler();
  }
}

} // namespace

void* operator new(size_t bytes)
{
  void* block = allocate_or_throw([bytes] { return allocate_bytes(bytes); });
  memory_audit::on_allocation(block, bytes);
  return block;
}

void* operator new[](size_t bytes)
{
  void* block = allocate_or_throw([bytes] { return allocate_bytes(bytes); });
  memory_audit::on_allocation(block, bytes);
  return block;
}

void* operator new(size_t bytes, const std::nothrow_t&) noexcept
{
  void* block = allocate_bytes(bytes);
  memory_audit::on_allocation(block, bytes);
  return block;
}

void* operator new[](size_t bytes, const std::nothrow_t&) noexcept
{
  void* block = allocate_bytes(bytes);
  memory_audit::on_allocation(block, bytes);
  return block;
}

void* operator new(size_t bytes, std::align_val_t alignment)
{
  void* block = allocate_or_throw(
      [bytes, alignment] { return allocate_aligned_bytes(bytes, static_cast<size_t>(alignment)); });
  memory_audit::on_allocation(block, bytes);
  return block;
}

void* operator new[](size_t bytes, std::align_val_t alignment)
{
  void* block = allocate_or_throw(
      [bytes, alignment] { return allocate_aligned_bytes(bytes, static_cast<size_t>(alignment)); });
  memory_audit::on_allocation(block, bytes);
  return block;
}

void* operator new(size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
  void* block = allocate_aligned_bytes(bytes, static_cast<size_t>(alignment));
  memory_audit::on_allocation(block, bytes);
  return block;
}

void* operator new[](size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
  void* block = allocate_aligned_bytes(bytes, static_cast<size_t>(alignment));
  memory_audit::on_allocation(block, bytes);
  return block;
}

void operator delete(void* pointer) noexcept
{
  memory_audit::on_free(pointer);
  std::free(pointer);
}

void operator delete[](void* pointer) noexcept
{
  memory_audit::on_free(pointer);
  std::free(pointer);
}

void operator delete(void* pointer, size_t) noexcept
{
  memory_audit::on_free(pointer);
  std::free(pointer);
}

void operator delete[](void* pointer, size_t) noexcept
{
  memory_audit::on_free(pointer);
  std::free(pointer);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept
{
  memory_audit::on_free(pointer);
  std::free(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept
{
  memory_audit::on_free(pointer);
  std::free(pointer);
}

void operator delete(void* pointer, std::align_val_t) noexcept
{
  memory_audit::on_free(pointer);
  free_aligned_bytes(pointer);
}

void operator delete[](void* pointer, std::align_val_t) noexcept
{
  memory_audit::on_free(pointer);
  free_aligned_bytes(pointer);
}

void operator delete(void* pointer, size_t, std::align_val_t) noexcept
{
  memory_audit::on_free(pointer);
  free_aligned_bytes(pointer);
}

void operator delete[](void* pointer, size_t, std::align_val_t) noexcept
{
  memory_audit::on_free(pointer);
  free_aligned_bytes(pointer);
}

void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
  memory_audit::on_free(pointer);
  free_aligned_bytes(pointer);
}

void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
  memory_audit::on_free(pointer);
  free_aligned_bytes(pointer);
}

#endif // TILDE_MEMORY_AUDIT
