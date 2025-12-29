#pragma once

#include <clean-core/bit.hh>
#include <clean-core/fwd.hh>
#include <clean-core/utility.hh>

#include <atomic>

/// Newtype for size class index (0, 1, 2, ...) to prevent API confusion.
/// The index i maps to actual size 2^i bytes.
/// Kept as u8 to be as tight as possible
enum class cc::node_class_index : cc::u8
{
    // max small class is 2^8 = 256 bytes
    // after that we do normal allocations and store a header
    small_max = 8,

    // number of small size classes
    small_count = small_max + 1,
};

/// Newtype for actual size class in bytes (1, 2, 4, 8, 16, ...) to prevent API confusion.
/// This is the actual allocation size & alignment, computed as 2^index.
enum class cc::node_class_size : cc::u64
{
};

namespace cc
{
/// Compute the class index from size and alignment requirements.
/// Class index i maps to actual size 2^i bytes (power of two).
/// Input is max(size, alignment) rounded up to next power of 2 via bit_width.
/// This helper is called by the templated node_class_index_for<T>() to minimize template logic.
/// Examples: size=1,align=1 → max=1 → bit_width=1 → index=0 (class size 1)
///           size=5,align=4 → max=5 → bit_width(4)=3 → index=3 (class size 8)
///           size=16,align=8 → max=16 → bit_width(15)=4 → index=4 (class size 16)
[[nodiscard]] constexpr node_class_index node_class_index_from_size_and_align(isize size, isize alignment)
{
    auto const required = cc::max(size, alignment);
    return node_class_index(cc::bit_width(u64(required - 1)));
}

/// Compute the class index for T.
/// Class index i means actual allocation size is 2^i bytes.
/// All nodes with the same class index share slab infrastructure.
/// Uses bit operations for ultra-cheap computation and masking logic.
/// Examples: sizeof=1,alignof=1 → index 0 (size 1)
///           sizeof=12,alignof=4 → index 4 (size 16)
///           sizeof=24,alignof=8 → index 5 (size 32)
template <class T>
[[nodiscard]] constexpr node_class_index node_class_index_for()
{
    return node_class_index_from_size_and_align(sizeof(T), alignof(T));
}

/// Compute slab size in bytes for a given class index.
/// Slab size = (2^index) * 64 bytes, yielding exactly 64 slots per slab.
/// Examples: index 0 (size 1) → 64 bytes, index 2 (size 4) → 256 bytes, index 4 (size 16) → 1024 bytes.
[[nodiscard]] constexpr isize node_slab_size_bytes_for_class(node_class_index idx)
{
    return (isize(1) << isize(idx)) * 64;
}

/// Compute the alignment mask for a slab of the given class index.
/// Used to extract slab base from any pointer within the slab via ptr & ~mask.
[[nodiscard]] constexpr isize node_slab_mask_for_class(node_class_index idx)
{
    return node_slab_size_bytes_for_class(idx) - 1;
}

/// Recover the slab base address from any pointer inside the slab.
/// Exploits the fact that slabs are aligned to their own size.
/// Computation is ptr & ~(slab_size - 1), a single bitwise AND.
[[nodiscard]] CC_FORCE_INLINE cc::byte* node_slab_base_for_ptr(cc::byte* ptr, node_class_index idx)
{
    auto const mask = node_slab_mask_for_class(idx);
    return reinterpret_cast<cc::byte*>(reinterpret_cast<u64>(ptr) & ~u64(mask)); // NOLINT
}

/// Retrieve the free bitmap for a slab.
/// The u64 free bitmap is stored at the slab base; one bit per slot.
/// Bits corresponding to slots overlapping the bitmap itself remain permanently zero.
/// This function is safe to call with nullptr and will return nullptr in that case.
[[nodiscard]] CC_FORCE_INLINE u64* node_slab_freemap_for_base(cc::byte* base)
{
    return reinterpret_cast<u64*>(base); // NOLINT
}

/// Retrieve the pointer to the next slab in the ring.
/// The next slab pointer is stored adjacent to the freemap (at base + 8).
[[nodiscard]] CC_FORCE_INLINE cc::byte* node_slab_next_for_base(cc::byte* base)
{
    return *reinterpret_cast<cc::byte**>(base + 8); // NOLINT
}

/// Compute the slot index within a slab for a given pointer.
/// Index = (ptr - slab_base) / class_size where class_size = 2^class_index.
/// Optimized to (ptr - slab_base) >> class_index for power-of-two division.
[[nodiscard]] CC_FORCE_INLINE u64 node_slot_index_for_ptr(cc::byte* ptr, cc::byte* base, node_class_index idx)
{
    return u64(ptr - base) >> u64(idx);
}

/// Compute the slot pointer for a given base address, class index, and slot index.
/// Inverse of node_slot_index_for_ptr: ptr = base + slot_index * class_size.
/// Optimized to base + (slot_index << class_index) for power-of-two multiplication.
[[nodiscard]] CC_FORCE_INLINE cc::byte* node_slot_ptr_for(cc::byte* base, node_class_index idx, u64 slot_index)
{
    return base + (slot_index << u64(idx));
}

/// Free a node by returning its slot to the slab's free bitmap.
/// Requires only the pointer and class index; no allocator state or resource reference needed.
/// Implemented as a single atomic_or into the slab's free bitmap; wait-free and callable from any thread.
/// The owning thread may later discover this freed slot in its cold allocation path.
///
/// DESIGN CHOICE: This is a free function, not bound to node_memory_resource.
/// This ensures that freeing cannot directly depend on the resource by default.
/// Deallocation is stateless and decoupled from allocation, enabling wait-free cross-thread deallocation
/// without requiring the freeing thread to have any reference to or knowledge of the allocating resource.
CC_FORCE_INLINE void node_allocation_free(cc::byte* ptr, node_class_index idx)
{
    auto const base = cc::node_slab_base_for_ptr(ptr, idx);
    auto const freemap = cc::node_slab_freemap_for_base(base);
    auto const slot_bit = u64(1) << cc::node_slot_index_for_ptr(ptr, base, idx);

    // TODO: branch on large node
    CC_ASSERT(idx <= node_class_index::small_max, "TODO: implement large-node-alloc");

    CC_ASSERT((*freemap & slot_bit) == 0, "node is already freed. double-delete or corruption?");
    cc::atomic_or(*freemap, slot_bit);
}

/// Default node memory resource used when none specified.
/// This is a system-backed allocator stored in the data segment, making the pointer valid even during
/// static initialization in other translation units (safe for use in global/static constructors).
/// This uses TLS-pooled slabs per size class and a cold-path rebalance strategy
/// New backing allocations are pulled in from cc::default_memory_resource
extern cc::node_memory_resource* const default_node_memory_resource;
} // namespace cc

// this is a concrete non-customizable allocator interface for all node classes
// if a node memory resource segregates by threads, it must be by giving out different allocators (e.g. via TLS)
// this is designed in a way that the fast path allocation is _extremely_ fast
// each node_allocator is NOT thread_safe, even though a node_memory_resource might be
// this struct functions as a local cache to keep the hot path happy
struct cc::node_allocator
{
public:
    // allocates a new node from the given size class
    // must be freed via node_allocation_free and the same class index!
    // extremely efficient fast path
    // occasionally calls into a slow path to regenerate
    // NOTE: this must be inlined to make "idx" a comptime constant in most cases!
    [[nodiscard]] CC_FORCE_INLINE cc::byte* allocate_node_bytes(node_class_index idx)
    {
        // slow path for large nodes
        if (idx > node_class_index::small_max) [[unlikely]]
            return this->allocate_node_bytes_large(idx);

        auto const base = _slab_base[isize(idx)];

        // happy path for valid slabs with free slots
        if (base != nullptr) [[likely]]
        {
            auto const a_freemap = std::atomic_ref<u64>(*cc::node_slab_freemap_for_base(base));
            auto const freemap = a_freemap.load();

            // this is the hottest node allocation path, so we duplicate it
            if (freemap != 0) [[likely]]
            {
                auto const slot_idx = cc::count_trailing_zeroes(freemap);
                auto const slot_bit = u64(1) << slot_idx;
                // updating the freemap must happen atomically
                // we're the only thread allocating from it
                // BUT there can be many threads that concurrently free
                auto const old_freemap = a_freemap.fetch_and(~slot_bit);
                CC_ASSERT((old_freemap & slot_bit) != 0, "double-allocation detected. this indicates multiple threads "
                                                         "allocating from the same slab");
                return cc::node_slot_ptr_for(base, idx, u64(slot_idx));
            }
        }

        // all else goes through fallback paths
        return this->allocate_node_bytes_non_fast(idx);
    }

private:
    // fallback when the current slab is either not allocated or doesn't have free slots
    [[nodiscard]] cc::byte* allocate_node_bytes_non_fast(node_class_index idx)
    {
        if (_slab_base[isize(idx)] == nullptr) [[unlikely]]
            initialize_slab_for(idx);

        auto const start_base = _slab_base[isize(idx)];
        CC_ASSERT(start_base != nullptr, "node class should be initialized");

        // slab is initialized but full
        // we now iterate the slab ring to find a new free slab
        // this is still reasonably hot (it's the full node capacity for this thread without refill)
        auto base = cc::node_slab_next_for_base(start_base);
        while (base != start_base)
        {
            CC_ASSERT(base != nullptr, "the slab ring must be a cycling single-linked-list. indicates a "
                                       "node_memory_resource bug.");

            auto const a_freemap = std::atomic_ref<u64>(*cc::node_slab_freemap_for_base(base));
            auto const freemap = a_freemap.load();

            if (freemap != 0) [[likely]]
            {
                // record next free slab
                _slab_base[isize(idx)] = base;

                // allocate & return
                auto const slot_idx = cc::count_trailing_zeroes(freemap);
                auto const slot_bit = u64(1) << slot_idx;
                // updating the freemap must happen atomically
                // we're the only thread allocating from it
                // BUT there can be many threads that concurrently free
                auto const old_freemap = a_freemap.fetch_and(~slot_bit);
                CC_ASSERT((old_freemap & slot_bit) != 0, "double-allocation detected. this indicates multiple threads "
                                                         "allocating from the same slab");
                return cc::node_slot_ptr_for(base, idx, slot_idx);
            }

            // advance
            base = cc::node_slab_next_for_base(base);
        }

        // all slabs in the ring are full
        // => we request more from the allocator
        return this->refill_slabs_and_allocate_node_bytes(idx);
    }

    // slow path for non-small nodes
    // TODO: implement me
    [[nodiscard]] CC_COLD_FUNC cc::byte* allocate_node_bytes_large(node_class_index idx);

    // called when the current slab ring is full
    // should do bookkeeping and ensure the slab ring has free capacity
    // and also allocate a node for the given size class (guaranteed to be a small class)
    // TODO: implement me
    [[nodiscard]] CC_COLD_FUNC cc::byte* refill_slabs_and_allocate_node_bytes(node_class_index idx);

    // only called when a class is not initialized yet
    // guarantees non-null base for that class afterwards
    // TODO: implement me
    CC_COLD_FUNC void initialize_slab_for(node_class_index idx);

private:
    // base pointers to a slab for each class
    // is lazy-initialized (can be nullptr) via a slow path
    // concrete node memory resources can decide to only support certain classes (but the default one supports all)
    // the slabs here are not guaranteed to have free slots
    // because we want to keep the happy path fast (and there could be frees until the next alloc)
    cc::byte* _slab_base[isize(node_class_index::small_count)] = {};

    // the backup resource for this allocation
    // NOTE: must be non-nullptr for a valid allocator!
    cc::node_memory_resource* _resource = nullptr;

    friend node_memory_resource;
};

/// Small-node allocation system optimized for cheap thread-local allocation and wait-free deallocation.
/// Nodes are grouped by power-of-two size classes: class index i corresponds to size 2^i bytes.
/// Size class index = bit_width(max(sizeof(T), alignof(T)) - 1), ensuring both size and alignment are satisfied.
///
/// Each slab is a 64 * class_size block; slabs are aligned to their own size.
/// The slab prefix contains metadata that blocks some initial slots:
///   - First u64 (offset 0): Free bitmap tracking slot availability (one bit per slot)
///   - Second u64 (offset 8): Pointer to the next slab in the slab ring (byte*)
///
/// The number of blocked slots depends on class size:
///   - 1B nodes (e.g., char): 16 bytes prefix blocks 16 slots → 48 usable slots per slab
///   - 8B nodes (e.g., i64, ptr): 16 bytes prefix blocks 2 slots → 62 usable slots per slab
///   - 16B+ nodes: 16 bytes prefix blocks 1 slot → 63 usable slots per slab
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
///
/// Future: could support shared_ptr-like semantics as well by co-locating refcounts
struct cc::node_memory_resource
{
    // returns a node allocator that is usable on this thread
    // must not be used cross-thread
    // for best performance, try to reuse one allocator as much as possible
    // it is fine for a memory resource to return the same reference again on the same thread
    // this mechanism allows TLS-segregated resources but also purely local resources at the same time
    cc::function_ptr<node_allocator&()> get_allocator = nullptr;

    /// User-defined data for custom allocators. Can be nullptr for stateless allocators.
    void* userdata = nullptr;
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
    // properties
public:
    [[nodiscard]] bool is_valid() const { return ptr != nullptr; }
    explicit operator bool() const { return ptr != nullptr; }

    // factory
public:
    // NOTE: resource can be nullptr
    template <class... Args>
    [[nodiscard]] static node_allocation create_from(node_allocator& alloc, Args&&... args)
    {
        static_assert(
            requires { T(cc::forward<Args>(args)...); }, "T is not constructible from the provided argument "
                                                         "types");

        auto const ptr = alloc.allocate_node_bytes(cc::node_class_index_for<T>());

        node_allocation n;
        n.ptr = new (cc::placement_new, ptr) T(cc::forward<Args>(args)...);
        return n;
    }

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
            cc::node_allocation_free((cc::byte*)ptr, cc::node_class_index_for<T>());
        }
    }

    // members
public:
    /// Pointer to the managed T; nullptr indicates an empty handle.
    /// All deallocation metadata (slab base, slot index) is computed from this pointer.
    T* ptr = nullptr;
};

// almost like a theoretical cc::node_allocation<void>
// this stores size class + void* ptr + a deleter fun ptr
// super useful for type erased wrappers like unique_function (with no natural base class)
struct cc::any_node_allocation
{
    // properties
public:
    [[nodiscard]] bool is_valid() const { return ptr != nullptr; }
    explicit operator bool() const { return ptr != nullptr; }

    // ctors/dtor
public:
    any_node_allocation() = default;

    template <class T>
    any_node_allocation(node_allocation<T> alloc)
      : ptr(cc::exchange(alloc.ptr, nullptr)), class_index(cc::node_class_index_for<T>())
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
            deleter = [](void* ptr) { (*reinterpret_cast<T*>(ptr)).~T(); }; // NOLINT
    }

    any_node_allocation(any_node_allocation&& rhs) noexcept
      : ptr(cc::exchange(rhs.ptr, nullptr)), deleter(cc::exchange(rhs.deleter, nullptr)), class_index(rhs.class_index)
    {
    }
    any_node_allocation& operator=(any_node_allocation&& rhs) noexcept
    {
        ptr = cc::exchange(rhs.ptr, nullptr);
        deleter = cc::exchange(rhs.deleter, nullptr);
        class_index = rhs.class_index;
        return *this;
    }
    any_node_allocation(any_node_allocation const&) = delete;
    any_node_allocation& operator=(any_node_allocation const&) = delete;

    /// Destroy the managed object and return its slot to the slab.
    /// Calls ~T(), then performs a single atomic_or into the slab's free bitmap.
    /// Wait-free and requires no allocator state; the slab base and slot index are derived from ptr.
    ~any_node_allocation()
    {
        if (ptr != nullptr)
        {
            if (deleter)
                deleter(ptr);

            cc::node_allocation_free(reinterpret_cast<cc::byte*>(ptr), class_index); // NOLINT
        }
    }

    // members
public:
    /// Pointer to the managed T; nullptr indicates an empty handle.
    /// All deallocation metadata (slab base, slot index) is computed from this pointer.
    void* ptr = nullptr;

    /// If non-null, calls the dtor of ptr (null for trivially destructible)
    cc::function_ptr<void(void*)> deleter = nullptr;

    // size class index of this node allocation
    cc::node_class_index class_index = {};

    // future: we have 7 padding bytes here (3 on wasm?)
};

// node_allocation but with a way to get class index dynamically (not coupled to T)
// enables casting and polymorphism
// completely user-customizable
// NodeTraits::destroy_and_get_class_index(T&) -> node_class_index
template <class T, class NodeTraits>
struct cc::poly_node_allocation
{
    // properties
public:
    [[nodiscard]] bool is_valid() const { return ptr != nullptr; }
    explicit operator bool() const { return ptr != nullptr; }

    // ctors/dtor
public:
    poly_node_allocation() = default;

    poly_node_allocation(poly_node_allocation&& rhs) noexcept : ptr(cc::exchange(rhs.ptr, nullptr)) {}
    poly_node_allocation& operator=(poly_node_allocation&& rhs) noexcept
    {
        ptr = cc::exchange(rhs.ptr, nullptr);
        return *this;
    }
    poly_node_allocation(poly_node_allocation const&) = delete;
    poly_node_allocation& operator=(poly_node_allocation const&) = delete;

    /// Destroy the managed object and return its slot to the slab.
    /// Calls NodeTraits::destroy_and_get_class_index, then performs a single atomic_or into the slab's free bitmap.
    /// Wait-free and requires no allocator state; the slab base and slot index are derived from ptr.
    ~poly_node_allocation()
    {
        if (ptr != nullptr)
        {
            cc::node_class_index const class_idx = NodeTraits::destroy_and_get_class_index(*ptr);
            cc::node_allocation_free((cc::byte*)ptr, class_idx);
        }
    }

    // members
public:
    /// Pointer to the managed T; nullptr indicates an empty handle.
    /// All deallocation metadata (slab base, slot index) is computed from this pointer.
    T* ptr = nullptr;
};
