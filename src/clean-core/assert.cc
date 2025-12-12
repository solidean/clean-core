#include "assert.hh"

#include <cstdio>
#include <cstdlib>
#include <print>

#ifdef CC_COMPILER_MSVC
extern "C" __declspec(dllimport) int __stdcall IsDebuggerPresent() noexcept;
#endif

#ifdef CC_COMPILER_POSIX
#include <unistd.h>

#include <cstring>
#endif

namespace cc::impl
{
CC_COLD_FUNC void handle_assert_failure(std::string_view expression,
                                        std::string const& message,
                                        std::source_location location)
{
    std::println(stderr, "Assertion failed: {}", expression);
    std::println(stderr, "  Message: {}", message);
    std::println(stderr, "  Location: {}:{}:{} ({})", location.file_name(), location.line(), location.column(),
                 location.function_name());

    // no abort here, it's outside
}

bool is_debugger_connected() noexcept
{
#ifdef CC_COMPILER_MSVC
    return ::IsDebuggerPresent() != 0;
#elif defined(CC_OS_LINUX)
    // Check /proc/self/status for TracerPid
    if (auto* f = std::fopen("/proc/self/status", "r"))
    {
        char buf[1024];
        while (std::fgets(buf, sizeof(buf), f))
        {
            if (std::strncmp(buf, "TracerPid:", 10) == 0)
            {
                int pid = 0;
                std::sscanf(buf + 10, "%d", &pid);
                std::fclose(f);
                return pid != 0;
            }
        }
        std::fclose(f);
    }
    return false;
#elif defined(CC_OS_APPLE)
    // Use sysctl to check P_TRACED flag
    extern "C" int sysctl(int*, unsigned int, void*, unsigned long*, void*, unsigned long) noexcept;

    int mib[4] = {1 /* CTL_KERN */, 14 /* KERN_PROC */, 1 /* KERN_PROC_PID */, 0};
    mib[3] = getpid();

    struct kinfo_proc
    {
        char pad[32];
        int p_flag;
    } info{};

    unsigned long size = sizeof(info);
    if (sysctl(mib, 4, &info, &size, nullptr, 0) == 0)
        return (info.p_flag & 0x00000800 /* P_TRACED */) != 0;

    return false;
#else
    return false;
#endif
}

[[noreturn]] void perform_abort() noexcept
{
    std::abort();
}
} // namespace cc::impl
