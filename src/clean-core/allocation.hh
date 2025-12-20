#pragma once

#include <clean-core/fwd.hh>

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
    cc::memory_resource* resource = nullptr;
};
