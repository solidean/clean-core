#include "allocation.hh"

#include <clean-core/assertf.hh>
#include <clean-core/macros.hh>
#include <clean-core/utility.hh>

#include <cstdlib>

namespace
{
/// Static function implementations for the system memory resource.
/// These ignore the userdata parameter as the system allocator is stateless.

cc::byte* system_allocate_bytes(cc::isize bytes, cc::isize alignment, void* userdata)
{
    CC_UNUSED(userdata);

    CC_ASSERT(alignment > 0 && cc::is_power_of_two(alignment), "alignment must be a power of 2");

    // Contract: bytes == 0 always returns nullptr
    if (bytes == 0)
        return nullptr;

    // Delegate to try_allocate and assert success
    cc::byte* p = nullptr;

#ifdef CC_OS_WINDOWS
    p = static_cast<cc::byte*>(_aligned_malloc(bytes, alignment));
#else
    // Use posix_memalign instead of std::aligned_alloc to avoid the bytes % alignment == 0 requirement.
    // posix_memalign requires alignment >= sizeof(void*), so we clamp to that minimum.
    void* raw_ptr = nullptr;
    cc::isize effective_alignment = alignment < sizeof(void*) ? sizeof(void*) : alignment;
    int result = posix_memalign(&raw_ptr, effective_alignment, bytes);
    p = result == 0 ? static_cast<cc::byte*>(raw_ptr) : nullptr;
#endif

    CC_ASSERTF(p != nullptr, "allocation failed: requested {} bytes with alignment {}", bytes, alignment);
    return p;
}

cc::byte* system_try_allocate_bytes(cc::isize bytes, cc::isize alignment, void* userdata)
{
    CC_UNUSED(userdata);

    CC_ASSERT(alignment > 0 && cc::is_power_of_two(alignment), "alignment must be a power of 2");

    // Contract: bytes == 0 always returns nullptr
    if (bytes == 0)
        return nullptr;

    // Use platform-specific aligned allocation:
    // - Windows: _aligned_malloc does not strictly require bytes % alignment == 0
    // - POSIX: posix_memalign does not require bytes % alignment == 0 (unlike std::aligned_alloc)
#ifdef CC_OS_WINDOWS
    return static_cast<cc::byte*>(_aligned_malloc(bytes, alignment));
#else
    // Use posix_memalign instead of std::aligned_alloc to avoid the bytes % alignment == 0 requirement.
    // posix_memalign requires alignment >= sizeof(void*), so we clamp to that minimum.
    void* raw_ptr = nullptr;
    cc::isize effective_alignment = alignment < sizeof(void*) ? sizeof(void*) : alignment;
    int result = posix_memalign(&raw_ptr, effective_alignment, bytes);
    return result == 0 ? static_cast<cc::byte*>(raw_ptr) : nullptr;
#endif
}

void system_deallocate_bytes(cc::byte* p, cc::isize bytes, cc::isize alignment, void* userdata)
{
    CC_UNUSED(bytes);
    CC_UNUSED(alignment);
    CC_UNUSED(userdata);

    // Contract: size and alignment are provided for resources that need them (e.g., pooling),
    //           but standard malloc/free don't require them for deallocation.
    // IMPORTANT: Must use matching free function for the platform's allocator:
    // - Windows: _aligned_malloc requires _aligned_free (not free())
    // - POSIX: std::aligned_alloc uses std::free (not a special free function)
#ifdef CC_OS_WINDOWS
    _aligned_free(p);
#else
    std::free(p);
#endif
}

cc::isize system_try_resize_bytes_in_place(cc::byte* p,
                                           cc::isize old_bytes,
                                           cc::isize min_bytes,
                                           cc::isize max_bytes,
                                           cc::isize alignment,
                                           void* userdata)
{
    CC_UNUSED(userdata);

    CC_ASSERT(p != nullptr, "cannot resize null pointer");
    CC_ASSERT(alignment > 0 && cc::is_power_of_two(alignment), "alignment must be a power of 2");
    CC_ASSERT(old_bytes > 0, "old_bytes must be positive");
    CC_ASSERT(1 <= min_bytes && min_bytes <= max_bytes, "must have 1 <= min_bytes <= max_bytes");

    CC_UNUSED(p);
    CC_UNUSED(old_bytes);
    CC_UNUSED(min_bytes);
    CC_UNUSED(max_bytes);
    CC_UNUSED(alignment);

    // Standard malloc/aligned_alloc do not support in-place resize.
    // Return -1 to signal failure (contract: allocation remains valid at p with size old_bytes).
    // Rationale: Unlike realloc, we cannot move the allocation here because that would
    //            invalidate pointers into the allocation (e.g., vec.push_back(vec[0])).
    //            Containers must handle resize failure by allocating a new block separately.
    return -1;
}

/// System memory resource instance stored in the data segment.
/// This is the default fallback when cc::allocation<T>::custom_resource is nullptr.
/// Stored in the data segment (not on heap) so it remains valid during static initialization,
/// making cc::default_memory_resource safe to use in global/static constructors.
constinit cc::memory_resource const system_memory_resource = {
    .allocate_bytes = system_allocate_bytes,
    .try_allocate_bytes = system_try_allocate_bytes,
    .deallocate_bytes = system_deallocate_bytes,
    .try_resize_bytes_in_place = system_try_resize_bytes_in_place,
    .userdata = nullptr,
};

} // namespace

constinit cc::memory_resource const* const cc::default_memory_resource = &system_memory_resource;
