#pragma once

#include <clean-core/assert.hh>
#include <clean-core/fwd.hh>

// =========================================================================================================
// Utility functions for common operations
// =========================================================================================================
//
// Move semantics:
//   move(value)                 - cast value to rvalue reference for moving
//   forward<T>(value)           - perfect forwarding for template arguments
//   exchange(obj, new_val)      - replace obj with new_val and return old value
//
// Comparison and clamping:
//   max(a, b)                   - returns the larger of two values (requires operator<)
//   min(a, b)                   - returns the smaller of two values (requires operator<)
//   clamp(v, lo, hi)            - clamps value v to range [lo, hi] (requires operator<)
//
// Wrapping arithmetic:
//   wrapped_increment(pos, max) - increment with wrap-around to 0 at max
//   wrapped_decrement(pos, max) - decrement with wrap-around to max-1 at 0
//
// Integer division:
//   int_div_round_up(nom, denom)           - divide integers and round up (both > 0)
//   int_round_up_to_multiple(val, mult)    - round up value to next multiple
//
// Swapping:
//   swap(a, b)                  - swap values, respects ADL swap overloads
//   swap_by_move(a, b)          - swap without respecting custom overloads
//
// Alignment (value or pointer):
//   is_power_of_two(value)           - check if value is a power of 2
//   align_up(value, alignment)       - increment to next aligned boundary (power of 2)
//   align_down(value, alignment)     - decrement to previous aligned boundary (power of 2)
//   align_up_masked(value, mask)     - increment using pre-computed mask
//   align_down_masked(value, mask)   - decrement using pre-computed mask
//   is_aligned(value, alignment)     - check if aligned at boundary (power of 2)
//

namespace cc
{
// =========================================================================================================
// Move semantics
// =========================================================================================================

/// Cast value to rvalue reference to enable move semantics
/// Indicates that the value can be moved from (its resources can be transferred)
/// Usage:
///   vec.push_back(cc::move(obj));  // transfer obj into vector
///   T b = cc::move(a);              // move construct b from a
template <class T>
[[nodiscard]] constexpr T&& move(T& value) noexcept
{
    return static_cast<T&&>(value);
}

/// Perfect forwarding for template arguments
/// Preserves value category (lvalue/rvalue) when forwarding arguments
/// Usage:
///   template<class T>
///   void wrapper(T&& arg) {
///       foo(cc::forward<T>(arg));  // forwards as lvalue or rvalue depending on T
///   }
template <class T>
[[nodiscard]] constexpr T&& forward(T& value) noexcept
{
    return static_cast<T&&>(value);
}

template <class T>
[[nodiscard]] constexpr T&& forward(T&& value) noexcept // NOLINT
{
    return static_cast<T&&>(value);
}

/// Replace object with new value and return the old value
/// Atomically assigns new_val to obj and returns obj's previous value
/// Usage:
///   int old = cc::exchange(counter, 0);      // reset counter to 0, get old value
///   auto ptr = cc::exchange(p, nullptr);     // take ownership of p, set p to null
///   State prev = cc::exchange(state, State::Ready);  // transition state
template <class T, class U = T>
[[nodiscard]] constexpr T exchange(T& obj, U&& new_val) // NOLINT
{
    T old_val = static_cast<T&&>(obj);
    obj = forward<U>(new_val);
    return old_val;
}

// =========================================================================================================
// Comparison and clamping
// =========================================================================================================

/// Returns the larger of two values using operator<
/// Returns a reference to allow selecting elements without copying (works with noncopyables)
/// Guarantees that cc::min(a,b) and cc::max(a,b) return different elements (never both return the same)
/// When a == b, max returns b (consistent with min returning a)
/// Usage:
///   int const& larger = cc::max(x, y);
///   auto const& obj = cc::max(obj_a, obj_b);  // works with noncopyables
///   cc::max(obj_a, obj_b).foo();              // can call members on result
template <class T>
[[nodiscard]] constexpr T const& max(T const& a, T const& b)
{
    static_assert(requires { a < b; }, "T must support operator<");
    return (a < b) ? b : a; // NOLINT(bugprone-return-const-ref-from-parameter) - returning reference is intentional
}

/// Returns the smaller of two values using operator<
/// Returns a reference to allow selecting elements without copying (works with noncopyables)
/// Guarantees that cc::min(a,b) and cc::max(a,b) return different elements (never both return the same)
/// When a == b, min returns a (consistent with max returning b)
/// Usage:
///   int const& smaller = cc::min(x, y);
///   auto const& obj = cc::min(obj_a, obj_b);  // works with noncopyables
///   cc::min(vec[i], vec[j]).process();        // can call members on result
template <class T>
[[nodiscard]] constexpr T const& min(T const& a, T const& b)
{
    static_assert(requires { a < b; }, "T must support operator<");
    return (a < b) ? a : b; // NOLINT(bugprone-return-const-ref-from-parameter) - returning reference is intentional
}

/// Clamps a value to the range [lo, hi]
/// Returns a reference to one of {v, lo, hi} without copying (works with noncopyables)
/// Precondition: lo <= hi (expressed as !(hi < lo))
/// Usage:
///   int clamped = cc::clamp(x, 0, 100);           // ensures x is in [0, 100]
///   float normalized = cc::clamp(val, 0.0f, 1.0f);
///   cc::clamp(obj, min_obj, max_obj).foo();       // can call members on result
template <class T>
[[nodiscard]] constexpr T const& clamp(T const& v, T const& lo, T const& hi);

// =========================================================================================================
// Wrapping arithmetic
// =========================================================================================================

/// Increment with wrap-around: (pos + 1) % max
/// When pos + 1 == max, returns 0; otherwise returns pos + 1
/// Precondition: max > 0
/// Generates optimal assembly for ring buffer increment (no division)
/// Usage:
///   int idx = wrapped_increment(idx, buffer_size);  // wraps at buffer_size
///   // wrapped_increment(0, 3) == 1
///   // wrapped_increment(2, 3) == 0
template <class T>
[[nodiscard]] constexpr T wrapped_increment(T pos, T max)
{
    CC_ASSERT(max > 0, "wrapped_increment: max must be positive");
    ++pos;
    return pos == max ? T(0) : pos;
}

/// Decrement with wrap-around: (pos - 1 + max) % max
/// When pos == 0, returns max - 1; otherwise returns pos - 1
/// Precondition: max > 0
/// Generates optimal assembly for ring buffer decrement (no division)
/// Usage:
///   int idx = wrapped_decrement(idx, buffer_size);  // wraps at 0 to buffer_size-1
///   // wrapped_decrement(1, 3) == 0
///   // wrapped_decrement(0, 3) == 2
template <class T>
[[nodiscard]] constexpr T wrapped_decrement(T pos, T max)
{
    CC_ASSERT(max > 0, "wrapped_decrement: max must be positive");
    return pos == 0 ? max - 1 : pos - 1;
}

// =========================================================================================================
// Integer division
// =========================================================================================================

/// Divide integers and round up: ceil(nom / denom)
/// Precondition: nom > 0 && denom > 0
/// Equivalent to (nom + denom - 1) / denom but avoids overflow
/// Usage:
///   int pages = int_div_round_up(total_items, items_per_page);
///   // int_div_round_up(10, 3) == 4
///   // int_div_round_up(9, 3) == 3
template <class T>
[[nodiscard]] constexpr T int_div_round_up(T nom, T denom)
{
    CC_ASSERT(nom > 0 && denom > 0, "int_div_round_up: both nom and denom must be positive");
    return 1 + ((nom - 1) / denom);
}

/// Round up to the next multiple of a given value
/// Returns the smallest multiple of 'multiple' that is >= val
/// Usage:
///   int aligned = cc::int_round_up_to_multiple(size, 10);  // round to next multiple of 10
///   // cc::int_round_up_to_multiple(23, 10) == 30
///   // cc::int_round_up_to_multiple(30, 10) == 30
/// Corner cases:
///   val == 0: returns 0
///   multiple == 1: returns val
/// Preconditions:
///   multiple > 0
/// Note:
///   For power-of-two multiples, use align_up() instead - it's faster
template <class T>
[[nodiscard]] constexpr T int_round_up_to_multiple(T val, T multiple)
{
    CC_ASSERT(multiple > 0, "int_round_up_to_multiple: multiple must be positive");
    return ((val + multiple - 1) / multiple) * multiple;
}

// =========================================================================================================
// Swapping
// =========================================================================================================

namespace impl
{
struct swap_fn
{
    template <class T>
    constexpr void operator()(T& a, T& b) const;
};
} // namespace impl

/// ADL-aware swap that respects custom swap overloads
/// Implemented as a function object (not a function) so it cannot be found by ADL
/// This allows calling unqualified swap(a, b) inside the implementation to find custom overloads
/// while preventing infinite recursion
/// Usage:
///   cc::swap(a, b);  // finds custom swap via ADL if available, otherwise uses move-based swap
[[maybe_unused]] constexpr impl::swap_fn swap;

/// Simple swap that does not respect custom overloads
/// Always uses move construction/assignment (T must be move-constructible and move-assignable)
/// Use when you explicitly want to bypass custom swap implementations
/// Usage:
///   cc::swap_by_move(a, b);  // always uses T's move operations
template <class T>
constexpr void swap_by_move(T& a, T& b)
{
    T tmp = static_cast<T&&>(a);
    a = static_cast<T&&>(b);
    b = static_cast<T&&>(tmp);
}

// =========================================================================================================
// Alignment (for values or pointers)
// =========================================================================================================

/// Check if a positive value is a power of two
/// Returns true if value is a power of 2 (1, 2, 4, 8, 16, ...)
/// Usage:
///   bool ok = cc::is_power_of_two(256);  // true
///   bool bad = cc::is_power_of_two(100); // false
/// Preconditions:
///   value > 0
template <class T>
[[nodiscard]] constexpr bool is_power_of_two(T value)
{
    CC_ASSERT(value > 0, "is_power_of_two: value must be positive");
    return (value & (value - 1)) == 0;
}

/// Increment value to align at the given pre-computed mask
/// mask should be (alignment - 1) where alignment is a power of 2
/// Faster than align_up when mask is precomputed
/// Usage:
///   auto* aligned = cc::align_up_masked(ptr, 15);  // align to 16 bytes (mask = 16-1 = 15)
///   int aligned_val = cc::align_up_masked(300, 15);  // = 304
template <class T>
[[nodiscard]] constexpr T align_up_masked(T value, isize mask)
{
    return (T)(((isize)value + mask) & ~mask);
}

/// Decrement value to align at the given pre-computed mask
/// mask should be (alignment - 1) where alignment is a power of 2
/// Faster than align_down when mask is precomputed
/// Usage:
///   auto* aligned = cc::align_down_masked(ptr, 15);  // align to 16 bytes (mask = 16-1 = 15)
///   int aligned_val = cc::align_down_masked(300, 15);  // = 288
template <class T>
[[nodiscard]] constexpr T align_down_masked(T value, isize mask)
{
    return (T)((isize)value & ~mask);
}

/// Increment value to align at the given boundary
/// Usage:
///   auto* aligned = cc::align_up(ptr, 256);      // align pointer to 256 bytes
///   int val = cc::align_up(300, 16);             // = 304 (next multiple of 16)
///   // cc::align_up(0x5ACE, 256) == 0x5B00
/// Corner cases:
///   value already aligned: returns value unchanged
///   alignment == 1: returns value unchanged
/// Preconditions:
///   alignment > 0 and alignment must be a power of 2
template <class T>
[[nodiscard]] constexpr T align_up(T value, isize alignment)
{
    CC_ASSERT(alignment > 0 && is_power_of_two(alignment), "align_up: alignment must be a power of 2");
    return align_up_masked(value, alignment - 1);
}

/// Decrement value to align at the given boundary
/// Usage:
///   auto* aligned = cc::align_down(ptr, 256);    // align pointer to 256 bytes
///   int val = cc::align_down(300, 16);           // = 288 (previous multiple of 16)
///   // cc::align_down(0x5ACE, 256) == 0x5A00
/// Corner cases:
///   value already aligned: returns value unchanged
///   alignment == 1: returns value unchanged
/// Preconditions:
///   alignment > 0 and alignment must be a power of 2
template <class T>
[[nodiscard]] constexpr T align_down(T value, isize alignment)
{
    CC_ASSERT(alignment > 0 && is_power_of_two(alignment), "align_down: alignment must be a power of 2");
    return align_down_masked(value, alignment - 1);
}

/// Check if value is aligned at the given boundary
/// Usage:
///   if (is_aligned(ptr, 16)) { /* ptr is 16-byte aligned */ }
///   bool ok = is_aligned(size, 4096);  // check if size is page-aligned
/// Corner cases:
///   alignment == 1: always returns true
/// Preconditions:
///   alignment > 0 and alignment must be a power of 2
template <class T>
[[nodiscard]] constexpr bool is_aligned(T value, isize alignment)
{
    CC_ASSERT(alignment > 0 && is_power_of_two(alignment), "is_aligned: alignment must be a power of 2");
    return 0 == ((isize)value & (alignment - 1));
}
} // namespace cc

// =========================================================================================================
// Implementation
// =========================================================================================================

// must be done outside of the cc namespace so cc::swap cannot be found anymore
namespace _no_cc_namespace // NOLINT
{
template <class T>
constexpr void do_swap_impl(T& a, T& b)
{
    if constexpr (requires { swap(a, b); })
    {
        swap(a, b);
    }
    else
    {
        T tmp = static_cast<T&&>(a);
        a = static_cast<T&&>(b);
        b = static_cast<T&&>(tmp);
    }
}
} // namespace _no_cc_namespace

template <class T>
constexpr void cc::impl::swap_fn::operator()(T& a, T& b) const
{
    _no_cc_namespace::do_swap_impl(a, b, 0);
}
