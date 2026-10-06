#pragma once

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DbgHelp.h>
#include <crtdbg.h>
#include <cstdint>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <exception>

#pragma comment(lib, "DbgHelp.lib")

namespace crash_handler {

inline LONG WINAPI exception_handler(EXCEPTION_POINTERS *ex)
{
    fprintf(stderr, "\n=== CRASH: exception 0x%08lX at 0x%p ===\n",
            ex->ExceptionRecord->ExceptionCode,
            ex->ExceptionRecord->ExceptionAddress);

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymInitialize(process, NULL, TRUE);

    CONTEXT *ctx = ex->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = ctx->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < 64; i++)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread,
                         &frame, ctx, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;

        char buffer[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *sym = reinterpret_cast<SYMBOL_INFO *>(buffer);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;

        DWORD64 displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, sym))
        {
            IMAGEHLP_LINE64 line = {sizeof(IMAGEHLP_LINE64)};
            DWORD line_disp = 0;
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_disp, &line))
                fprintf(stderr, "  [%2d] %s +0x%llx  (%s:%lu)\n",
                        i, sym->Name, displacement, line.FileName, line.LineNumber);
            else
                fprintf(stderr, "  [%2d] %s +0x%llx\n",
                        i, sym->Name, displacement);
        }
        else
        {
            fprintf(stderr, "  [%2d] 0x%llx\n", i, frame.AddrPC.Offset);
        }
    }

    SymCleanup(process);
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

inline void print_current_stack()
{
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);

    void *stack[64];
    USHORT frames = CaptureStackBackTrace(0, 64, stack, NULL);

    for (USHORT i = 0; i < frames; i++)
    {
        char buffer[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *sym = reinterpret_cast<SYMBOL_INFO *>(buffer);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;

        DWORD64 displacement = 0;
        if (SymFromAddr(process, reinterpret_cast<DWORD64>(stack[i]), &displacement, sym))
        {
            IMAGEHLP_LINE64 line = {sizeof(IMAGEHLP_LINE64)};
            DWORD line_disp = 0;
            if (SymGetLineFromAddr64(process, reinterpret_cast<DWORD64>(stack[i]), &line_disp, &line))
                fprintf(stderr, "  [%2d] %s +0x%llx  (%s:%lu)\n",
                        i, sym->Name, displacement, line.FileName, line.LineNumber);
            else
                fprintf(stderr, "  [%2d] %s +0x%llx\n",
                        i, sym->Name, displacement);
        }
        else
        {
            fprintf(stderr, "  [%2d] 0x%p\n", i, stack[i]);
        }
    }

    SymCleanup(process);
    fflush(stderr);
}

inline void abort_handler(int)
{
    fprintf(stderr, "\n=== CRASH: abort/terminate called ===\n");
    print_current_stack();
}

// A failed assert() or a Debug STL check (_STL_VERIFY) reports through
// _CrtDbgReport and then dies through _invalid_parameter, which fast-fails past
// both the exception filter and SIGABRT: without these two the message is one
// line and no stack. The hook prints the stack at the assertion itself, where
// the checking frame's CALLER is the line that matters.
inline bool assertion_stack_printed = false;

#ifdef _DEBUG
inline int assertion_report_hook(int report_kind, char *message, int *return_value)
{
    if (report_kind == _CRT_ASSERT || report_kind == _CRT_ERROR)
    {
        fprintf(stderr, "\n=== ASSERTION: %s ===\n", message ? message : "");
        print_current_stack();
        assertion_stack_printed = true;
    }
    *return_value = 0;
    return FALSE;
}
#endif

inline void invalid_parameter_handler(const wchar_t *expression, const wchar_t *function,
                                      const wchar_t *file, unsigned int line, uintptr_t)
{
    fprintf(stderr, "\n=== CRASH: invalid parameter %ls in %ls (%ls:%u) ===\n",
            expression ? expression : L"", function ? function : L"", file ? file : L"", line);
    if (!assertion_stack_printed) print_current_stack();
    fflush(stderr);
    _exit(3);
}

inline void install()
{
    SetUnhandledExceptionFilter(exception_handler);
    signal(SIGABRT, abort_handler);
#ifdef _DEBUG
    _CrtSetReportHook(assertion_report_hook);
#endif
    _set_invalid_parameter_handler(invalid_parameter_handler);
    std::set_terminate([]()
    {
        fprintf(stderr, "\n=== CRASH: std::terminate called ===\n");
        abort_handler(0);
        std::abort();
    });
}

} // namespace crash_handler

#else

namespace crash_handler {
inline void install() {} // no-op on non-Windows for now
}

#endif
