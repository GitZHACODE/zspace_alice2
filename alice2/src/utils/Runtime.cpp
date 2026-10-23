#include "Runtime.h"
#include <filesystem>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace alice2 {
void initializeRuntime(const char* executable) {
    namespace fs = std::filesystem;
    fs::path path;
#ifdef _WIN32
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!size) throw std::runtime_error("Cannot locate the executable");
        if (size < buffer.size()) { path = std::wstring(buffer.data(), size); break; }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__linux__)
    path = fs::read_symlink("/proc/self/exe");
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        throw std::runtime_error("Cannot locate the executable");
    path = fs::canonical(buffer.data());
#else
    path = fs::absolute(executable);
#endif
    (void)executable;
    fs::current_path(path.parent_path());
    fs::create_directories("data");
    fs::create_directories(".user");
}
}
