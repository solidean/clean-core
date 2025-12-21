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
void destroy_objects_in_reverse(T* start, T* end)
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
void copy_create_objects_to(T*& dest_end, T const* src_start, T const* src_end)
{
    static_assert(sizeof(T) > 0, "T must be a complete type (did you forget to include a header?)");

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
void copy_assign_objects_to(T*& dest_end, T const* src_start, T const* src_end)
{
    static_assert(sizeof(T) > 0, "T must be a complete type (did you forget to include a header?)");

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
    /// Creates a deep copy of another allocation using the specified memory resource.
    ///
    /// Copies only the live object range [rhs.obj_start, rhs.obj_end), not the full capacity.
    /// The result is a "tight" allocation: allocated bytes exactly match live object count,
    /// with obj_start and obj_end spanning the full allocated range (no spare capacity).
    ///
    /// Objects are copy-constructed via copy_create_objects_to.
    /// Alignment is set to alignof(T).
    ///
    /// The resource parameter may differ from rhs.custom_resource, enabling cross-resource copies.
    /// Empty allocations (size == 0) result in nullptr with no real allocation call.
    [[nodiscard]] static allocation create_copy_of(allocation const& rhs, memory_resource const* resource)
    {
        allocation result;
        result.custom_resource = resource;
        result.alignment = alignof(T);

        // Calculate the number of live objects to copy
        auto const obj_count = rhs.obj_end - rhs.obj_start;
        auto const byte_size = obj_count * sizeof(T);

        // Resolve the actual resource to use
        auto const& res = resource ? *resource : *default_memory_resource;

        // Allocate bytes (even if zero-sized)
        result.alloc_start = res.allocate_bytes(byte_size, result.alignment, res.userdata);
        result.alloc_end = result.alloc_start + byte_size;

        // Initialize obj_start to the allocation base and obj_end to obj_start (empty initial state)
        // This ensures [obj_start, obj_end) remains a valid live range even if copy construction throws
        result.obj_start = (T*)result.alloc_start;
        result.obj_end = result.obj_start;

        // Copy-construct objects from rhs into the new allocation
        // obj_end is incremented for each successful construction
        // On success: obj_end == alloc_end (tight allocation with no spare capacity)
        // On exception: [obj_start, obj_end) contains only the successfully constructed objects
        impl::copy_create_objects_to(result.obj_end, rhs.obj_start, rhs.obj_end);

        return result;
    }

    /// Same as create_copy_of(rhs, resource) but uses the same memory resource as rhs
    [[nodiscard]] static allocation create_copy_of(allocation const& rhs)
    {
        return create_copy_of(rhs, rhs.custom_resource);
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
        custom_resource(cc::exchange(rhs.custom_resource, nullptr))
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
            custom_resource = cc::exchange(rhs_tmp.custom_resource, nullptr);
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
