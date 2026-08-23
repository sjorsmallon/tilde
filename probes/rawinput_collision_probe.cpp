// Build and run (not wired into CMake on purpose -- it is a diagnostic, not a
// target):
//
//   clang++ -std=gnu++23 probes/rawinput_collision_probe.cpp -o rawinput_probe.exe -luser32
//   ./rawinput_probe.exe
//
// See raw_input_plan.md, "Why SDL must never own relative mouse mode here".
//
// Does a second RegisterRawInputDevices for the SAME usage (what SDL's
// ToggleRawInput does when relative mouse mode turns on) steal the raw input
// away from an earlier RIDEV_INPUTSINK registration on a message-only window
// (what raw_input_win32.cpp does)?
//
// Reproduces both registrations exactly as the two sources spell them, then
// injects motion/keys with SendInput and counts what the message-only window
// actually receives.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

static int g_mouse_reports    = 0;
static int g_keyboard_reports = 0;

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
  if (message == WM_INPUT)
  {
    UINT size = 0;
    GetRawInputData((HRAWINPUT)l, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    BYTE buffer[1024];
    if (size <= sizeof(buffer) &&
        GetRawInputData((HRAWINPUT)l, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) == size)
    {
      const RAWINPUT* raw = (const RAWINPUT*)buffer;
      if (raw->header.dwType == RIM_TYPEMOUSE)
        ++g_mouse_reports;
      else if (raw->header.dwType == RIM_TYPEKEYBOARD)
        ++g_keyboard_reports;
    }
    return 0;
  }
  return DefWindowProcW(hwnd, message, w, l);
}

static void pump()
{
  MSG message;
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
  {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

static void inject_mouse_and_key()
{
  INPUT inputs[3] = {};
  inputs[0].type           = INPUT_MOUSE;
  inputs[0].mi.dx          = 7;
  inputs[0].mi.dy          = 3;
  inputs[0].mi.dwFlags     = MOUSEEVENTF_MOVE;
  inputs[1].type           = INPUT_KEYBOARD;
  inputs[1].ki.wVk         = VK_F24; // harmless, unlikely to be bound
  inputs[2].type           = INPUT_KEYBOARD;
  inputs[2].ki.wVk         = VK_F24;
  inputs[2].ki.dwFlags     = KEYEVENTF_KEYUP;
  SendInput(3, inputs, sizeof(INPUT));

  // Raw input is delivered asynchronously; give it a moment and pump.
  for (int i = 0; i < 40; ++i)
  {
    pump();
    Sleep(5);
  }
  pump();
}

int main()
{
  WNDCLASSEXW window_class = {};
  window_class.cbSize        = sizeof(window_class);
  window_class.lpfnWndProc   = wnd_proc;
  window_class.hInstance     = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"probe_raw_input_sink";
  RegisterClassExW(&window_class);

  HWND sink = CreateWindowExW(0, window_class.lpszClassName, L"probe", 0, 0, 0, 0, 0,
                              HWND_MESSAGE, nullptr, window_class.hInstance, nullptr);
  if (!sink)
  {
    printf("FAILED to create message-only window (%lu)\n", GetLastError());
    return 1;
  }

  // --- Registration 1: exactly raw_input_win32.cpp ---
  RAWINPUTDEVICE ours[2] = {};
  ours[0].usUsagePage = 0x01;
  ours[0].usUsage     = 0x02; // mouse
  ours[0].dwFlags     = RIDEV_INPUTSINK;
  ours[0].hwndTarget  = sink;
  ours[1].usUsagePage = 0x01;
  ours[1].usUsage     = 0x06; // keyboard
  ours[1].dwFlags     = RIDEV_INPUTSINK;
  ours[1].hwndTarget  = sink;
  if (!RegisterRawInputDevices(ours, 2, sizeof(RAWINPUTDEVICE)))
  {
    printf("FAILED first RegisterRawInputDevices (%lu)\n", GetLastError());
    return 1;
  }

  g_mouse_reports = g_keyboard_reports = 0;
  inject_mouse_and_key();
  const int mouse_before    = g_mouse_reports;
  const int keyboard_before = g_keyboard_reports;
  printf("BEFORE SDL-style registration:  mouse=%d  keyboard=%d\n", mouse_before, keyboard_before);

  // --- Registration 2: exactly SDL2's ToggleRawInput(SDL_TRUE) ---
  // RAWINPUTDEVICE rawMouse = { 0x01, 0x02, 0, NULL };
  RAWINPUTDEVICE sdl_mouse = {0x01, 0x02, 0, nullptr};
  if (!RegisterRawInputDevices(&sdl_mouse, 1, sizeof(RAWINPUTDEVICE)))
  {
    printf("FAILED SDL-style RegisterRawInputDevices (%lu)\n", GetLastError());
    return 1;
  }

  g_mouse_reports = g_keyboard_reports = 0;
  inject_mouse_and_key();
  const int mouse_after    = g_mouse_reports;
  const int keyboard_after = g_keyboard_reports;
  printf("AFTER  SDL-style registration:  mouse=%d  keyboard=%d\n", mouse_after, keyboard_after);

  printf("\n");
  if (mouse_before > 0 && mouse_after == 0)
    printf("RESULT: COLLISION CONFIRMED - the SDL-style registration STOLE the mouse.\n");
  else if (mouse_before > 0 && mouse_after > 0)
    printf("RESULT: no collision - both registrations coexist.\n");
  else
    printf("RESULT: INCONCLUSIVE - the sink saw no mouse input even before (%d).\n", mouse_before);

  if (keyboard_before > 0 && keyboard_after > 0)
    printf("        Keyboard survived, as predicted (SDL never registers usage 0x06).\n");

  // --- And what SDL's disable path does on the way out ---
  RAWINPUTDEVICE sdl_remove = {0x01, 0x02, RIDEV_REMOVE, nullptr};
  RegisterRawInputDevices(&sdl_remove, 1, sizeof(RAWINPUTDEVICE));
  g_mouse_reports = g_keyboard_reports = 0;
  inject_mouse_and_key();
  printf("AFTER  SDL-style RIDEV_REMOVE:  mouse=%d  keyboard=%d\n", g_mouse_reports,
         g_keyboard_reports);
  if (g_mouse_reports == 0)
    printf("        -> leaving play state does NOT hand the mouse back.\n");

  return 0;
}
