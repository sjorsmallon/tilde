#pragma once

// Which cores are the fast ones, and putting the main thread on them.
//
// WHY THIS EXISTS. A hybrid CPU (Intel 12th gen and later; this machine is a
// 13900K with 8 P-cores and 16 E-cores) has two different microarchitectures
// under one socket. If the scheduler moves the main thread from a P-core to an
// E-core mid-session, that thread loses roughly 40% of its throughput
// instantly. It is a genuine hitch, it is invisible to an allocation profiler
// AND to a cache profiler, and it is why games pin their main thread.
//
// The launcher's framerate cap SLEEPS every frame, which makes this worse
// rather than better: a thread that sleeps regularly looks idle to Intel Thread
// Director, and idle threads are exactly what it demotes to E-cores.
//
// WHAT "PERFORMANCE CORE" MEANS HERE. Windows reports an `EfficiencyClass` per
// core, where a HIGHER value means greater performance and lower efficiency --
// the name reads backwards. So the P-cores are the ones in the highest class
// present, and everything else is an E-core. Nothing here hardcodes a vendor, a
// core count or a model number; a non-hybrid machine reports one class and
// try_pin_current_thread_to_performance_cores() correctly does nothing.
//
// It pins to the SET of performance cores, never to one core. Pinning a thread
// to a single core means anything else scheduled there stalls it, which trades
// a rare hitch for a common one.
//
// Only the MAIN thread wants this. The task system's workers should be free to
// use E-cores -- that is what they are for -- and the raw-input thread spends
// its life blocked in GetMessageW.

#include <cstdint>

namespace cpu_topology
{

struct topology_t
{
  uint32_t logical_processor_count = 0;
  // Logical processors in the highest efficiency class, and in every class
  // below it. On a non-hybrid machine every core is a "performance" core and
  // efficiency_core_count is 0.
  uint32_t performance_core_count = 0;
  uint32_t efficiency_core_count  = 0;
  bool     is_hybrid              = false;
  // Affinity mask of the performance cores, within the CALLING THREAD's
  // processor group. Zero if the query failed.
  uint64_t performance_core_mask = 0;
};

topology_t query();

// The calling thread's current affinity mask, within its processor group. 0 if
// it could not be read. Windows has no plain "get thread affinity" call -- the
// setter returns the PREVIOUS mask, which is useless for asking without also
// changing it -- so this goes through GetThreadGroupAffinity. It exists so the
// pin can be VERIFIED rather than assumed from a non-zero return code.
uint64_t current_thread_affinity_mask();

// Restricts the calling thread to the performance cores. Returns false when
// there is nothing to do (not hybrid, query failed, or the platform has no such
// notion) and leaves affinity untouched -- a false is not an error.
bool try_pin_current_thread_to_performance_cores();

} // namespace cpu_topology
