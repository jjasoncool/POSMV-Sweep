#pragma once
#include <string_view>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif
namespace posmv {
// Remove our common flag before the existing CLI parser sees it.
inline bool setup_console(int& argc, char** argv) {
    bool debug = false;
    int count = 1;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--debug") debug = true;
        else argv[count++] = argv[i];
    }
    argc = count;
    argv[count] = nullptr;
#ifdef _WIN32
    if (argc > 1 || debug) {
        const auto usable = [](DWORD id) {
            HANDLE handle = GetStdHandle(id);
            return handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
        };
        const bool out = usable(STD_OUTPUT_HANDLE), err = usable(STD_ERROR_HANDLE);
        if (AttachConsole(ATTACH_PARENT_PROCESS) || (debug && AllocConsole())) {
            if (!out) (void)std::freopen("CONOUT$", "w", stdout);
            if (!err) (void)std::freopen("CONOUT$", "w", stderr);
        }
    }
#endif
    if (debug) std::fprintf(stderr, "Debug console enabled; raw packet tracing is not enabled.\n");
    return debug;
}
}