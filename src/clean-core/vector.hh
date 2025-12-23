#pragma once

#include <clean-core/impl/allocating_container.hh>


// TODO:
// - sequence entry points
// - retyping APIs
// - equality, order, hashing
// - insert/erase operations
// - reserve with specific strategy (front/back/balanced)
// - shrink_to_fit
// - assign operations
// - emplace/insert at arbitrary positions


/// Dynamically allocated vector of T elements with value semantics.
/// Similar to std::vector with support for growth operations (push, pop, resize).
/// Owns the underlying memory through cc::allocation<T>.
/// Compatible with the allocation-share protocol for efficient memory sharing.
/// Supports move semantics and allocator-aware construction.
template <class T>
struct cc::vector : private cc::allocating_container<T, vector<T>>
{
    using base = cc::allocating_container<T, vector<T>>;

    // element access
public:
    using base::operator[]; // access element by index
    using base::back;       // access last element
    using base::data;       // get pointer to underlying storage
    using base::front;      // access first element

    // iterators
public:
    using base::begin; // get pointer to first element
    using base::end;   // get pointer to one past last element

    // queries
public:
    using base::empty;      // check if vector is empty
    using base::size;       // get number of elements
    using base::size_bytes; // get total size in bytes

    // capacity queries
public:
    using base::capacity_back;         // get available capacity at back
    using base::has_capacity_back_for; // check if capacity exists for N elements at back

    /// Returns the total capacity (elements that can be stored without reallocation).
    /// For vector, capacity refers to back capacity (append capacity).
    [[nodiscard]] constexpr isize capacity() const { return size() + capacity_back(); }

    // factories
public:
    using base::create_copy_of;         // create deep copy from span
    using base::create_defaulted;       // create with default-constructed elements
    using base::create_filled;          // create with copies of a value
    using base::create_from_allocation; // create from existing allocation
    using base::create_uninitialized;   // create with uninitialized memory
    using base::create_with_capacity;   // create with reserved capacity

    // allocation management
public:
    using base::extract_allocation; // extract underlying allocation

    // modifiers - growth operations
public:
    using base::clear; // destroy all elements, size becomes 0

    using base::emplace_back;        // construct element at back (with allocation if needed)
    using base::emplace_back_stable; // construct element at back (requires capacity)
    using base::push_back;           // add element at back (with allocation if needed)
    using base::push_back_stable;    // add element at back (requires capacity)

    using base::pop_back;    // remove and return last element
    using base::remove_back; // remove last element (fast path, no return value)

    using base::pop_at;              // remove and return element at index (preserves order)
    using base::pop_at_unordered;    // remove and return element at index (O(1), does not preserve order)
    using base::remove_at;           // remove element at index (preserves order)
    using base::remove_at_unordered; // remove element at index (O(1), does not preserve order)

    // TODO: resize(isize new_size) - resize to new_size, default-construct new elements (should be in allocating_container)
    // TODO: resize(isize new_size, T const& value) - resize to new_size, copy-construct from value (should be in allocating_container)
    // TODO: reserve(isize capacity) - ensure capacity without changing size
    // TODO: shrink_to_fit() - reduce capacity to match size
    // TODO: assign(isize count, T const& value) - replace contents
    // TODO: assign(Iterator first, Iterator last) - replace contents from range
    // TODO: insert(Iterator pos, T const& value) - insert element at position
    // TODO: insert(Iterator pos, T&& value) - insert element at position
    // TODO: insert(Iterator pos, isize count, T const& value) - insert multiple copies
    // TODO: erase(Iterator pos) - remove element at position
    // TODO: erase(Iterator first, Iterator last) - remove range of elements
    // TODO: emplace(Iterator pos, Args&&... args) - construct element at position

    // vector has deep-copy value semantics
    vector() = default;
    ~vector() = default;
    vector(vector&&) = default;
    vector& operator=(vector&&) = default;
    vector(vector const&) = default;
    vector& operator=(vector const&) = default;

    friend base;
};

template <class T>
struct cc::unique_vector : private cc::allocating_container<T, vector<T>>
{
    using base = cc::allocating_container<T, vector<T>>;

    // element access
public:
    using base::operator[]; // access element by index
    using base::back;       // access last element
    using base::data;       // get pointer to underlying storage
    using base::front;      // access first element

    // iterators
public:
    using base::begin; // get pointer to first element
    using base::end;   // get pointer to one past last element

    // queries
public:
    using base::empty;      // check if vector is empty
    using base::size;       // get number of elements
    using base::size_bytes; // get total size in bytes

    // capacity queries
public:
    using base::capacity_back;         // get available capacity at back
    using base::has_capacity_back_for; // check if capacity exists for N elements at back

    /// Returns the total capacity (elements that can be stored without reallocation).
    /// For vector, capacity refers to back capacity (append capacity).
    [[nodiscard]] constexpr isize capacity() const { return size() + capacity_back(); }

    // factories
public:
    using base::create_copy_of;         // create deep copy from span
    using base::create_defaulted;       // create with default-constructed elements
    using base::create_filled;          // create with copies of a value
    using base::create_from_allocation; // create from existing allocation
    using base::create_uninitialized;   // create with uninitialized memory
    using base::create_with_capacity;   // create with reserved capacity

    // allocation management
public:
    using base::extract_allocation; // extract underlying allocation

    // modifiers - growth operations
public:
    using base::clear; // destroy all elements, size becomes 0

    using base::emplace_back;        // construct element at back (with allocation if needed)
    using base::emplace_back_stable; // construct element at back (requires capacity)
    using base::push_back;           // add element at back (with allocation if needed)
    using base::push_back_stable;    // add element at back (requires capacity)

    using base::pop_back;    // remove and return last element
    using base::remove_back; // remove last element (fast path, no return value)

    using base::pop_at;              // remove and return element at index (preserves order)
    using base::pop_at_unordered;    // remove and return element at index (O(1), does not preserve order)
    using base::remove_at;           // remove element at index (preserves order)
    using base::remove_at_unordered; // remove element at index (O(1), does not preserve order)

    // TODO: resize(isize new_size) - resize to new_size, default-construct new elements (should be in allocating_container)
    // TODO: resize(isize new_size, T const& value) - resize to new_size, copy-construct from value (should be in allocating_container)
    // TODO: reserve(isize capacity) - ensure capacity without changing size
    // TODO: shrink_to_fit() - reduce capacity to match size
    // TODO: assign(isize count, T const& value) - replace contents
    // TODO: assign(Iterator first, Iterator last) - replace contents from range
    // TODO: insert(Iterator pos, T const& value) - insert element at position
    // TODO: insert(Iterator pos, T&& value) - insert element at position
    // TODO: insert(Iterator pos, isize count, T const& value) - insert multiple copies
    // TODO: erase(Iterator pos) - remove element at position
    // TODO: erase(Iterator first, Iterator last) - remove range of elements
    // TODO: emplace(Iterator pos, Args&&... args) - construct element at position

    // unique_vector has move-only semantics
    unique_vector() = default;
    ~unique_vector() = default;
    unique_vector(unique_vector&&) = default;
    unique_vector& operator=(unique_vector&&) = default;
    unique_vector(unique_vector const&) = delete;
    unique_vector& operator=(unique_vector const&) = delete;

    friend base;
};
