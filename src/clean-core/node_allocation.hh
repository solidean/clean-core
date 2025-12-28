#pragma once

#include <clean-core/bit.hh>
#include <clean-core/fwd.hh>
#include <clean-core/utility.hh>

/// Small-node allocation system optimized for cheap thread-local allocation and wait-free deallocation.
/// Nodes are grouped by power-of-two size classes: class index i corresponds to size 2^i bytes.
/// Size class index = bit_width(max(sizeof(T), alignof(T)) - 1), ensuring both size and alignment are satisfied.
///
/// Each slab contains exactly 64 slots and is size_class * 64 bytes; slabs are aligned to their own size.
/// A u64 free bitmap stored at the slab base tracks slot availability (one bit per slot).
/// The bitmap physically overlaps ceil(8 / size_class) initial slots; those slots are permanently unavailable.
///
/// Allocation is thread-owned and may update allocator state for bookkeeping and slab lifecycle management.
/// Deallocation requires only the pointer and class index; no allocator state or resource reference needed.
/// Free operations are wait-free: any thread may free any node via atomic bitmap update (atomic_or).
/// The allocating thread discovers remotely freed slots during cold allocation paths (slab reuse, new slab allocation).
///
/// Slab base recovery from any interior pointer: ptr & ~(slab_size - 1), exploiting alignment.
///
/// Target use case: node-based containers (list, map, set) and small heap objects (unique_ptr payloads, unique_function).
/// Out of scope: large allocations, contiguous buffers, bulk operations; use cc::allocation<T> or cc::memory_resource.
struct cc::node_memory_resource
{
    /// Newtype for size class index (0, 1, 2, ...) to prevent API confusion.
    /// The index i maps to actual size 2^i bytes.
    enum class class_index : u64
    {
    };

    /// Newtype for actual size class in bytes (1, 2, 4, 8, 16, ...) to prevent API confusion.
    /// This is the actual allocation size & alignment, computed as 2^index.
    enum class class_size : u64
    {
    };

    /// Compute the class index from size and alignment requirements.
    /// Class index i maps to actual size 2^i bytes (power of two).
    /// Input is max(size, alignment) rounded up to next power of 2 via bit_width.
    /// This helper is called by the templated class_index_for<T>() to minimize template logic.
    /// Examples: size=1,align=1 → max=1 → bit_width=1 → index=0 (class size 1)
    ///           size=5,align=4 → max=5 → bit_width(4)=3 → index=3 (class size 8)
    ///           size=16,align=8 → max=16 → bit_width(15)=4 → index=4 (class size 16)
    static constexpr class_index class_index_from_size_and_align(isize size, isize alignment)
    {
        auto const required = cc::max(size, alignment);
        return class_index(cc::bit_width(u64(required - 1)));
    }

    /// Compute the class index for T.
    /// Class index i means actual allocation size is 2^i bytes.
    /// All nodes with the same class index share slab infrastructure.
    /// Uses bit operations for ultra-cheap computation and masking logic.
    /// Examples: sizeof=1,alignof=1 → index 0 (size 1)
    ///           sizeof=12,alignof=4 → index 4 (size 16)
    ///           sizeof=24,alignof=8 → index 5 (size 32)
    template <class T>
    static constexpr class_index class_index_for()
    {
        return class_index_from_size_and_align(sizeof(T), alignof(T));
    }

    /// Compute slab size in bytes for a given class index.
    /// Slab size = (2^index) * 64 bytes, yielding exactly 64 slots per slab.
    /// Examples: index 0 (size 1) → 64 bytes, index 2 (size 4) → 256 bytes, index 4 (size 16) → 1024 bytes.
    static constexpr isize slab_size_bytes_for_class(class_index idx) { return (isize(1) << isize(idx)) * 64; }

    /// Compute the alignment mask for a slab of the given class index.
    /// Used to extract slab base from any pointer within the slab via ptr & ~mask.
    static constexpr isize slab_mask_for_class(class_index idx) { return slab_size_bytes_for_class(idx) - 1; }

    /// Recover the slab base address from any pointer inside the slab.
    /// Exploits the fact that slabs are aligned to their own size.
    /// Computation is ptr & ~(slab_size - 1), a single bitwise AND.
    static cc::byte* slab_base_for_ptr(cc::byte* ptr, class_index idx)
    {
        auto const mask = node_memory_resource::slab_mask_for_class(idx);
        return reinterpret_cast<cc::byte*>(reinterpret_cast<u64>(ptr) & ~u64(mask)); // NOLINT
    }

    /// Retrieve the free bitmap for a slab.
    /// The u64 free bitmap is stored at the slab base; one bit per slot.
    /// Bits corresponding to slots overlapping the bitmap itself remain permanently zero.
    static u64* slab_freemap_for_base(cc::byte* base)
    {
        return reinterpret_cast<u64*>(base); // NOLINT
    }

    /// Compute the slot index within a slab for a given pointer.
    /// Index = (ptr - slab_base) / class_size where class_size = 2^class_index.
    /// Optimized to (ptr - slab_base) >> class_index for power-of-two division.
    static u64 slot_index_for_ptr(cc::byte* ptr, cc::byte* base, class_index idx)
    {
        return u64(ptr - base) >> u64(idx);
    }

    /// Free a node by returning its slot to the slab's free bitmap.
    /// Requires only the pointer and class index; no allocator state or resource reference needed.
    /// Implemented as a single atomic_or into the slab's free bitmap; wait-free and callable from any thread.
    /// The owning thread may later discover this freed slot in its cold allocation path.
    static void node_allocation_free(cc::byte* ptr, class_index idx)
    {
        auto const base = slab_base_for_ptr(ptr, idx);
        auto const freemap = slab_freemap_for_base(base);
        auto const slot_bit = u64(1) << slot_index_for_ptr(ptr, base, idx);

        cc::atomic_or(*freemap, slot_bit);
    }
};

/// Move-only owning handle for a single live T stored in node memory.
/// Stores only a T*; all information required to free the slot is derived from the pointer and sizeof/alignof(T).
/// The destructor calls ~T() and returns the slot to its slab via wait-free bitmap update.
/// No allocator state, resource reference, or size parameter is stored or required for destruction.
/// Intended for node-based container elements, unique_ptr payloads, and unique_function storage.
/// This handle does not support copying; ownership transfer is move-only.
template <class T>
struct cc::node_allocation
{
    // ctors/dtor
public:
    node_allocation() = default;

    node_allocation(node_allocation&& rhs) noexcept : ptr(cc::exchange(rhs.ptr, nullptr)) {}
    node_allocation& operator=(node_allocation&& rhs) noexcept
    {
        ptr = cc::exchange(rhs.ptr, nullptr);
        return *this;
    }
    node_allocation(node_allocation const&) = delete;
    node_allocation& operator=(node_allocation const&) = delete;

    /// Destroy the managed object and return its slot to the slab.
    /// Calls ~T(), then performs a single atomic_or into the slab's free bitmap.
    /// Wait-free and requires no allocator state; the slab base and slot index are derived from ptr.
    ~node_allocation()
    {
        if (ptr != nullptr)
        {
            ptr->~T();
            cc::node_memory_resource::node_allocation_free((cc::byte*)ptr,
                                                           cc::node_memory_resource::class_index_for<T>());
        }
    }

    // members
public:
    /// Pointer to the managed T; nullptr indicates an empty handle.
    /// All deallocation metadata (slab base, slot index) is computed from this pointer.
    T* ptr = nullptr;
};
