#pragma once

#include <clean-core/macros.hh>

#include <format>
#include <source_location>
#include <string>
#include <string_view>

// =========================================================================================================
// CC_ASSERT - Runtime assertion with formatted message
//
// Validates a condition at runtime and triggers a debugger break + abort on failure.
//
// Features:
//   - Formatted error messages using std::format
//   - Automatic source location capture (file, line, function)
//   - Debugger integration: breaks into debugger when attached, otherwise aborts
//   - Expression stringification for clear error reporting
//   - Active in debug and release-with-debug-info builds by default (we believe in more checks)
//
// When assertions are active:
//   Assertions are enabled in CC_DEBUG and CC_RELWITHDEBINFO builds.
//   In CC_RELEASE builds, assertions are disabled unless CC_ENABLE_ASSERT_IN_RELEASE is defined.
//
// What assertions are for:
//   Assertions protect INVARIANTS, PRECONDITIONS, and POSTCONDITIONS.
//   They catch PROGRAMMER ERRORS early during development.
//
// What assertions are NOT for:
//   - NOT for user input validation
//   - NOT for exceptional error handling
//   - NOT for common/expected error conditions
//
// Error handling strategy:
//   - Assertions      -> programmer errors, violated invariants/preconditions/postconditions
//   - Exceptions      -> exceptional & nonlocal error handling
//   - result<T, E>    -> common/expected error handling
//
// Important:
//   Assertions can be semantically equivalent to std::terminate().
//   NEVER trigger assertions based on user input or external conditions!
//   Production builds can provide a custom assertion handler to prevent data loss.
//
// Usage:
//   CC_ASSERT(ptr != nullptr, "pointer must not be null");
//   CC_ASSERT(size > 0, "size must be positive, got {}", size);
//   CC_ASSERT(idx < array.size(), "index {} out of bounds (size: {})", idx, array.size());
//
//   void process(int* data, size_t count)
//   {
//       CC_ASSERT(data != nullptr, "data pointer must be valid"); // precondition
//       CC_ASSERT(count > 0, "count must be positive");           // precondition
//       // ... process data ...
//       CC_ASSERT(result_valid(), "postcondition violated");      // postcondition
//   }
//
// Rationale:
//   Unlike standard assert(), CC_ASSERT provides:
//     - Rich formatting capabilities for diagnostic messages
//     - Better debugger integration (breaks at assertion site, not in abort())
//     - Configurable behavior across build configurations
//     - Source location without macros like __FILE__ and __LINE__
//
#define CC_ASSERT(cond, msg, ...) CC_IMPL_ASSERT(cond, msg, ##__VA_ARGS__)

// =========================================================================================================
// CC_DEBUG_BREAK - Conditional debugger breakpoint
//
// Triggers a debugger break if a debugger is attached, otherwise does nothing.
//
// Usage:
//   CC_DEBUG_BREAK(); // breaks into debugger if attached
//
//   if (some_error_condition)
//   {
//       log_error("critical error detected");
//       CC_DEBUG_BREAK();
//   }
//
// Rationale:
//   - Safely breaks into the debugger without crashing when no debugger is present
//   - Executes inline (not in a function) so debugger breaks at the exact location
//   - Platform-specific implementation ensures correct debugger interaction
//   - Useful for investigating unexpected conditions without full assertion failure
//
#define CC_DEBUG_BREAK() CC_IMPL_DEBUG_BREAK()

// =========================================================================================================
// CC_BREAK_AND_ABORT - Debug break followed by program termination
//
// Triggers a debugger break (if attached) then unconditionally aborts the program.
//
// Usage:
//   CC_BREAK_AND_ABORT(); // break into debugger, then terminate
//
//   if (unrecoverable_error())
//   {
//       log_fatal("cannot continue");
//       CC_BREAK_AND_ABORT();
//   }
//
// Rationale:
//   - Combines debugger investigation opportunity with guaranteed termination
//   - Used by CC_ASSERT after logging assertion details
//   - Ensures program doesn't continue in an invalid state
//   - Debugger break happens first to allow inspection before termination
//
#define CC_BREAK_AND_ABORT() (CC_DEBUG_BREAK(), ::cc::impl::perform_abort())


// =========================================================================================================
// Implementation details
// =========================================================================================================

namespace cc
{
namespace impl
{
// Called when an assertion fails
// Prints diagnostic information to stderr
// Note: does not abort, caller must follow with CC_BREAK_AND_ABORT()
CC_COLD_FUNC void handle_assert_failure(std::string_view expression, std::string message, std::source_location location);

// Checks if a debugger is currently attached to the process
// Platform-specific implementation (Windows: IsDebuggerPresent, Linux: /proc, macOS: sysctl)
bool is_debugger_connected() noexcept;

// Terminates the program
// Wrapper around std::abort() to allow future customization
[[noreturn]] void perform_abort() noexcept;
} // namespace impl
} // namespace cc

// Platform-specific debugger break implementation
// The debugger should break right in the assert macro, so this cannot hide in a function call

#ifdef CC_COMPILER_MSVC

// __debugbreak() terminates immediately without an attached debugger
#define CC_IMPL_DEBUG_BREAK() (::cc::impl::is_debugger_connected() ? __debugbreak() : void(0))

#elif defined(CC_COMPILER_POSIX)

// __builtin_trap() causes an illegal instruction and crashes without an attached debugger
// we use a SIGTRAP to signal a trace/breakpoint
// the _trap is technically not correct because a BREAKpoint is recoverable
// the use in CC_ASSERT is simply to provide a cleaner debugging experience
// and is followed by an abort anyways
// NOTE: we don't want to pull in any posix header here, so we simply declare raise
//       SIGTRAP is 5 according to https://man7.org/linux/man-pages/man7/signal.7.html
extern "C" int raise(int) noexcept;
#define CC_IMPL_DEBUG_BREAK() (::cc::impl::is_debugger_connected() ? (void)::raise(5) : void(0))

#else

#define CC_IMPL_DEBUG_BREAK() void(0)

#endif

// Assert implementation - enabled in debug/relwithdebinfo, optionally in release

#if defined(CC_DEBUG) || defined(CC_RELWITHDEBINFO) || defined(CC_ENABLE_ASSERT_IN_RELEASE)

#define CC_IMPL_ASSERT(cond, msg, ...)                                                            \
    do                                                                                            \
    {                                                                                             \
        if CC_CONDITION_UNLIKELY (!(cond))                                                        \
        {                                                                                         \
            ::cc::impl::handle_assert_failure(#cond, std::format(msg __VA_OPT__(, ) __VA_ARGS__), \
                                              std::source_location::current());                   \
            CC_BREAK_AND_ABORT();                                                                 \
        }                                                                                         \
    } while (false)

#else

// In release builds without CC_ENABLE_ASSERT_IN_RELEASE, assertions are stripped
// We still use the format string to ensure it compiles correctly
#define CC_IMPL_ASSERT(cond, msg, ...)                          \
    do                                                          \
    {                                                           \
        CC_UNUSED(cond);                                        \
        CC_UNUSED(std::format(msg __VA_OPT__(, ) __VA_ARGS__)); \
    } while (false)

#endif
