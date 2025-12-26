#pragma once

#include <clean-core/assert.hh>
#include <clean-core/fwd.hh>
#include <clean-core/utility.hh>

#include <type_traits>

/// Sentinel type used to represent the "no value" state in optional.
/// Construct as cc::nullopt to explicitly assign or compare against empty optionals.
/// Deliberately lacks a default constructor to avoid ambiguity in optional<T> = {}.
struct cc::nullopt_t
{
    enum class _ctor_tag // NOLINT(readability-identifier-naming)
    {
        tag
    };
    explicit constexpr nullopt_t(_ctor_tag) {}
};

namespace cc
{
/// The canonical instance of nullopt_t used to construct or assign empty optionals.
/// Usage: optional<int> opt = nullopt; or if (opt == nullopt).
constexpr nullopt_t nullopt = nullopt_t{nullopt_t::_ctor_tag::tag};
} // namespace cc

/// Sum type representing either a value of type T or no value (T | none), similar to std::optional.
/// Provides a safer subset of std::optional's API: no operator* or operator-> to avoid misuse.
/// Equality comparison available; other relational operators deliberately omitted.
/// Trivially copyable when T is trivially copyable; otherwise uses T's move/copy semantics.
template <class T>
struct cc::optional
{
    // construction
public:
    /// Default optional is empty: has_value() == false.
    optional() = default;

    /// Constructs an optional holding the given value; conditionally explicit.
    /// Forwarding constructor: perfect-forwards the value into internal storage.
    template <class U = std::remove_cv_t<T>>
    explicit(!std::is_convertible_v<U, T>) constexpr optional(U&& value) : _has_value(true) // NOLINT
    {
        new (cc::placement_new, &_storage.value) T(cc::forward<U>(value));
    }

    /// Constructs an empty optional from cc::nullopt; allows explicit empty initialization.
    optional(nullopt_t) {}

    // trivial copy/move/destroy - defaulted when T allows bitwise operations
public:
    /// Move constructor for trivially copyable T: plain memcpy, no destructors run.
    /// When T is trivially copyable, optional<T> is also trivially copyable.
    optional(optional&&)
        requires std::is_trivially_copyable_v<T>
    = default;
    /// Copy constructor for trivially copyable T: plain memcpy, no destructors run.
    optional(optional const&)
        requires std::is_trivially_copyable_v<T>
    = default;
    /// Move assignment for trivially copyable T: plain memcpy, no destructors run.
    optional& operator=(optional&&)
        requires std::is_trivially_copyable_v<T>
    = default;
    /// Copy assignment for trivially copyable T: plain memcpy, no destructors run.
    optional& operator=(optional const&)
        requires std::is_trivially_copyable_v<T>
    = default;

    /// Destructor for trivially destructible T: no-op, compiler-generated.
    ~optional()
        requires std::is_trivially_destructible_v<T>
    = default;

    // non-trivial copy/move/destroy - custom implementation when T requires special handling
public:
    /// Move constructor for non-trivial T: move-constructs value, then destroys rhs and marks it empty.
    /// After this operation, rhs.has_value() == false; avoids double-destruction.
    /// noexcept assumes T's move constructor does not throw (common for move semantics).
    optional(optional&& rhs) noexcept
        requires(!std::is_trivially_copyable_v<T>)
      : _has_value(rhs._has_value)
    {
        if (_has_value)
        {
            new (cc::placement_new, &_storage.value) T(cc::move(rhs._storage.value));
            rhs._storage.value.~T();
            rhs._has_value = false;
        }
    }

    /// Copy constructor for non-trivial T: copy-constructs value when rhs holds one.
    /// Only available when T supports copying.
    optional(optional const& rhs)
        requires(!std::is_trivially_copyable_v<T> && std::is_copy_constructible_v<T>)
      : _has_value(rhs._has_value)
    {
        if (_has_value)
            new (cc::placement_new, &_storage.value) T(rhs._storage.value);
    }

    /// Move assignment for non-trivial T: moves or constructs from rhs, handling all state combinations.
    /// When rhs holds a value: move-assigns if *this holds a value, move-constructs otherwise.
    /// When rhs is empty: destroys *this's value if present.
    /// Leaves rhs engaged with a moved-from value (matches std::optional behavior).
    /// This design makes subobject self-move safe: my_opt = cc::move(my_opt.value().sub_opt) works.
    optional& operator=(optional&& rhs) noexcept
        requires(!std::is_trivially_copyable_v<T>)
    {
        if (rhs._has_value)
        {
            if (_has_value)
                _storage.value = cc::move(rhs._storage.value);
            else
                new (cc::placement_new, &_storage.value) T(cc::move(rhs._storage.value));

            _has_value = true;
        }
        else if (_has_value)
        {
            _storage.value.~T();
            _has_value = false;
        }

        return *this;
    }

    /// Copy assignment for non-trivial T: copies or constructs from rhs, handling all state combinations.
    /// When rhs holds a value: copy-assigns if *this holds a value, copy-constructs otherwise.
    /// When rhs is empty: destroys *this's value if present.
    /// Includes self-assignment check to avoid unnecessary work.
    /// Only available when T supports both copy construction and copy assignment.
    optional& operator=(optional const& rhs)
        requires(!std::is_trivially_copyable_v<T> && std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>)
    {
        if (this != &rhs)
        {
            if (rhs._has_value)
            {
                if (_has_value)
                    _storage.value = rhs._storage.value;
                else
                    new (cc::placement_new, &_storage.value) T(rhs._storage.value);

                _has_value = true;
            }
            else if (_has_value)
            {
                _storage.value.~T();
                _has_value = false;
            }
        }

        return *this;
    }

    /// Destructor for non-trivial T: destroys the held value if present.
    ~optional()
        requires(!std::is_trivially_destructible_v<T>)
    {
        if (_has_value)
            _storage.value.~T();
    }

    // queries and access
public:
    /// Returns true if this optional holds a value, false if empty.
    [[nodiscard]] bool has_value() const { return _has_value; }

    /// Returns a reference to the held value, preserving the value category of the optional itself.
    /// Precondition: has_value() == true.
    /// Uses deducing this (C++23) to forward lvalue/rvalue/const qualifiers from the optional to T.
    template <class Self>
    [[nodiscard]] auto&& value(this Self&& self)
    {
        CC_ASSERT(self.has_value(), "attempted to access value of empty optional");
        return static_cast<Self&&>(self)._storage.value;
    }

    // comparison
public:
    /// Equality comparison: two optionals are equal if both empty or both hold equal values.
    /// Only available when T supports operator==.
    /// Returns false if one is empty and the other is not.
    [[nodiscard]] friend bool operator==(optional const& lhs, optional const& rhs)
        requires requires(T v) { bool(v == v); }
    {
        if (lhs._has_value != rhs._has_value)
            return false;
        if (lhs._has_value)
            return lhs._storage.value == rhs._storage.value;
        return true;
    }

    /// Equality comparison with a value: optional is equal to the value if it holds an equal value.
    /// Returns false if the optional is empty.
    /// Avoids constructing a temporary optional during comparison.
    [[nodiscard]] friend bool operator==(optional const& lhs, T const& rhs)
        requires requires(T v) { bool(v == v); }
    {
        return lhs._has_value && lhs._storage.value == rhs;
    }

    /// Equality comparison with bool: deleted when T is not bool to prevent implicit conversions.
    /// When T is bool, this allows comparing optional<bool> with true/false.
    /// When T is not bool, this prevents optional<int> from comparing with true/false.
    [[nodiscard]] bool operator==(bool) const
        requires(!std::is_same_v<T, bool>)
    = delete;

    // members
private:
    /// Uninitialized storage with proper size and alignment for T.
    /// Value is constructed in-place when the optional is engaged.
    cc::storage_for<T> _storage;

    /// True when _storage.value holds a live T object; false when storage is uninitialized.
    /// Placed after _storage so that this == &_storage.value (pointer arithmetic optimization).
    bool _has_value = false;
};
