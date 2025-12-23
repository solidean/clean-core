#pragma once

#include <clean-core/impl/allocating_container.hh>


// TODO:
// - sequence entry points
// - retyping APIs
// - resize APIs? -> would be totally fine I think
// - equality, order, hashing


template <class T>
struct cc::unique_array : private cc::allocating_container<T, array<T>>
{
    using base = cc::allocating_container<T, array<T>>;

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
    using base::empty;      // check if array is empty
    using base::size;       // get number of elements
    using base::size_bytes; // get total size in bytes

    // factories
public:
    using base::create_copy_of;         // create deep copy from span
    using base::create_defaulted;       // create with default-constructed elements
    using base::create_filled;          // create with copies of a value
    using base::create_from_allocation; // create from existing allocation
    using base::create_uninitialized;   // create with uninitialized memory

    // allocation management
public:
    using base::extract_allocation; // extract underlying allocation

    // unique_array has move-only semantics
    using base::allocating_container; // inherit constructors (including initializer_list)
    unique_array() = default;
    ~unique_array() = default;
    unique_array(unique_array&&) = default;
    unique_array& operator=(unique_array&&) = default;
    unique_array(unique_array const&) = delete;
    unique_array& operator=(unique_array const&) = delete;

    friend base;
};
