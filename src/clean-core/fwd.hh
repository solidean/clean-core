#pragma once

#include <cstddef>
#include <cstdint>


namespace cc
{

//
// Primitives
//

// Explicitly-sized primitive types
// We encourage using these types wherever the range is important for correctness or memory layout.
// However, we happily use "int" as a default integer if the range doesn't matter much
// (e.g. well below a few millions, such as loop counters or small counts).

// signed integers
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

// unsigned integers
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

// floating point
using f32 = float;
using f64 = double;

// generic bytes
using byte = std::byte;

// signed size type (controversial but intentional)
// We use signed i64 for sizes and indices instead of size_t for several reasons:
// * Arithmetic with sizes often requires subtraction, which causes underflow bugs with unsigned
//   (e.g. "size - 1" underflows when size is 0, becoming a huge positive number)
// * Mixed signed/unsigned arithmetic is a major source of bugs and confusing implicit conversions
// * Negative values are useful for error returns, sentinel values, and relative offsets
// * We only target 64-bit platforms, so i64 provides plenty of range (2^63 - 1 > 9 quintillion)
// * As a greenfield standard library not interoperating with std:: most of the time,
//   we avoid the backwards compatibility friction that plagues existing C++ codebases
// * Modern practice (see Stroustrup's P1428R0) recognizes unsigned sizes as a historical mistake
// * Bounds checking happens at runtime anyway, so unsigned providing "extra range" is illusory
// * Mathematically: signed integers model a proper subset of the integers with correct arithmetic and ordering.
//   Unsigned integers model a modulo ring where operations wrap around and comparisons break
//   (a < b does NOT imply a + c < b + c). Overflow being UB for signed is good: it means we model
//   actual integers as long as we stay in bounds. Unsigned silently transitions to a different
//   algebraic structure outside bounds, which causes subtle bugs.
using isize = i64;

// pointer
using nullptr_t = std::nullptr_t;

//
// Memory
//

struct memory_resource;
template <class T>
struct allocation;


//
// Views
//

template <class T>
struct span;
template <class T, isize N>
struct fixed_span;

//
// Container
//

struct nullopt_t;
template <class T>
struct optional;

template <class T, class ContainerT>
struct allocating_container;

template <class T>
struct array;
template <class T>
struct unique_array;
template <class T, isize N>
struct fixed_array;

template <class T>
struct vector;
template <class T>
struct unique_vector;
// template <class T, isize N>
// struct fixed_vector;

// template <class T>
// struct devector;
// template <class T, isize N>
// struct fixed_devector;

} // namespace cc
