#pragma once

#include <clean-core/allocation.hh>

#include <new>


/// Mixin implementing the common "contiguous container over cc::allocation<T>" surface area.
///
/// This is a CRTP-style helper: concrete containers privately inherit it as
/// `cc::allocating_container<T, Derived>`, then selectively re-expose members via `using`.
/// Example (abridged):
///
///     template<class T>
///     struct cc::array : private cc::allocating_container<T, array<T>> {
///         using base = cc::allocating_container<T, array<T>>;
///         using base::operator[];
///         using base::data;
///         using base::begin; using base::end;
///         using base::size;  using base::empty;
///         // ... array-specific policies / ctors ...
///     };
///
/// The goal is to provide a shared, consistent foundation for any container whose storage and
/// object lifetime is modeled by `cc::allocation<T>` (i.e. a live object subrange inside an owned
/// byte allocation). This centralizes the "boring but sharp" parts: indexing, iteration, size
/// queries, and allocation-aware factories / extraction, while keeping policy decisions in the
/// actual container type.
///
/// Concrete containers can tailor:
/// - growth behavior (no growth like `array`, back-only growth like `vector`, double-ended like `devector`)
/// - invariants and allowed operations (push/pop/resize/rebalance)
/// - copy semantics (keep/delete/replace the provided deep-copy defaults)
///
/// Capacity is expressed directionally (`capacity_front` / `capacity_back` and corresponding
/// `has_capacity_*_for`) because a single `capacity()` is semantically ambiguous across policies:
/// for a vector it typically means "append capacity", whereas for a devector it could mean a pooled
/// budget or a total-possible size anchored at `obj_start`. Containers are free to define their own
/// `capacity()` (or `capacity_total()`) in terms of the directional primitives if they want.
///
/// Member functions with the `_stable` suffix guarantee that they will never reallocate the buffer
/// or move/invalidate live objects. They keep existing references, pointers, and iterators stable.
/// These functions will typically assert that sufficient allocation capacity is already present.
///
///
/// === Exception & reference guarantees ===
///
/// Applies to all push/emplace operations (except _stable variants, which never allocate).
///
/// Allocation failures leave the container unchanged.
/// Capacity may increase even if a subsequent operation fails.
/// Element construction failures leave size and live range unchanged.
///
/// Reallocation always uses move construction (no copy fallback).
/// If a move throws during reallocation, the container remains structurally valid
/// (size, bounds, iteration correct), but some elements may be in moved-from state.
///
/// The old allocation remains valid until new elements are constructed.
/// Constructing from existing elements (e.g. `emplace_back(v[i])`) is safe during growth.
///
/// Any reallocation invalidates pointers, references, and iterators.
///
/// Design favors predictable behavior over preserving values when moves throw.
template <class T, class ContainerT>
struct cc::allocating_container
{
    using container_t = ContainerT;

    /// Minimum alignment used for heap allocations of this container.
    ///
    /// We align allocations to at least one destructive-interference unit (typically a cache line).
    /// Combined with rounding allocation sizes to multiples of this value, this ensures that
    /// distinct container allocations never share a cache line, eliminating allocator-induced
    /// false sharing between containers.
    /// This removes "spooky" cross-object performance interference while keeping alignment small
    /// enough to avoid systematic cache set aliasing that can occur with larger alignments.
    /// Larger-than-necessary alignment inside a single container (e.g. multiple elements per line)
    /// remains the programmer's responsibility by design.
    static constexpr isize alloc_alignment = cc::max(alignof(T), std::hardware_destructive_interference_size);

    /// Maximum extra slack allowed when growing an allocation.
    ///
    /// We cap allocator leeway to one OS page (4 KiB) so allocators that naturally
    /// round to page granularity can return a full page, without letting small
    /// allocations balloon uncontrollably.
    static constexpr isize alloc_max_slack = 4096;

    // element access
public:
    /// Returns a reference to the element at index i.
    /// Precondition: 0 <= i < size().
    [[nodiscard]] constexpr T& operator[](isize i)
    {
        auto const p_obj = _data.obj_start + i;
        CC_ASSERT(_data.obj_start <= p_obj && p_obj < _data.obj_end, "index out of bounds");
        return *p_obj;
    }
    [[nodiscard]] constexpr T const& operator[](isize i) const
    {
        auto const p_obj = _data.obj_start + i;
        CC_ASSERT(_data.obj_start <= p_obj && p_obj < _data.obj_end, "index out of bounds");
        return *p_obj;
    }

    /// Returns a reference to the first element.
    /// Precondition: !empty().
    [[nodiscard]] constexpr T& front()
    {
        CC_ASSERT(_data.obj_start < _data.obj_end, "allocation is empty");
        return *_data.obj_start;
    }
    [[nodiscard]] constexpr T const& front() const
    {
        CC_ASSERT(_data.obj_start < _data.obj_end, "allocation is empty");
        return *_data.obj_start;
    }

    /// Returns a reference to the last element.
    /// Precondition: !empty().
    [[nodiscard]] constexpr T& back()
    {
        CC_ASSERT(_data.obj_start < _data.obj_end, "allocation is empty");
        return *(_data.obj_end - 1);
    }
    [[nodiscard]] constexpr T const& back() const
    {
        CC_ASSERT(_data.obj_start < _data.obj_end, "allocation is empty");
        return *(_data.obj_end - 1);
    }

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
    [[nodiscard]] constexpr isize size() const { return _data.obj_end - _data.obj_start; }
    /// Returns the total size in bytes of all elements in the array.
    [[nodiscard]] constexpr isize size_bytes() const { return (_data.obj_end - _data.obj_start) * sizeof(T); }
    /// Returns true if size() == 0.
    [[nodiscard]] constexpr bool empty() const { return _data.obj_start == _data.obj_end; }

    /// How many elements can be inserted at the front (via push_front) without reallocation.
    /// Computed as the number of whole sizeof(T) slots between alloc_start and obj_start,
    [[nodiscard]] constexpr isize capacity_front() const
    {
        // Note: pointer arithmetic is well-defined for the "all nullptr" case because the C++ standard
        //       explicitly defines nullptr - nullptr == 0 (no UB)
        auto const front_bytes = (cc::byte const*)_data.obj_start - _data.alloc_start;
        return front_bytes / sizeof(T); // floor
    }

    /// How many elements can be inserted at the back (via push_back) without reallocation.
    /// Computed as the number of whole sizeof(T) slots between obj_end and alloc_end.
    [[nodiscard]] constexpr isize capacity_back() const
    {
        // Note: pointer arithmetic is well-defined for the "all nullptr" case because the C++ standard
        //       explicitly defines nullptr - nullptr == 0 (no UB)
        auto const back_bytes = _data.alloc_end - (cc::byte const*)_data.obj_end;
        return back_bytes / sizeof(T); // floor
    }

    /// Cheap predicate: do we have room to grow the live range by `count` elements at the front
    /// without reallocation?
    [[nodiscard]] constexpr bool has_capacity_front_for(isize count) const
    {
        // Note: pointer arithmetic is well-defined for the "all nullptr" case because the C++ standard
        //       explicitly defines nullptr - nullptr == 0 (no UB)
        auto const front_bytes = (cc::byte const*)_data.obj_start - _data.alloc_start;
        return front_bytes >= count * isize(sizeof(T));
    }

    /// Cheap predicate: do we have room to grow the live range by `count` elements at the back
    /// without reallocation?
    [[nodiscard]] constexpr bool has_capacity_back_for(isize count) const
    {
        // Note: pointer arithmetic is well-defined for the "all nullptr" case because the C++ standard
        //       explicitly defines nullptr - nullptr == 0 (no UB)
        auto const back_bytes = _data.alloc_end - (cc::byte const*)_data.obj_end;
        return back_bytes >= count * isize(sizeof(T));
    }

    // resizing
public:
    // Computes the next allocation size when growing the container.
    //
    // Uses exponential growth (doubling) to ensure amortized constant-time growth,
    // then rounds up to the cache-line alignment used by this container.
    // This preserves allocator- and container-level guarantees against
    // cross-allocation false sharing while avoiding frequent small reallocations.
    [[nodiscard]] static constexpr isize alloc_grow_size_for(isize curr_size, isize min_size)
    {
        return cc::align_up(cc::max(curr_size << 1, min_size), alloc_alignment);
    }

    // destroys the live object range, so that obj_start == obj_end afterwards
    // calls all destructors, does not move obj_start
    constexpr void clear()
    {
        impl::destroy_objects_in_reverse(_data.obj_start, _data.obj_end);
        _data.obj_end = _data.obj_start;
    }

    // appends
public:
    /// Constructs a new element at the back using existing capacity.
    /// Requires `has_capacity_back_for(1)` to be true; caller must ensure capacity in advance.
    /// No allocation occurs; pointers, references, and iterators remain valid (stable operation).
    /// Strong exception safety; O(1) complexity.
    /// Low-level primitive for performance-critical or reference-sensitive code.
    template <class... Args>
    constexpr T& emplace_back_stable(Args&&... args)
    {
        static_assert(
            requires { T(cc::forward<Args>(args)...); }, "emplace_back_stable: T is not constructible from "
                                                         "the provided argument types");
        CC_ASSERT(has_capacity_back_for(1), "not enough capacity for emplace_back_stable");
        auto const p = new (cc::placement_new, _data.obj_end) T(cc::forward<Args>(args)...);
        _data.obj_end++; // _after_ so exceptions in T(...) leave the state valid
        return *p;
    }

    /// Copy-constructs a new element at the back using existing capacity.
    /// Requires `has_capacity_back_for(1)` to be true; caller must ensure capacity in advance.
    /// No allocation occurs; pointers, references, and iterators remain valid (stable operation).
    /// Strong exception safety; O(1) complexity.
    /// Low-level primitive for performance-critical or reference-sensitive code.
    constexpr T& push_back_stable(T const& value) { return emplace_back_stable(value); }

    /// Move-constructs a new element at the back using existing capacity.
    /// Requires `has_capacity_back_for(1)` to be true; caller must ensure capacity in advance.
    /// No allocation occurs; pointers, references, and iterators remain valid (stable operation).
    /// Strong exception safety; O(1) complexity.
    /// Low-level primitive for performance-critical or reference-sensitive code.
    constexpr T& push_back_stable(T&& value) { return emplace_back_stable(cc::move(value)); }

    /// Constructs a new element at the front using existing capacity.
    /// Requires `has_capacity_front_for(1)` to be true; caller must ensure capacity in advance.
    /// No allocation occurs; pointers, references, and iterators remain valid (stable operation).
    /// Strong exception safety; O(1) complexity.
    /// Low-level primitive for performance-critical or reference-sensitive code.
    template <class... Args>
    constexpr T& emplace_front_stable(Args&&... args)
    {
        static_assert(
            requires { T(cc::forward<Args>(args)...); }, "emplace_front_stable: T is not constructible from "
                                                         "the provided argument types");
        CC_ASSERT(has_capacity_front_for(1), "not enough capacity for emplace_front_stable");
        auto const p = new (cc::placement_new, _data.obj_start - 1) T(cc::forward<Args>(args)...);
        _data.obj_start--; // _after_ so exceptions in T(...) leave the state valid
        return *p;
    }

    /// Copy-constructs a new element at the front using existing capacity.
    /// Requires `has_capacity_front_for(1)` to be true; caller must ensure capacity in advance.
    /// No allocation occurs; pointers, references, and iterators remain valid (stable operation).
    /// Strong exception safety; O(1) complexity.
    /// Low-level primitive for performance-critical or reference-sensitive code.
    constexpr T& push_front_stable(T const& value) { return emplace_front_stable(value); }

    /// Move-constructs a new element at the front using existing capacity.
    /// Requires `has_capacity_front_for(1)` to be true; caller must ensure capacity in advance.
    /// No allocation occurs; pointers, references, and iterators remain valid (stable operation).
    /// Strong exception safety; O(1) complexity.
    /// Low-level primitive for performance-critical or reference-sensitive code.
    constexpr T& push_front_stable(T&& value) { return emplace_front_stable(cc::move(value)); }

    /// Ensures capacity to add count elements at the back, allocating if necessary.
    /// Returns a pointer to obj_end that can be used for construction and direct increment.
    ///
    /// Performance design:
    /// This function is marked CC_COLD_FUNC and should only be called from [[unlikely]] branches.
    /// The happy path (sufficient capacity) avoids function calls entirely, allowing the compiler
    /// to inline T(...) construction without any indirection.
    ///
    /// Usage pattern (begin/finalize sandwich):
    /// The returned pointer points to either &_data.obj_end (if realloc succeeded) or
    /// &new_allocation.obj_end (if full reallocation needed). This allows directly incrementing
    /// obj_end after each construction, writing through to the correct allocation.
    ///
    /// Example usage:
    ///   allocation<T> new_allocation;
    ///   auto p_obj_end = &_data.obj_end;
    ///
    ///   if (!has_capacity_back_for(1)) [[unlikely]]
    ///       p_obj_end = ensure_capacity_back_begin(new_allocation, 1);
    ///
    ///   // Construct new object into active alloc
    ///   // Do this BEFORE moving other objects because construction might reference them
    ///   // and so that a throwing T(...) doesn't break the container
    ///   // (an exception here means the empty new_allocation is cleaned up and container is untouched)
    ///   // NOTE: single syntactic construction site here (no branches) helps the inliner be more aggressive
    ///   auto const p = new (cc::placement_new, *p_obj_end) T(...);
    ///   (*p_obj_end)++; // _after_ so exceptions in T(...) leave state valid
    ///
    ///   if (new_allocation.is_valid()) [[unlikely]]
    ///       ensure_capacity_back_finalize(new_allocation);
    CC_COLD_FUNC [[nodiscard]] constexpr T** ensure_capacity_back_begin(allocation<T>& new_allocation, isize count)
    {
        CC_ASSERT(!has_capacity_back_for(count), "only call this if we don't have enough capacity");

        // exponential growth strategy, at least sizeof(T) more
        auto const new_size_request_min
            = allocating_container::alloc_grow_size_for(_data.alloc_size_bytes() + sizeof(T) * count);
        auto const new_size_request_max = new_size_request_min + cc::min(new_size_request_min, alloc_max_slack);

        // try realloc first
        if (_data.try_resize_alloc(new_size_request_min, new_size_request_max))
            return &_data.obj_end;

        // otherwise we need a full new allocation
        // TODO: think about re-center logic for cc::devector
        new_allocation = cc::allocation<T>::create_empty_bytes(new_size_request_min, new_size_request_max,
                                                               alloc_alignment, _data.custom_resource);

        // Construct new elements where they would be in the new allocation (after old elements)
        // The old allocation remains valid during construction phase
        // The new allocation's live range tracks only newly-constructed elements for exception safety:
        // If we add multiple elements and construction throws, only the successfully constructed
        // new elements (e.g. first k of push_back_range when at k+1) need cleanup via new_allocation's dtor
        // Thus the live range semantically starts behind the old allocation's live range
        // In finalize, when we move over old elements, we extend it to the full allocation
        new_allocation.obj_start = new_allocation.obj_start + size();
        new_allocation.obj_end = new_allocation.obj_start;
        return &new_allocation.obj_end;
    }

    /// Finalizes the back capacity operation after elements have been constructed.
    /// PRECONDITION: new_allocation must be valid (i.e., full reallocation occurred, not just realloc).
    /// Moves old elements into the new allocation and replaces _data.
    /// The obj_end has already been updated via the pointer returned by ensure_capacity_back_begin.
    CC_COLD_FUNC constexpr void ensure_capacity_back_finalize(allocation<T>& new_allocation)
    {
        CC_ASSERT(new_allocation.is_valid(), "only call this when we have a temporary alloc");

        // Move over old elements in reverse order for exception safety with throwing move ctors
        // new_allocation.obj_start points to where the old elements should go (before newly constructed elements)
        // We move from _data in reverse: last element first, first element last
        // This is exception safe: if a move throws, new_allocation contains a valid contiguous range
        // (newly constructed elements at the end + successfully moved old elements in front)
        // Note: We don't decrement _data.obj_end during the move - moves in C++ are non-destructive
        // The old allocation remains in a moved-from but valid state, cleaned up when _data is destroyed below
        impl::move_create_objects_to_reverse(new_allocation.obj_start, _data.obj_start, _data.obj_end);

        // Replace the current allocation
        // This destroys _data (cleaning up the moved-from old elements) and adopts new_allocation
        _data = cc::move(new_allocation);
    }

    /// Appends a new element to the back, allocating if necessary.
    /// If has_capacity_back_for(1) is true, no invalidation of any kind occurs.
    /// Otherwise, see "Exception & reference guarantees" section above.
    /// Amortized O(1) complexity.
    template <class... Args>
    constexpr T& emplace_back(Args&&... args)
    {
        static_assert(
            requires { T(cc::forward<Args>(args)...); }, "emplace_back: T is not constructible from "
                                                         "the provided argument types");

        allocation<T> new_allocation;
        auto p_obj_end = &_data.obj_end;

        if (!has_capacity_back_for(1)) [[unlikely]]
            p_obj_end = ensure_capacity_back_begin(new_allocation, 1);

        auto const p = new (cc::placement_new, *p_obj_end) T(cc::forward<Args>(args)...);
        (*p_obj_end)++; // _after_ so exceptions in T(...) leave state valid

        if (new_allocation.is_valid()) [[unlikely]]
            ensure_capacity_back_finalize(new_allocation);

        return *p;
    }

    /// Appends a copy of the element to the back.
    /// See emplace_back for guarantees and complexity.
    constexpr T& push_back(T const& value) { return emplace_back(value); }

    /// Appends an element to the back via move.
    /// See emplace_back for guarantees and complexity.
    constexpr T& push_back(T&& value) { return emplace_back(cc::move(value)); }

    // removals
public:
    /// Removes and returns the last element by move.
    /// Precondition: !empty().
    /// O(1) complexity.
    /// NOTE: Prefer remove_back() if you don't need the return value (avoids an extra move).
    [[nodiscard("use remove_back() if you don't need the return value")]] constexpr T pop_back()
    {
        CC_ASSERT(_data.obj_start < _data.obj_end, "cannot pop from empty container");
        auto value = cc::move(*(_data.obj_end - 1));
        (_data.obj_end - 1)->~T();
        _data.obj_end--;
        return value;
    }

    /// Removes and returns the first element by move.
    /// Precondition: !empty().
    /// O(1) complexity.
    /// NOTE: Prefer remove_front() if you don't need the return value (avoids an extra move).
    [[nodiscard("use remove_front() if you don't need the return value")]] constexpr T pop_front()
    {
        CC_ASSERT(_data.obj_start < _data.obj_end, "cannot pop from empty container");
        auto value = cc::move(*_data.obj_start);
        _data.obj_start->~T();
        _data.obj_start++;
        return value;
    }

    /// Removes the last element.
    /// Precondition: !empty().
    /// O(1) complexity.
    /// Fast path: destroys the element in place without moving.
    constexpr void remove_back()
    {
        CC_ASSERT(_data.obj_start < _data.obj_end, "cannot remove from empty container");
        _data.obj_end--;
        _data.obj_end->~T();
    }

    /// Removes the first element.
    /// Precondition: !empty().
    /// O(1) complexity.
    /// Fast path: destroys the element in place without moving.
    constexpr void remove_front()
    {
        CC_ASSERT(_data.obj_start < _data.obj_end, "cannot remove from empty container");
        _data.obj_start->~T();
        _data.obj_start++;
    }

    /// Removes and returns the element at the given index by move.
    /// Precondition: 0 <= idx < size().
    /// O(n) complexity due to element compaction.
    /// NOTE: Prefer remove_at() if you don't need the return value (avoids an extra move).
    [[nodiscard("use remove_at() if you don't need the return value")]] constexpr T pop_at(isize idx)
    {
        auto const p_obj = _data.obj_start + idx;
        CC_ASSERT(_data.obj_start <= p_obj && p_obj < _data.obj_end, "index out of bounds");

        // Move out the value before compacting
        auto value = cc::move(*p_obj);

        // Compact remaining elements backward (move-assigns into p_obj, which is now moved-from but alive)
        impl::compact_move_objects_backward(p_obj, p_obj + 1, _data.obj_end);

        // The last element is now in moved-from state; destroy it and shrink
        _data.obj_end--;
        _data.obj_end->~T();

        return value;
    }

    /// Removes the element at the given index.
    /// Precondition: 0 <= idx < size().
    /// O(n) complexity due to element compaction.
    /// Fast path: compacts elements without an extra move for the return value.
    constexpr void remove_at(isize idx)
    {
        auto const p_obj = _data.obj_start + idx;
        CC_ASSERT(_data.obj_start <= p_obj && p_obj < _data.obj_end, "index out of bounds");

        // Compact remaining elements backward (move-assigns over p_obj)
        impl::compact_move_objects_backward(p_obj, p_obj + 1, _data.obj_end);

        // The last element is now in moved-from state; destroy it and shrink
        _data.obj_end--;
        _data.obj_end->~T();
    }

    /// Removes and returns the element at the given index by swapping with the last element.
    /// Does not preserve relative order of elements (hence _unordered suffix).
    /// Precondition: 0 <= idx < size().
    /// O(1) complexity. All references remain valid except for the last element.
    /// Preferred over pop_at() when element order doesn't matter.
    /// NOTE: Prefer remove_at_unordered() if you don't need the return value (avoids an extra move).
    [[nodiscard("use remove_at_unordered() if you don't need the return value")]] constexpr T pop_at_unordered(isize idx)
    {
        auto const p_obj = _data.obj_start + idx;
        CC_ASSERT(_data.obj_start <= p_obj && p_obj < _data.obj_end, "index out of bounds");

        // Move out the value at idx
        auto value = cc::move(*p_obj);

        // Move the last element into the gap (unless we're removing the last element)
        _data.obj_end--;
        if (p_obj != _data.obj_end)
            *p_obj = cc::move(*_data.obj_end);

        // Destroy the last element (now either moved-from or the original at idx if it was last)
        _data.obj_end->~T();

        return value;
    }

    /// Removes the element at the given index by swapping with the last element.
    /// Does not preserve relative order of elements (hence _unordered suffix).
    /// Precondition: 0 <= idx < size().
    /// O(1) complexity. All references remain valid except for the last element.
    /// Preferred over remove_at() when element order doesn't matter.
    /// Fast path: avoids the extra move required by pop_at_unordered().
    constexpr void remove_at_unordered(isize idx)
    {
        auto const p_obj = _data.obj_start + idx;
        CC_ASSERT(_data.obj_start <= p_obj && p_obj < _data.obj_end, "index out of bounds");

        // Move the last element into the gap (unless we're removing the last element)
        _data.obj_end--;
        if (p_obj != _data.obj_end)
            *p_obj = cc::move(*_data.obj_end);

        // Destroy the last element (now either moved-from or the original at idx if it was last)
        _data.obj_end->~T();
    }

    // ctors / allocation
public:
    // array directly from a previous allocation
    // simply treats the live objects as the array
    [[nodiscard]] static container_t create_from_allocation(cc::allocation<T> data)
    {
        container_t c;
        c._data = cc::move(data);
        return c;
    }

    // initializes a new container_t with "size" many defaulted elements
    [[nodiscard]] static container_t create_defaulted(size_t size, cc::memory_resource const* resource = nullptr)
    {
        auto const byte_size = cc::align_up(size * sizeof(T), alloc_alignment);
        auto result = cc::allocation<T>::create_empty_bytes(byte_size, byte_size, alloc_alignment, resource);
        impl::default_create_objects_to(result.obj_end, size);
        return container_t::create_from_allocation(cc::move(result));
    }

    // initializes a new container_t with "size" many elements, all copy-constructed from "value"
    [[nodiscard]] static container_t create_filled(size_t size,
                                                   T const& value,
                                                   cc::memory_resource const* resource = nullptr)
    {
        auto const byte_size = cc::align_up(size * sizeof(T), alloc_alignment);
        auto result = cc::allocation<T>::create_empty_bytes(byte_size, byte_size, alloc_alignment, resource);
        impl::fill_create_objects_to(result.obj_end, size, value);
        return container_t::create_from_allocation(cc::move(result));
    }

    // initializes a new container_t with "size" many uninitialized elements (only safe for trivial types)
    [[nodiscard]] static container_t create_uninitialized(size_t size, cc::memory_resource const* resource = nullptr)
    {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable for uninitialized allocation");
        static_assert(std::is_trivially_destructible_v<T>, "T must be trivially destructible for uninitialized "
                                                           "allocation");

        auto const byte_size = cc::align_up(size * sizeof(T), alloc_alignment);
        auto result = cc::allocation<T>::create_empty_bytes(byte_size, byte_size, alloc_alignment, resource);
        result.obj_end = result.obj_start + size;
        return container_t::create_from_allocation(cc::move(result));
    }

    // creates a deep copy of the provided span
    [[nodiscard]] static container_t create_copy_of(cc::span<T const> source,
                                                    cc::memory_resource const* resource = nullptr)
    {
        auto const byte_size = cc::align_up(source.size() * sizeof(T), alloc_alignment);
        auto result = cc::allocation<T>::create_empty_bytes(byte_size, byte_size, alloc_alignment, resource);
        impl::copy_create_objects_to(result.obj_end, source.data(), source.data() + source.size());
        return container_t::create_from_allocation(cc::move(result));
    }

    // initializes a new container_t with reserved capacity but no live objects
    // guarantees at least "capacity" elements can be inserted without reallocation
    // actual capacity may be larger due to cache-line alignment (alloc_alignment)
    [[nodiscard]] static container_t create_with_capacity(size_t capacity, cc::memory_resource const* resource = nullptr)
    {
        auto const byte_size = cc::align_up(capacity * sizeof(T), alloc_alignment);
        return container_t::create_from_allocation(
            cc::allocation<T>::create_empty_bytes(byte_size, byte_size, alloc_alignment, resource));
    }

    allocating_container() = default;
    ~allocating_container() = default;

    // move semantics are already fine via cc::allocation
    allocating_container(allocating_container&&) = default;
    allocating_container& operator=(allocating_container&&) = default;

    // deep copy semantics
    // containers that use this mix-in can simply delete their copy ctor if they do not want it
    allocating_container(allocating_container const& rhs)
    {
        auto const byte_size = cc::align_up(rhs.size() * sizeof(T), alloc_alignment);
        _data = cc::allocation<T>::create_empty_bytes(byte_size, byte_size, alloc_alignment, rhs._data.custom_resource);
        cc::impl::copy_create_objects_to(_data.obj_end, rhs._data.obj_start, rhs._data.obj_end);
    }
    allocating_container& operator=(allocating_container const& rhs)
    {
        if (this != &rhs)
        {
            auto const byte_size = cc::align_up(rhs.size() * sizeof(T), alloc_alignment);
            auto new_data = cc::allocation<T>::create_empty_bytes(byte_size, byte_size, alloc_alignment,
                                                                  _data.custom_resource); // keep lhs resource
            cc::impl::copy_create_objects_to(new_data.obj_end, rhs._data.obj_start, rhs._data.obj_end);
            _data = cc::move(new_data);
        }
        return *this;
    }

    /// Extracts and returns the underlying allocation, leaving the allocating_container empty.
    /// The returned `cc::allocation<T>` owns the backing storage and live objects.
    /// The allocating_container retains its memory resource for future use.
    /// Enables zero-copy interop with other contiguous containers.
    /// Complexity: O(1).
    cc::allocation<T> extract_allocation() { return cc::move(_data); }

private:
    cc::allocation<T> _data;
};
