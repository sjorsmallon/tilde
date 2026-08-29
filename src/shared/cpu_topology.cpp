#include "cpu_topology.hpp"

#include "log.hpp"

#include <bit>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace cpu_topology
{

#ifdef _WIN32
namespace
{

// The relationship buffer is a run of VARIABLY SIZED records, so it is walked by
// each record's own Size rather than indexed.
bool for_each_core(void (*visit)(const PROCESSOR_RELATIONSHIP&, void*), void* user_data)
{
  DWORD length = 0;
  if (GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length) ||
      GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return false;

  uint8_t* buffer = static_cast<uint8_t*>(std::malloc(length));
  if (buffer == nullptr)
    return false;

  if (!GetLogicalProcessorInformationEx(
          RelationProcessorCore,
          reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer), &length))
  {
    std::free(buffer);
    return false;
  }

  for (DWORD offset = 0; offset < length;)
  {
    const auto* record =
        reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer + offset);
    if (record->Size == 0)
      break;
    if (record->Relationship == RelationProcessorCore)
      visit(record->Processor, user_data);
    offset += record->Size;
  }

  std::free(buffer);
  return true;
}

struct scan_t
{
  uint16_t group                = 0;
  uint8_t  highest_class        = 0;
  bool     saw_any              = false;
  bool     saw_lower_class      = false;
  uint64_t performance_mask     = 0;
  uint32_t performance_logical  = 0;
  uint32_t efficiency_logical   = 0;
  uint32_t total_logical        = 0;
};

// Two passes over the same records: the first finds the highest class present,
// the second sums the masks. One pass cannot do it -- the highest class is not
// known until every record has been seen, and a core in the highest class may
// appear first.
void find_highest_class(const PROCESSOR_RELATIONSHIP& core, void* user_data)
{
  scan_t& scan = *static_cast<scan_t*>(user_data);
  for (WORD index = 0; index < core.GroupCount; ++index)
  {
    if (core.GroupMask[index].Group != scan.group || core.GroupMask[index].Mask == 0)
      continue;
    if (!scan.saw_any || core.EfficiencyClass > scan.highest_class)
      scan.highest_class = core.EfficiencyClass;
    scan.saw_any = true;
  }
}

void accumulate(const PROCESSOR_RELATIONSHIP& core, void* user_data)
{
  scan_t& scan = *static_cast<scan_t*>(user_data);
  for (WORD index = 0; index < core.GroupCount; ++index)
  {
    const GROUP_AFFINITY& affinity = core.GroupMask[index];
    if (affinity.Group != scan.group || affinity.Mask == 0)
      continue;

    const uint32_t logical =
        static_cast<uint32_t>(std::popcount(static_cast<uint64_t>(affinity.Mask)));
    scan.total_logical += logical;

    if (core.EfficiencyClass == scan.highest_class)
    {
      scan.performance_mask |= static_cast<uint64_t>(affinity.Mask);
      scan.performance_logical += logical;
    }
    else
    {
      scan.saw_lower_class = true;
      scan.efficiency_logical += logical;
    }
  }
}

bool scan_current_group(scan_t& scan)
{
  GROUP_AFFINITY thread_affinity{};
  if (GetThreadGroupAffinity(GetCurrentThread(), &thread_affinity))
    scan.group = thread_affinity.Group;

  if (!for_each_core(find_highest_class, &scan) || !scan.saw_any)
    return false;
  return for_each_core(accumulate, &scan);
}

} // namespace
#endif

topology_t query()
{
  topology_t topology;

#ifdef _WIN32
  scan_t scan;
  if (!scan_current_group(scan))
    return topology;

  topology.logical_processor_count = scan.total_logical;
  topology.performance_core_count  = scan.performance_logical;
  topology.efficiency_core_count   = scan.efficiency_logical;
  topology.is_hybrid               = scan.saw_lower_class;
  topology.performance_core_mask   = scan.performance_mask;
#endif

  return topology;
}

uint64_t current_thread_affinity_mask()
{
#ifdef _WIN32
  GROUP_AFFINITY affinity{};
  if (!GetThreadGroupAffinity(GetCurrentThread(), &affinity))
    return 0;
  return static_cast<uint64_t>(affinity.Mask);
#else
  return 0;
#endif
}

bool try_pin_current_thread_to_performance_cores()
{
#ifdef _WIN32
  const topology_t topology = query();

  if (!topology.is_hybrid || topology.performance_core_mask == 0)
  {
    log_terminal("cpu_topology: {} logical processors, not hybrid — main thread left unpinned",
                 topology.logical_processor_count);
    return false;
  }

  if (SetThreadAffinityMask(GetCurrentThread(),
                            static_cast<DWORD_PTR>(topology.performance_core_mask)) == 0)
  {
    log_error("cpu_topology: SetThreadAffinityMask(0x{:x}) failed with {}",
              topology.performance_core_mask, GetLastError());
    return false;
  }

  log_terminal("cpu_topology: hybrid CPU ({} performance + {} efficiency logical processors) — "
               "main thread pinned to mask 0x{:x}",
               topology.performance_core_count, topology.efficiency_core_count,
               topology.performance_core_mask);
  return true;
#else
  return false;
#endif
}

} // namespace cpu_topology
