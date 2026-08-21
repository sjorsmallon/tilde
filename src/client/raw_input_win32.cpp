#include "raw_input_win32.hpp"

#include "../shared/log.hpp"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace client::raw_input
{

namespace
{

constexpr size_t RING_CAPACITY = 8192; // power of two, so the wrap is a mask
constexpr size_t RING_MASK     = RING_CAPACITY - 1;

raw_input_event_t     g_ring[RING_CAPACITY];
std::atomic<uint64_t> g_write_index{0};
std::atomic<uint64_t> g_read_index{0};
std::atomic<uint64_t> g_dropped_count{0};

std::thread           g_thread;
std::atomic<uint32_t> g_thread_id{0};
std::atomic<bool>     g_running{false};

// Single producer (the input thread), single consumer (the game thread). The
// release on the write index publishes the slot contents along with it.
void push_event(const raw_input_event_t& event)
{
  const uint64_t write_index = g_write_index.load(std::memory_order_relaxed);
  const uint64_t read_index  = g_read_index.load(std::memory_order_acquire);

  if (write_index - read_index >= RING_CAPACITY)
  {
    g_dropped_count.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  g_ring[write_index & RING_MASK] = event;
  g_write_index.store(write_index + 1, std::memory_order_release);
}

LRESULT CALLBACK raw_input_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
  return DefWindowProcW(window, message, wparam, lparam);
}

struct button_mapping_t
{
  USHORT   down_flag;
  USHORT   up_flag;
  uint16_t code;
};

constexpr button_mapping_t BUTTON_MAPPINGS[] = {
    {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, 0},
    {RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, 1},
    {RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, 2},
    {RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, 3},
    {RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, 4},
};

void handle_raw_input(HRAWINPUT raw_input_handle, uint64_t arrival_qpc_ticks)
{
  alignas(8) uint8_t buffer[sizeof(RAWINPUT) + 64];
  UINT               size = sizeof(buffer);

  if (GetRawInputData(raw_input_handle, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) ==
      (UINT)-1)
    return;

  const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(buffer);

  if (raw->header.dwType == RIM_TYPEKEYBOARD)
  {
    raw_input_event_t event{};
    event.arrival_qpc_ticks = arrival_qpc_ticks;
    event.kind              = raw_input_kind_t::Key;
    event.code              = raw->data.keyboard.VKey;
    event.down              = (raw->data.keyboard.Flags & RI_KEY_BREAK) == 0;
    push_event(event);
    return;
  }

  if (raw->header.dwType != RIM_TYPEMOUSE)
    return;

  const USHORT button_flags = raw->data.mouse.usButtonFlags;

  // MOTION FIRST, and it is not an `else`. One raw report can carry travel and
  // a button transition together, and the travel happened BEFORE the click
  // within that report -- pushing it first is what puts the aim under the
  // crosshair the shot is taken through. This used to return early on any
  // report that carried a button, which silently dropped the last scrap of
  // movement before every single click.
  // MOUSE_MOVE_ABSOLUTE makes lLastX/Y screen coordinates rather than travel --
  // a tablet, a touchscreen or an RDP session. There is no delta to be had from
  // one report, so it is dropped here and the aim falls back to SDL's own
  // relative motion for that frame. Feeding an absolute coordinate in as a
  // delta would fling the view across the map.
  const bool motion_is_relative =
      (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0;

  if (motion_is_relative && (raw->data.mouse.lLastX != 0 || raw->data.mouse.lLastY != 0))
  {
    raw_input_event_t event{};
    event.arrival_qpc_ticks = arrival_qpc_ticks;
    event.kind              = raw_input_kind_t::Mouse_Motion;
    event.delta_x           = raw->data.mouse.lLastX;
    event.delta_y           = raw->data.mouse.lLastY;
    push_event(event);
  }

  // One raw report can carry a press and a release of different buttons, so
  // these are tested independently rather than switched.
  for (const button_mapping_t& mapping : BUTTON_MAPPINGS)
  {
    if ((button_flags & (mapping.down_flag | mapping.up_flag)) == 0)
      continue;

    raw_input_event_t event{};
    event.arrival_qpc_ticks = arrival_qpc_ticks;
    event.kind              = raw_input_kind_t::Mouse_Button;
    event.code              = mapping.code;
    event.down              = (button_flags & mapping.down_flag) != 0;
    push_event(event);
  }
}

void input_thread_main(std::promise<bool> ready)
{
  g_thread_id.store(GetCurrentThreadId(), std::memory_order_release);

  const HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSEXW window_class{};
  window_class.cbSize        = sizeof(window_class);
  window_class.lpfnWndProc   = raw_input_window_proc;
  window_class.hInstance     = instance;
  window_class.lpszClassName = L"TildeRawInputSink";
  RegisterClassExW(&window_class);

  // HWND_MESSAGE: no pixels, no z-order, no input focus. It exists only to own a
  // message queue this thread can block on.
  const HWND window = CreateWindowExW(0, window_class.lpszClassName, nullptr, 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, instance, nullptr);
  if (window == nullptr)
  {
    log_error("raw_input: CreateWindowExW for the message-only sink failed ({})", GetLastError());
    ready.set_value(false);
    return;
  }

  // RIDEV_INPUTSINK: delivered even while the game window is not foreground, so
  // a transition during an alt-tab is stamped rather than silently missing.
  RAWINPUTDEVICE devices[2]{};
  devices[0].usUsagePage = 0x01;
  devices[0].usUsage     = 0x02; // mouse
  devices[0].dwFlags     = RIDEV_INPUTSINK;
  devices[0].hwndTarget  = window;
  devices[1].usUsagePage = 0x01;
  devices[1].usUsage     = 0x06; // keyboard
  devices[1].dwFlags     = RIDEV_INPUTSINK;
  devices[1].hwndTarget  = window;

  if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE)))
  {
    log_error("raw_input: RegisterRawInputDevices failed ({})", GetLastError());
    DestroyWindow(window);
    ready.set_value(false);
    return;
  }

  ready.set_value(true);

  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0)
  {
    // The clock read is the FIRST thing after the wake, before the message is
    // even inspected: everything below it would be latency measured instead of
    // the input.
    LARGE_INTEGER arrival{};
    QueryPerformanceCounter(&arrival);

    if (message.message == WM_INPUT)
      handle_raw_input(reinterpret_cast<HRAWINPUT>(message.lParam),
                       static_cast<uint64_t>(arrival.QuadPart));

    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  DestroyWindow(window);
}

} // namespace

std::optional<uint64_t> try_start()
{
  if (g_running.load(std::memory_order_acquire))
    return std::nullopt;

  std::promise<bool> ready;
  std::future<bool>  ready_future = ready.get_future();

  g_thread = std::thread(input_thread_main, std::move(ready));

  if (ready_future.wait_for(std::chrono::seconds(2)) != std::future_status::ready ||
      !ready_future.get())
  {
    if (g_thread.joinable())
      g_thread.join();
    return std::nullopt;
  }

  g_running.store(true, std::memory_order_release);

  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  log_terminal("[raw-input] thread up, QPC frequency {} Hz",
               static_cast<uint64_t>(frequency.QuadPart));
  return static_cast<uint64_t>(frequency.QuadPart);
}

void stop()
{
  if (!g_running.load(std::memory_order_acquire))
    return;

  PostThreadMessageW(g_thread_id.load(std::memory_order_acquire), WM_QUIT, 0, 0);
  if (g_thread.joinable())
    g_thread.join();
  g_running.store(false, std::memory_order_release);
}

size_t drain(Span<raw_input_event_t> out)
{
  const uint64_t write_index = g_write_index.load(std::memory_order_acquire);
  uint64_t       read_index  = g_read_index.load(std::memory_order_relaxed);

  size_t written = 0;
  while (read_index != write_index && written < out.size())
  {
    out[written] = g_ring[read_index & RING_MASK];
    ++read_index;
    ++written;
  }

  g_read_index.store(read_index, std::memory_order_release);
  return written;
}

uint64_t dropped_event_count()
{
  return g_dropped_count.load(std::memory_order_relaxed);
}

} // namespace client::raw_input

#else // !_WIN32

namespace client::raw_input
{
std::optional<uint64_t> try_start() { return std::nullopt; }
void                    stop() {}
size_t                  drain(Span<raw_input_event_t>) { return 0; }
uint64_t                dropped_event_count() { return 0; }
} // namespace client::raw_input

#endif
