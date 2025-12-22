#pragma once

#include <clean-core/fwd.hh>
#include <clean-core/utility.hh>


namespace cc
{
/// Default memory resource used when allocation::custom_resource == nullptr.
/// This is a system allocator stored in the data segment, making the pointer valid even during
/// static initialization in other translation units (safe for use in global/static constructors).
extern cc::memory_resource const* const default_memory_resource;
} // namespace cc

// future: move to a impl helper header?
namespace cc::impl
{
/// Calls destructors on [start, end) in reverse order.
/// Empty ranges (start == end) and nullptr are valid and result in a no-op.
/// Trivially destructible types are optimized out at compile time.
template <class T>
constexpr void destroy_objects_in_reverse(T* start, T* end)
{
    static_assert(sizeof(T) > 0, "T must be a complete type (did you forget to include a header?)");

    if constexpr (!std::is_trivially_destructible_v<T>)
    {
        while (end != start)
        {
            --end;
            end->~T();
        }
    }
}

/// Default-constructs a count of objects using placement new.
/// dest_end is incremented for each successfully constructed object.
/// IMPORTANT: Assumes the objects at [*dest_end, *dest_end + count) are NOT yet constructed
/// (uninitialized memory). This function initializes the lifetime of objects starting at *dest_end. If default
/// construction throws, dest_end points to the element that threw (not yet constructed). If no exception occurs,
/// dest_end is updated to one past the last constructed object. count == 0 is valid and results in a no-op.
/// All objects are properly initialized via T(), which ensures zero-initialization for trivial types (e.g., int).
///
/// Usage pattern:
///   auto obj_start = (T*)uninitialized_memory;
///   auto obj_end = obj_start;
///   default_create_objects_to(obj_end, count);
///   // [obj_start, obj_end) is now the constructed live range
template <class T>
constexpr void default_create_objects_to(T*& dest_end, isize count)
{
    static_assert(sizeof(T) > 0, "T must be a complete type (did you forget to include a header?)");
    static_assert(std::is_default_constructible_v<T>, "T must be default constructible");

    // Always construct with T() to ensure proper initialization
    // (for trivial types like int, this ensures zero-initialization)
    for (isize i = 0; i < count; ++i)
    {
        new (cc::placement_new, dest_end) T();
        ++dest_end;
    }
}

/// Fill-constructs a count of objects by copy-constructing from a single value using placement new.
/// dest_end is incremented for each successfully constructed object.
/// IMPORTANT: Assumes the objects at [*dest_end, *dest_end + count) are NOT yet constructed
/// (uninitialized memory). This function initializes the lifetime of objects starting at *dest_end. If copy
/// construction throws, dest_end points to the element that threw (not yet constructed). If no exception occurs,
/// dest_end is updated to one past the last constructed object. count == 0 is valid and results in a no-op.
///
/// Usage pattern:
///   auto obj_start = (T*)uninitialized_memory;
///   auto obj_end = obj_start;
///   fill_create_objects_to(obj_end, count, value);
///   // [obj_start, obj_end) is now the constructed live range, each element a copy of value
template <class T>
constexpr void fill_create_objects_to(T*& dest_end, isize count, T const& value)
{
    static_assert(sizeof(T) > 0, "T must be a complete type (did you forget to include a header?)");
    static_assert(std::is_copy_constructible_v<T>, "T must be copy constructible");

    for (isize i = 0; i < count; ++i)
    {
        new (cc::placement_new, dest_end) T(value);
        ++dest_end;
    }
}

/// Copy-constructs objects from [src_start, src_end) using placement new.
/// dest_end is incremented for each successfully constructed object.
/// IMPORTANT: Assumes the objects at [*dest_end, *dest_end + (src_end - src_start)) are NOT yet constructed
/// (uninitialized memory). This function initializes the lifetime of objects starting at *dest_end. If copy
/// construction throws, dest_end points to the element that threw (not yet constructed). If no exception occurs,
/// dest_end is updated to one past the last constructed object. Empty ranges (src_start == src_end) and nullptr are
/// valid and result in a no-op. Trivially copyable types are optimized to use memcpy at compile time.
///
/// Usage pattern:
///   auto obj_start = (T*)uninitialized_memory;
///   auto obj_end = obj_start;
///   copy_create_objects_to(obj_end, src, src + count);
///   // [obj_start, obj_end) is now the constructed live range
template <class T>
constexpr void copy_create_objects_to(T*& dest_end, T const* src_start, T const* src_end)
{
    static_assert(sizeof(T) > 0, "T must be a complete type (did you forget to include a header?)");
    static_assert(std::is_copy_constructible_v<T>, "T must be copy constructible");

    if constexpr (std::is_trivially_copyable_v<T>)
    {
        auto const size = src_end - src_start;
        if (size > 0)
        {
            cc::memcpy(dest_end, src_start, size * sizeof(T));
            dest_end += size;
        }
    }
    else
    {
        while (src_start != src_end)
        {
            new (cc::placement_new, dest_end) T(*src_start);
            ++dest_end;
            ++src_start;
        }
    }
}

/// Copy-assigns objects from [src_start, src_end) using copy assignment operator.
/// dest_end is incremented for each successfully assigned object.
/// IMPORTANT: Assumes the objects at [*dest_end, *dest_end + (src_end - src_start)) are already constructed (alive).
/// If copy assignment throws, dest_end points to the element that threw (partially modified state).
/// If no exception occurs, dest_end is updated to one past the last assigned object.
/// Empty ranges (src_start == src_end) and nullptr are valid and result in a no-op.
/// Trivially copyable types are optimized to use memcpy at compile time.
///
/// Usage pattern:
///   auto obj_end = obj_start;
///   copy_assign_objects_to(obj_end, src, src + count);
///   // [obj_start, obj_end) is now the validly assigned range
template <class T>
constexpr void copy_assign_objects_to(T*& dest_end, T const* src_start, T const* src_end)
{
    static_assert(sizeof(T) > 0, "T must be a complete type (did you forget to include a header?)");
    static_assert(std::is_copy_assignable_v<T>, "T must be copy assignable");

    if constexpr (std::is_trivially_copyable_v<T>)
    {
        auto const size = src_end - src_start;
        if (size > 0)
        {
            cc::memcpy(dest_end, src_start, size * sizeof(T));
            dest_end += size;
        }
    }
    else
    {
        while (src_start != src_end)
        {
            *dest_end = *src_start;
            ++dest_end;
            ++src_start;
        }
    }
}

/// Fill-assigns a count of objects by copy-assigning from a single value using copy assignment operator.
/// dest_end is incremented for each successfully assigned object.
/// IMPORTANT: Assumes the objects at [*dest_end, *dest_end + count) are already constructed (alive).
/// If copy assignment throws, dest_end points to the element that threw (partially modified state).
/// If no exception occurs, dest_end is updated to one past the last assigned object.
/// count == 0 is valid and results in a no-op.
///
/// Usage pattern:
///   auto obj_end = obj_start;
///   fill_assign_objects_to(obj_end, count, value);
///   // [obj_start, obj_end) is now the validly assigned range, each element a copy of value
template <class T>
constexpr void fill_assign_objects_to(T*& dest_end, isize count, T const& value)
{
    static_assert(sizeof(T) > 0, "T must be a complete type (did you forget to include a header?)");
    static_assert(std::is_copy_assignable_v<T>, "T must be copy assignable");

    for (isize i = 0; i < count; ++i)
    {
        *dest_end = value;
        ++dest_end;
    }
}
} // namespace cc::impl

/// Polymorphic memory resource interface powering cc::allocation<T>.
/// Custom allocators implement this interface to provide pluggable allocation strategies.
/// The design favors explicit size/alignment tracking and non-movable in-place resize over realloc.
/// This is a POD struct using function pointers to avoid virtual dispatch and non-trivial constructors.
struct cc::memory_resource
{
    /// Allocate `bytes` with at least `alignment` alignment, never returning null for non-zero requests.
    /// bytes == 0 always returns nullptr.
    /// bytes > 0 always returns non-null; failure is fatal (assert/terminate) or throws.
    cc::function_ptr<cc::byte*(isize bytes, isize alignment, void* userdata)> allocate_bytes = nullptr;

    /// Attempt to allocate `bytes` with at least `alignment` alignment, returning nullptr on failure.
    /// bytes == 0 always returns nullptr.
    /// bytes > 0 may return nullptr to signal allocation was not possible.
    /// This provides an escape hatch for callers that must handle allocation failure explicitly.
    /// Implementations should prefer returning nullptr over fatal failure when feasible (best-effort).
    /// Wrappers are still permitted to fatally fail rather than return nullptr.
    cc::function_ptr<cc::byte*(isize bytes, isize alignment, void* userdata)> try_allocate_bytes = nullptr;

    /// Deallocate a block previously obtained from this resource with matching bytes and alignment.
    /// `p` must be the exact pointer returned by allocate_bytes or try_allocate_bytes.
    /// `bytes` and `alignment` must match the values used during allocation.
    /// Noexcept in spirit: only programmer bugs (e.g., mismatched size) may throw or terminate.
    /// Allocator exhaustion itself must not throw; containers may leak memory if this throws.
    cc::function_ptr<void(cc::byte* p, isize bytes, isize alignment, void* userdata)> deallocate_bytes = nullptr;

    /// Attempt to resize an existing allocation in place without moving or freeing it.
    /// This optimization hook enables contiguous containers to grow/shrink without invalidating iterators.
    /// The resize range [min_bytes, max_bytes] lets the resource choose any size fitting internal constraints.
    ///
    /// Preconditions:
    /// `p` was allocated from this resource with `old_bytes` and `alignment`.
    /// `1 <= min_bytes <= max_bytes`.
    ///
    /// Success (returns new_bytes in [min_bytes, max_bytes]):
    /// The allocation remains at address `p` (no move).
    /// The first min(old_bytes, new_bytes) bytes are preserved.
    /// The returned size becomes the canonical size for future resize/deallocate calls.
    ///
    /// Failure (returns -1):
    /// The allocation remains valid and unchanged at `p` with size `old_bytes`.
    /// Ownership remains with the caller; the resource does not free or invalidate `p`.
    ///
    /// Supports both growth (min_bytes > old_bytes) and shrink (max_bytes < old_bytes).
    /// Shrink success does not guarantee memory returned to OS; only updates logical allocation size.
    /// Prefer returning the smallest representable size >= min_bytes to minimize memory waste.
    /// `alignment` cannot be increased; it matches the original allocation.
    ///
    /// Rationale: Traditional realloc may move and implicitly free the original allocation.
    /// This is unsafe for containers where element addresses must remain stable during reallocation.
    /// Example: vector::push_back(vec[0]) where the source aliases the container's storage.
    ///
    /// Noexcept in spirit: only programmer bugs (e.g., mismatched old_bytes) may throw or terminate.
    /// Allocator exhaustion itself must not throw; containers may leak memory if this throws.
    cc::function_ptr<isize(cc::byte* p, isize old_bytes, isize min_bytes, isize max_bytes, isize alignment, void* userdata)>
        try_resize_bytes_in_place = nullptr;

    /// User-defined data for custom allocators. Can be nullptr for stateless allocators.
    void* userdata = nullptr;
};

/// Owning allocation handle for a contiguous byte block plus a typed "live window" inside it.
///
/// Design goals:
/// - Separate "what memory do we own?" from "which objects are currently alive in it?".
/// - Support pooling / reuse without reallocating by keeping the underlying byte allocation intact.
/// - Enable zero-copy interop between contiguous owning containers (adopt/release), and safe fallible
///   re-interpretation (retype) for trivially copyable payloads when the live window is cleared.
///
/// This is intentionally more expressive than the classic (ptr, size, capacity) triple:
/// - We keep the original allocation bounds (alloc_start/alloc_end) so we can round-trip capacity
///   across retypes and support realignment by moving obj_start/obj_end within the allocation.
/// - We treat capacity as an implicit property derived from the remaining bytes between obj_end and
///   alloc_end (and, for front-growth containers, between alloc_start and obj_start).
///
/// Philosophy:
/// - All owning containers that store their elements contiguously should be built on cc::allocation<T>.
///   This includes the main containers: array<T>, vector<T>, and devector<T> (double-ended vector).
/// - Sharing this handle gives those containers a uniform "escape hatch" story: allocations can be
///   adopted, released, retyped, and transferred across container types without copying.
/// - Non-owning views (span<T> / fixed_span<T, N>) are the common interop surface: contiguous containers
///   can usually expose span<T> views of their live window.
///
/// Fixed-size container variants:
/// - fixed_* containers carry their extent/capacity as template argument N and store data inline.
/// - They reuse the same object-lifetime helpers (construct/destroy/commit patterns) but do not use
///   cc::allocation<T> since there is no heap allocation to adopt/release; copying/moving is explicit.
/// - fixed_array<T, N> is fully alive and can provide fixed_span<T, N> in addition to span<T>.
///   fixed_vector<T, N> and fixed_devector<T, N> track partial liveness but still remain inline.
///
/// Tradeoffs:
/// - This handle is larger than a minimal vector header, but it centralizes allocation metadata that
///   we need for robust pooling/reuse, deallocation correctness, and strong container interop.
/// - Ring buffers with wrap-around are intentionally not represented here: once data wraps, the live
///   region becomes segmented and no longer matches contiguous container semantics.
/// - Alignment above alignof(T) is supported by moving the live window within the byte allocation
///   (usually after clear), rather than by infecting container types with alignment template args.
///
/// Invariants (unless stated otherwise by a specific container):
/// - [obj_start, obj_end) is the live object range; obj_end is exclusive.
/// - size() is (obj_end - obj_start) in elements.
/// - [alloc_start, alloc_end) is the owned byte allocation; alloc_end is exclusive.
/// - obj_start/obj_end always lie within [alloc_start, alloc_end) (after appropriate alignment).
/// - resource == nullptr means the global default memory resource is used.
template <class T>
struct cc::allocation
{
    /// Pointer to the first live object.
    ///
    /// Points into the owned byte allocation. The live range is contiguous and begins here.
    /// The object lifetime model is: all objects in [obj_start, obj_end) are alive; outside it is
    /// dead storage. For vector-like containers, obj_start is typically also the "data()" pointer.
    T* obj_start = nullptr;

    /// Pointer one past the last live object (exclusive end).
    ///
    /// This is the classic half-open range convention: [obj_start, obj_end).
    /// The number of live elements is (obj_end - obj_start).
    T* obj_end = nullptr;

    /// Start of the owned byte allocation (base pointer returned by the memory resource).
    ///
    /// This pointer must be passed back to the memory resource for deallocation.
    /// We keep the original allocation bounds so we can:
    /// - compute implicit capacity in bytes/elements,
    /// - preserve the underlying byte capacity across retypes,
    /// - and support realignment by moving obj_start/obj_end forward within the allocation.
    cc::byte* alloc_start = nullptr;

    /// End of the owned byte allocation (exclusive).
    ///
    /// The owned byte range is [alloc_start, alloc_end). The total allocated size in bytes is
    /// (alloc_end - alloc_start). This may be larger than the bytes "currently usable" for T if
    /// obj_start was aligned forward for a stricter alignment during reuse/retype.
    cc::byte* alloc_end = nullptr;

    /// Requested alignment for the owned byte allocation (in bytes).
    ///
    /// This is the alignment that was used when allocating [alloc_start, alloc_end) from the resource.
    /// It is stored to enable correct deallocation for resources that require the original alignment
    /// (and for debugging/validation). Note that obj_start may be aligned further forward than this
    /// when retyping to a type U with larger alignof(U); in that case the live window shifts, while
    /// the underlying allocation alignment metadata remains unchanged.
    isize alignment = 0;

    /// Memory resource that owns the allocation, or nullptr for the global default.
    ///
    /// Null means "use global fallback". This makes the all-zero state a valid empty allocation:
    /// - no owned bytes,
    /// - no live objects,
    /// - and the default resource implied.
    ///
    /// Containers can select a non-default resource without cluttering APIs by seeding an empty
    /// allocation that carries the desired resource; subsequent growth/reallocation uses that resource.
    cc::memory_resource const* custom_resource = nullptr;

    // api
public:
    /// Returns the effective resource to use for allocation operations.
    /// Resolves custom_resource if non-null, otherwise falls back to default_memory_resource.
    [[nodiscard]] cc::memory_resource const& resource() const
    {
        return custom_resource ? *custom_resource : *default_memory_resource;
    }

    /// Returns the span of live objects
    /// Note: proper mutability ("const correctness") is user responsibility
    [[nodiscard]] cc::span<T> obj_span() const { return cc::span<T>(obj_start, obj_end); }

    /// Returns the number of live objects
    [[nodiscard]] isize obj_size() const { return obj_end - obj_start; }

    /// Returns the live object size in bytes
    [[nodiscard]] isize obj_size_bytes() const { return (obj_end - obj_start) * sizeof(T); }

    /// Returns true if there are any live objects
    [[nodiscard]] bool has_objects() const { return obj_start != obj_end; }

    /// Returns a reference to the object at the given index
    /// Performs bounds checking via CC_ASSERT
    [[nodiscard]] T& obj_at(isize idx)
    {
        auto const p_obj = obj_start + idx;
        CC_ASSERT(obj_start <= p_obj && p_obj < obj_end, "index out of bounds");
        return *p_obj;
    }

    /// Returns a const reference to the object at the given index
    /// Performs bounds checking via CC_ASSERT
    [[nodiscard]] T const& obj_at(isize idx) const
    {
        auto const p_obj = obj_start + idx;
        CC_ASSERT(obj_start <= p_obj && p_obj < obj_end, "index out of bounds");
        return *p_obj;
    }

    /// Returns a reference to the first live object
    /// Asserts that the allocation is non-empty
    [[nodiscard]] T& obj_front()
    {
        CC_ASSERT(obj_start < obj_end, "allocation is empty");
        return *obj_start;
    }

    /// Returns a const reference to the first live object
    /// Asserts that the allocation is non-empty
    [[nodiscard]] T const& obj_front() const
    {
        CC_ASSERT(obj_start < obj_end, "allocation is empty");
        return *obj_start;
    }

    /// Returns a reference to the last live object
    /// Asserts that the allocation is non-empty
    [[nodiscard]] T& obj_back()
    {
        CC_ASSERT(obj_start < obj_end, "allocation is empty");
        return *(obj_end - 1);
    }

    /// Returns a const reference to the last live object
    /// Asserts that the allocation is non-empty
    [[nodiscard]] T const& obj_back() const
    {
        CC_ASSERT(obj_start < obj_end, "allocation is empty");
        return *(obj_end - 1);
    }

    // factories
public:
    /// Creates an empty allocation with reserved capacity but no live objects.
    ///
    /// Allocates space for 'size' objects with the specified alignment, but does not construct any objects.
    /// The result has obj_start == obj_end == alloc_start (zero live objects, full capacity available).
    /// This is useful for containers that want to reserve memory upfront and construct objects incrementally.
    ///
    /// The alignment parameter allows over-alignment beyond alignof(T).
    /// size == 0 results in nullptr with no real allocation call.
    [[nodiscard]] static allocation create_empty(isize size, isize alignment, memory_resource const* resource) // NOLINT
    {
        CC_ASSERT(alignment >= alignof(T), "alignment must be at least alignof(T)");
        CC_ASSERT(size >= 0, "size must be non-negative");

        allocation result;
        result.custom_resource = resource;
        result.alignment = alignment;

        // Calculate the number of bytes to allocate
        auto const byte_size = size * sizeof(T);

        // Resolve the actual resource to use
        auto const& res = resource ? *resource : *default_memory_resource;

        // Allocate bytes (even if zero-sized)
        result.alloc_start = res.allocate_bytes(byte_size, result.alignment, res.userdata);
        result.alloc_end = result.alloc_start + byte_size;

        // Initialize obj_start and obj_end to alloc_start (zero live objects, full capacity)
        result.obj_start = (T*)result.alloc_start;
        result.obj_end = result.obj_start;

        return result;
    }

    /// Creates an allocation with a specified count of default-constructed objects.
    ///
    /// Allocates space for 'size' objects and default-constructs all of them.
    /// The result is a "tight" allocation: allocated bytes exactly match live object count,
    /// with obj_start and obj_end spanning the full allocated range (no spare capacity).
    ///
    /// Objects are default-constructed via default_create_objects_to, which uses T().
    /// This ensures zero-initialization for primitive types (e.g., int, float, pointers).
    /// Alignment is set to alignof(T).
    ///
    /// Empty allocations (size == 0) result in nullptr with no real allocation call.
    [[nodiscard]] static allocation create_defaulted(isize size, memory_resource const* resource)
    {
        auto result = allocation::create_empty(size, alignof(T), resource);
        impl::default_create_objects_to(result.obj_end, size);
        return result;
    }

    /// Creates an allocation with a specified count of objects, all copy-constructed from a single value.
    ///
    /// Allocates space for 'size' objects and copy-constructs all of them from 'value'.
    /// The result is a "tight" allocation: allocated bytes exactly match live object count,
    /// with obj_start and obj_end spanning the full allocated range (no spare capacity).
    ///
    /// Objects are copy-constructed via fill_create_objects_to.
    /// Alignment is set to alignof(T).
    ///
    /// Empty allocations (size == 0) result in nullptr with no real allocation call.
    [[nodiscard]] static allocation create_filled(isize size, T const& value, memory_resource const* resource)
    {
        auto result = allocation::create_empty(size, alignof(T), resource);
        impl::fill_create_objects_to(result.obj_end, size, value);
        return result;
    }

    /// Creates an allocation with uninitialized memory treated as live objects.
    ///
    /// Allocates space for 'size' objects but does NOT construct them. Instead, obj_end is set to
    /// the end of the allocation, treating the uninitialized memory as if it contains live objects.
    /// The result is a "tight" allocation: allocated bytes exactly match the live object range,
    /// with obj_start and obj_end spanning the full allocated range (no spare capacity).
    ///
    /// IMPORTANT: This is only safe for trivially copyable and trivially destructible types,
    /// as enforced by static assertions. The caller is responsible for properly initializing
    /// the memory before reading from it (e.g., via memcpy or direct writes).
    ///
    /// Alignment is set to alignof(T).
    /// Empty allocations (size == 0) result in nullptr with no real allocation call.
    [[nodiscard]] static allocation create_uninitialized(isize size, memory_resource const* resource)
    {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable for uninitialized allocation");
        static_assert(std::is_trivially_destructible_v<T>, "T must be trivially destructible for uninitialized "
                                                           "allocation");

        auto result = allocation::create_empty(size, alignof(T), resource);
        result.obj_end = result.obj_start + size;
        return result;
    }

    /// Creates an allocation with uninitialized memory treated as live objects (UNSAFE).
    ///
    /// Allocates space for 'size' objects but does NOT construct them. Instead, obj_end is set to
    /// the end of the allocation, treating the uninitialized memory as if it contains live objects.
    /// The result is a "tight" allocation: allocated bytes exactly match the live object range,
    /// with obj_start and obj_end spanning the full allocated range (no spare capacity).
    ///
    /// DANGEROUS: Unlike create_uninitialized, this version does NOT enforce trivial copyability
    /// or trivial destructibility via static assertions. The caller MUST ensure that:
    /// 1. The type has a trivial destructor (or the memory is properly initialized before destruction)
    /// 2. The memory is properly initialized before any reads or operations that assume constructed objects
    ///
    /// Only use this when you need uninitialized allocation for types that don't satisfy the
    /// create_uninitialized requirements but you can guarantee safety through other means.
    ///
    /// Alignment is set to alignof(T).
    /// Empty allocations (size == 0) result in nullptr with no real allocation call.
    [[nodiscard]] static allocation create_uninitialized_unsafe(isize size, memory_resource const* resource)
    {
        auto result = allocation::create_empty(size, alignof(T), resource);
        result.obj_end = result.obj_start + size;
        return result;
    }

    /// Creates a deep copy of a span of objects using the specified memory resource.
    ///
    /// Copies all objects from the provided span.
    /// The result is a "tight" allocation: allocated bytes exactly match live object count,
    /// with obj_start and obj_end spanning the full allocated range (no spare capacity).
    ///
    /// Objects are copy-constructed via copy_create_objects_to.
    /// Alignment is set to alignof(T).
    ///
    /// Empty spans (size == 0) result in nullptr with no real allocation call.
    [[nodiscard]] static allocation create_copy_of(span<T const> source, memory_resource const* resource)
    {
        auto result = allocation::create_empty(source.size(), alignof(T), resource);
        impl::copy_create_objects_to(result.obj_end, source.data(), source.data() + source.size());
        return result;
    }

    /// Same as create_copy_of(source, resource) but uses the default memory resource
    [[nodiscard]] static allocation create_copy_of(span<T const> source)
    {
        return allocation::create_copy_of(source, nullptr);
    }

    /// Creates a deep copy of another allocation using the specified memory resource.
    ///
    /// Copies only the live object range [rhs.obj_start, rhs.obj_end), not the full capacity.
    /// This is a convenience overload that forwards to create_copy_of(span, resource).
    ///
    /// The resource parameter may differ from rhs.custom_resource, enabling cross-resource copies.
    [[nodiscard]] static allocation create_copy_of(allocation const& rhs, memory_resource const* resource)
    {
        return allocation::create_copy_of(rhs.obj_span(), resource);
    }

    /// Same as create_copy_of(rhs, resource) but uses the same memory resource as rhs
    [[nodiscard]] static allocation create_copy_of(allocation const& rhs)
    {
        return allocation::create_copy_of(rhs.obj_span(), rhs.custom_resource);
    }

    // lifecycle
public:
    allocation() = default;

    // no implicit copies for allocations
    // downstream containers need to handle this explicitly!
    allocation(allocation const&) = delete;
    allocation& operator=(allocation const&) = delete;

    allocation(allocation&& rhs) noexcept
      : obj_start(cc::exchange(rhs.obj_start, nullptr)),
        obj_end(cc::exchange(rhs.obj_end, nullptr)),
        alloc_start(cc::exchange(rhs.alloc_start, nullptr)),
        alloc_end(cc::exchange(rhs.alloc_end, nullptr)),
        alignment(cc::exchange(rhs.alignment, 0)),
        custom_resource(rhs.custom_resource) // rhs resource stays
    {
    }

    /// Move assignment operator with nested-rhs safety guarantee.
    ///
    /// This implementation is safe even when rhs is nested inside one of the objects
    /// being destroyed in 'this'. The critical ordering is:
    ///
    /// 1. Move rhs into a local temporary (which clears rhs via move constructor)
    /// 2. Destroy objects in 'this' (safe even if this destroys rhs, since it's already cleared)
    /// 3. Transfer ownership from the temporary to 'this'
    ///
    /// This ensures that if rhs is destroyed during step 2, it's already been moved-from
    /// and won't attempt to deallocate its memory (preventing double-free).
    ///
    /// Example scenario this protects against:
    ///   allocation<SomeStruct> outer;
    ///   outer contains: SomeStruct{ allocation<SomeStruct> inner; }
    ///   outer = std::move(outer[0].inner);  // rhs nested in 'this'
    ///
    allocation& operator=(allocation&& rhs) noexcept
    {
        if (this != &rhs)
        {
            // Move rhs into temporary - this clears rhs via the move constructor
            auto rhs_tmp = cc::move(rhs);

            // Destroy existing objects and deallocate
            impl::destroy_objects_in_reverse(obj_start, obj_end);
            if (alloc_start != nullptr)
            {
                auto const& res = resource();
                res.deallocate_bytes(alloc_start, alloc_end - alloc_start, alignment, res.userdata);
            }

            // Transfer ownership from tmp
            obj_start = cc::exchange(rhs_tmp.obj_start, nullptr);
            obj_end = cc::exchange(rhs_tmp.obj_end, nullptr);
            alloc_start = cc::exchange(rhs_tmp.alloc_start, nullptr);
            alloc_end = cc::exchange(rhs_tmp.alloc_end, nullptr);
            alignment = cc::exchange(rhs_tmp.alignment, 0);
            custom_resource = rhs_tmp.custom_resource; // rhs resource stays
        }

        return *this;
    }

    ~allocation()
    {
        // end life and call dtor of live objects
        impl::destroy_objects_in_reverse(obj_start, obj_end);

        // return allocation
        if (alloc_start != nullptr)
        {
            auto const& res = resource();
            res.deallocate_bytes(alloc_start, alloc_end - alloc_start, alignment, res.userdata);
        }
    }
};


template <class T, class ContainerT>
struct cc::allocating_container
{
    using container_t = ContainerT;

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
    [[nodiscard]] static container_t create_from_allocation(cc::allocation<T> data)
    {
        container_t c;
        c._data = cc::move(data);
        return c;
    }

    // initializes a new container_t with "size" many defaulted elements
    [[nodiscard]] static container_t create_defaulted(size_t size, cc::memory_resource const* resource = nullptr)
    {
        return container_t::create_from_allocation(cc::allocation<T>::create_defaulted(size, resource));
    }

    // initializes a new container_t with "size" many elements, all copy-constructed from "value"
    [[nodiscard]] static container_t create_filled(size_t size,
                                                   T const& value,
                                                   cc::memory_resource const* resource = nullptr)
    {
        return container_t::create_from_allocation(cc::allocation<T>::create_filled(size, value, resource));
    }

    // initializes a new container_t with "size" many uninitialized elements (only safe for trivial types)
    [[nodiscard]] static container_t create_uninitialized(size_t size, cc::memory_resource const* resource = nullptr)
    {
        return container_t::create_from_allocation(cc::allocation<T>::create_uninitialized(size, resource));
    }

    // creates a deep copy of the provided span
    [[nodiscard]] static container_t create_copy_of(cc::span<T const> source,
                                                    cc::memory_resource const* resource = nullptr)
    {
        return container_t::create_from_allocation(cc::allocation<T>::create_copy_of(source, resource));
    }

    allocating_container() = default;
    ~allocating_container() = default;

    // move semantics are already fine via cc::allocation
    allocating_container(allocating_container&&) = default;
    allocating_container& operator=(allocating_container&&) = default;

    // deep copy semantics
    // containers that use this mix-in can simply delete their copy ctor if they do not want it
    allocating_container(allocating_container const& rhs) : _data(cc::allocation<T>::create_copy_of(rhs._data)) {}
    allocating_container& operator=(allocating_container const& rhs)
    {
        if (this != &rhs)
            _data = cc::allocation<T>::create_copy_of(rhs._data, _data.custom_resource); // keep lhs resource
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
