#pragma once

#include <clean-core/allocation.hh>
#include <clean-core/assert.hh>
#include <clean-core/fwd.hh>

// TODO:
// - sequence entry points
// - retyping APIs
// - resize APIs? -> would be totally fine I think


/// Dynamically allocated array of T elements with value semantics.
/// Similar to std::vector but without growth operations (push, pop, resize).
/// Owns the underlying memory through cc::allocation<T>.
/// Compatible with the allocation-share protocol for efficient memory sharing.
/// Supports move semantics and allocator-aware construction.
template <class T>
struct cc::array
{
    // element access
public:
    /// Returns a reference to the element at index i.
    /// Precondition: 0 <= i < size().
    [[nodiscard]] constexpr T& operator[](isize i) { return _data.obj_at(i); }
    [[nodiscard]] constexpr T const& operator[](isize i) const { return _data.obj_at(i); }

    /// Returns a reference to the first element.
    /// Precondition: !empty().
    [[nodiscard]] constexpr T& front() { return _data.obj_front(); }
    [[nodiscard]] constexpr T const& front() const { return _data.obj_front(); }

    /// Returns a reference to the last element.
    /// Precondition: !empty().
    [[nodiscard]] constexpr T& back() { return _data.obj_back(); }
    [[nodiscard]] constexpr T const& back() const { return _data.obj_back(); }

    /// Returns a pointer to the underlying contiguous storage.
    /// May be nullptr if the array is default-constructed or empty.
    [[nodiscard]] constexpr T* data() { return _data.obj_start; }
    [[nodiscard]] constexpr T const* data() const { return _data.obj_start; }

    // iterators
public:
    /// Returns a pointer to the first element.
    /// Enables range-based for loops.
    [[nodiscard]] constexpr T* begin() { return _data.obj_start; }
    /// Returns a pointer to one past the last element.
    [[nodiscard]] constexpr T* end() { return _data.obj_end; }
    [[nodiscard]] constexpr T const* begin() const { return _data.obj_start; }
    [[nodiscard]] constexpr T const* end() const { return _data.obj_end; }

    // queries
public:
    /// Returns the number of elements in the array.
    [[nodiscard]] constexpr isize size() const { return _data.obj_size(); }
    /// Returns true if size() == 0.
    [[nodiscard]] constexpr bool empty() const { return !_data.has_objects(); }

    // ctors / allocation
public:
    // array directly from a previous allocation
    // simply treats the live objects as the array
    [[nodiscard]] static array create_from_allocation(cc::allocation<T> data) { return array(cc::move(data)); }

    // initializes a new array with "size" many defaulted elements
    [[nodiscard]] static array create_defaulted(size_t size, cc::memory_resource const* resource = nullptr)
    {
        return array(cc::allocation<T>::create_defaulted(size, resource));
    }

    // initializes a new array with "size" many elements, all copy-constructed from "value"
    [[nodiscard]] static array create_filled(size_t size, T const& value, cc::memory_resource const* resource = nullptr)
    {
        return array(cc::allocation<T>::create_filled(size, value, resource));
    }

    // initializes a new array with "size" many uninitialized elements (only safe for trivial types)
    [[nodiscard]] static array create_uninitialized(size_t size, cc::memory_resource const* resource = nullptr)
    {
        return array(cc::allocation<T>::create_uninitialized(size, resource));
    }

    // creates a deep copy of the provided span
    [[nodiscard]] static array create_copy_of(cc::span<T const> source, cc::memory_resource const* resource = nullptr)
    {
        return array(cc::allocation<T>::create_copy_of(source, resource));
    }

    array() = default;
    ~array() = default;

    // move semantics are already fine via cc::allocation
    array(array&&) = default;
    array& operator=(array&&) = default;

    // deep copy semantics
    array(array const& rhs) : _data(cc::allocation<T>::create_copy_of(rhs._data)) {}
    array& operator=(array const& rhs)
    {
        if (this != &rhs)
            _data = cc::allocation<T>::create_copy_of(rhs._data, _data.custom_resource); // keep lhs resource
        return *this;
    }

    /// Extracts and returns the underlying allocation, leaving the array empty.
    /// The returned `cc::allocation<T>` owns the backing storage and live objects.
    /// The array retains its memory resource for future use.
    /// Enables zero-copy interop with other contiguous containers.
    /// Complexity: O(1).
    cc::allocation<T> extract_allocation() { return cc::move(_data); }

private:
    explicit array(cc::allocation<T> data) : _data(cc::move(data)) {}

    cc::allocation<T> _data;
};

/// Fixed-size array of exactly N elements of type T.
/// Similar to std::array but follows clean-core conventions.
/// Trivial aggregate type - supports aggregate initialization: fixed_array<int, 3> arr = {1, 2, 3}.
/// Owns the underlying memory.
template <class T, cc::isize N>
struct cc::fixed_array
{
    static_assert(N >= 0, "fixed_array size must be non-negative");

    // members
public:
    T _data[N];

    // element access
public:
    /// Returns a reference to the element at index i.
    /// Precondition: 0 <= i < N.
    [[nodiscard]] constexpr T& operator[](isize i)
    {
        CC_ASSERT(0 <= i && i < N, "index out of bounds");
        return _data[i];
    }
    [[nodiscard]] constexpr T const& operator[](isize i) const
    {
        CC_ASSERT(0 <= i && i < N, "index out of bounds");
        return _data[i];
    }

    /// Returns a reference to the first element.
    [[nodiscard]] constexpr T& front() { return _data[0]; }
    [[nodiscard]] constexpr T const& front() const { return _data[0]; }

    /// Returns a reference to the last element.
    [[nodiscard]] constexpr T& back() { return _data[N - 1]; }
    [[nodiscard]] constexpr T const& back() const { return _data[N - 1]; }

    /// Returns a pointer to the underlying contiguous storage.
    [[nodiscard]] constexpr T* data() { return _data; }
    [[nodiscard]] constexpr T const* data() const { return _data; }

    // iterators
public:
    /// Returns a pointer to the first element.
    /// Enables range-based for loops.
    [[nodiscard]] constexpr T* begin() { return _data; }
    [[nodiscard]] constexpr T* end() { return _data + N; }
    [[nodiscard]] constexpr T const* begin() const { return _data; }
    [[nodiscard]] constexpr T const* end() const { return _data + N; }

    // queries
public:
    /// Returns the compile-time size N.
    [[nodiscard]] constexpr isize size() const { return N; }
    /// Returns true if N == 0 (compile-time constant).
    [[nodiscard]] constexpr bool empty() const { return N == 0; }
};

/// Specialization for N == 0 (empty array).
/// Zero-sized arrays T[0] are not valid in standard C++.
template <class T>
struct cc::fixed_array<T, 0>
{
    // element access
public:
    /// operator[] not callable on empty array.
    [[nodiscard]] constexpr T& operator[](isize)
    {
        static_assert(false, "operator[] not available on empty fixed_array");
    }
    [[nodiscard]] constexpr T const& operator[](isize) const
    {
        static_assert(false, "operator[] not available on empty fixed_array");
    }

    /// front() not callable on empty array.
    [[nodiscard]] constexpr T& front() { static_assert(false, "front() not available on empty fixed_array"); }
    [[nodiscard]] constexpr T const& front() const
    {
        static_assert(false, "front() not available on empty fixed_array");
    }

    /// back() not callable on empty array.
    [[nodiscard]] constexpr T& back() { static_assert(false, "back() not available on empty fixed_array"); }
    [[nodiscard]] constexpr T const& back() const { static_assert(false, "back() not available on empty fixed_array"); }

    /// Returns nullptr for empty array.
    [[nodiscard]] constexpr T* data() { return nullptr; }
    [[nodiscard]] constexpr T const* data() const { return nullptr; }

    // iterators
public:
    /// Returns nullptr (begin == end for empty array).
    [[nodiscard]] constexpr T* begin() { return nullptr; }
    [[nodiscard]] constexpr T* end() { return nullptr; }
    [[nodiscard]] constexpr T const* begin() const { return nullptr; }
    [[nodiscard]] constexpr T const* end() const { return nullptr; }

    // queries
public:
    /// Returns 0.
    [[nodiscard]] constexpr isize size() const { return 0; }
    /// Returns true.
    [[nodiscard]] constexpr bool empty() const { return true; }
};
