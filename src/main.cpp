#include "Application.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

#ifdef _WIN32
// A window that vanishes says nothing about why, and a crash inside a graphics
// driver looks exactly like a crash inside this program from the outside. This
// writes what Windows knows at the moment of death - what went wrong, where,
// and which module that address belongs to - beside the executable, so the
// next report is one file rather than one sentence.
std::wstring moduleOfAddress(void* address, std::uintptr_t& offset) {
    offset = 0;
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module) ||
        module == nullptr) {
        return L"(no module)";
    }
    offset = reinterpret_cast<std::uintptr_t>(address) -
             reinterpret_cast<std::uintptr_t>(module);
    wchar_t name[MAX_PATH] = {};
    if (GetModuleFileNameW(module, name, MAX_PATH) == 0)
        return L"(unnamed module)";
    const wchar_t* leaf = std::wcsrchr(name, L'\\');
    return leaf != nullptr ? leaf + 1 : name;
}

const wchar_t* exceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return L"access violation";
    case EXCEPTION_STACK_OVERFLOW:        return L"stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return L"illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return L"integer divide by zero";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return L"float divide by zero";
    case EXCEPTION_PRIV_INSTRUCTION:      return L"privileged instruction";
    case EXCEPTION_IN_PAGE_ERROR:         return L"in-page error";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return L"misaligned data";
    default:                              return L"unhandled exception";
    }
}

LONG WINAPI writeCrashReport(EXCEPTION_POINTERS* pointers) {
    if (pointers == nullptr || pointers->ExceptionRecord == nullptr)
        return EXCEPTION_CONTINUE_SEARCH;

    wchar_t executable[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    std::filesystem::path report(executable);
    report.replace_filename(L"Fluid Solver UI crash.txt");

    std::wofstream out(report, std::ios::app);
    if (!out.is_open())
        return EXCEPTION_CONTINUE_SEARCH;

    const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
    std::uintptr_t offset = 0;
    const std::wstring module = moduleOfAddress(record.ExceptionAddress,
                                                offset);

    SYSTEMTIME now{};
    GetLocalTime(&now);
    out << L"---- " << now.wYear << L'-' << now.wMonth << L'-' << now.wDay
        << L' ' << now.wHour << L':' << now.wMinute << L':' << now.wSecond
        << L" ----\n";
    out << L"  " << exceptionName(record.ExceptionCode) << L" (0x" << std::hex
        << record.ExceptionCode << std::dec << L")\n";
    out << L"  at " << module << L" + 0x" << std::hex << offset << std::dec
        << L"\n";
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        record.NumberParameters >= 2) {
        out << L"  " << (record.ExceptionInformation[0] != 0 ? L"writing"
                                                             : L"reading")
            << L" 0x" << std::hex << record.ExceptionInformation[1]
            << std::dec << L"\n";
    }
    out << L"  module bases, so any address above can be placed:\n";
    const wchar_t* leaf = std::wcsrchr(executable, L'\\');
    out << L"    " << (leaf != nullptr ? leaf + 1 : executable)
        << L" base 0x" << std::hex
        << reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))
        << std::dec << L"\n";
    out << L"    " << module << L" base 0x" << std::hex
        << (reinterpret_cast<std::uintptr_t>(record.ExceptionAddress) - offset)
        << std::dec << L"\n";

    // Not a call stack - a sweep of the stack memory for values that land
    // inside a loaded module, which is what return addresses look like. It
    // reads over the top of local variables that happen to resemble one, so
    // the order is a hint rather than a sequence. It needs no dbghelp and it
    // survives the case this was written for, where the instruction pointer
    // belongs to no module at all and there is nothing else to go on.
    if (pointers->ContextRecord != nullptr) {
#if defined(_M_X64) || defined(__x86_64__)
        const std::uintptr_t stackPointer = pointers->ContextRecord->Rsp;
#elif defined(_M_IX86) || defined(__i386__)
        const std::uintptr_t stackPointer = pointers->ContextRecord->Esp;
#elif defined(_M_ARM64) || defined(__aarch64__)
        const std::uintptr_t stackPointer = pointers->ContextRecord->Sp;
#else
        const std::uintptr_t stackPointer = 0;
#endif
        if (stackPointer != 0) {
            out << L"  addresses on the stack that belong to a module:\n";
            int found = 0;
            for (int slot = 0; slot < 512 && found < 24; ++slot) {
                void** cell = reinterpret_cast<void**>(
                    stackPointer + slot * sizeof(void*));
                if (IsBadReadPtr(cell, sizeof(void*)))
                    break;
                std::uintptr_t which = 0;
                const std::wstring name = moduleOfAddress(*cell, which);
                if (name == L"(no module)" || name == L"(unnamed module)")
                    continue;
                out << L"    " << name << L" + 0x" << std::hex << which
                    << std::dec << L"\n";
                ++found;
            }
        }
    }
    out.close();
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

std::vector<std::filesystem::path> systemArguments(
    int argc,
    char* argv[]) {
#ifdef _WIN32
    int wideArgumentCount = 0;
    LPWSTR* wideArguments =
        CommandLineToArgvW(GetCommandLineW(), &wideArgumentCount);
    if (wideArguments != nullptr) {
        std::vector<std::filesystem::path> arguments;
        arguments.reserve(static_cast<std::size_t>(wideArgumentCount));
        for (int index = 0; index < wideArgumentCount; ++index) {
            arguments.emplace_back(wideArguments[index]);
        }
        LocalFree(wideArguments);
        return arguments;
    }
#endif
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return arguments;
}

std::filesystem::path executablePath(
    const std::vector<std::filesystem::path>& arguments) {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length > 0 &&
        static_cast<std::size_t>(length) < buffer.size()) {
        buffer.resize(length);
        return std::filesystem::path(buffer);
    }
#elif defined(__linux__)
    // Everything the UI keeps beside itself - the solver, output/, the font,
    // the saved preferences - hangs off this path, and argv[0] is not it. A
    // launcher symlink or a .desktop entry hands over a bare name or a link,
    // and resolving that against the working directory aimed the whole lot at
    // wherever the program happened to be started from.
    {
        std::error_code linkError;
        const std::filesystem::path self =
            std::filesystem::read_symlink("/proc/self/exe", linkError);
        if (!linkError && !self.empty()) {
            return self;
        }
    }
#elif defined(__APPLE__)
    {
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        if (size > 0) {
            std::string pathBuffer(size, '\0');
            if (_NSGetExecutablePath(pathBuffer.data(), &size) == 0) {
                pathBuffer.resize(std::char_traits<char>::length(
                    pathBuffer.c_str()));
                std::error_code realError;
                const std::filesystem::path resolved =
                    std::filesystem::canonical(pathBuffer, realError);
                return realError
                    ? std::filesystem::path(pathBuffer)
                    : resolved;
            }
        }
    }
#endif
    std::error_code error;
    std::filesystem::path executable =
        arguments.empty()
            ? std::filesystem::path{}
            : std::filesystem::absolute(arguments.front(), error);
    if (error && !arguments.empty()) {
        executable = arguments.front();
    }
    return executable;
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetUnhandledExceptionFilter(writeCrashReport);
#endif
    const std::vector<std::filesystem::path> arguments =
        systemArguments(argc, argv);
    if (arguments.size() > 2) {
        std::cerr
            << "Usage: \"Fluid Solver UI\" [model.stl | model.obj | folder of solution_*.vtk]\n";
        return 1;
    }

    const std::filesystem::path initialModel =
        arguments.size() == 2
            ? arguments[1]
            : std::filesystem::path{};
    maskui::Application application(
        executablePath(arguments),
        initialModel);
    return application.run();
}
