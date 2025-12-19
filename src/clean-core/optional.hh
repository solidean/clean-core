#pragma once

#include <clean-core/assert.hh>
#include <clean-core/fwd.hh>
#include <clean-core/utility.hh>


struct cc::nullopt_t
{
    // NOTE: no default ctor must be declared
    //       otherwise optional<T> = {} does not work
    //       (due to ambiguous overload)
    enum class _ctor_tag
    {
        tag
    };
    explicit constexpr nullopt_t(_ctor_tag) {}
};
namespace cc
{
constexpr nullopt_t nullopt = nullopt_t{nullopt_t::_ctor_tag::tag};
}

// sum type of T | none
// mirrors std::optional closely while removing some edges
// no op*, op-> because they are error-prone
// no comparisons except equality visible
template <class T>
struct cc::optional
{
    // ctors
public:
    optional() = default;

    // implicit conversion
    optional(T value) : _has_value(true) { new (cc::placement_new, &_storage.value) T(cc::move(value)); }
    optional(nullopt_t) {}

    // trivial types
public:
    optional(optional&&)
        requires std::is_trivially_copyable_v<T>
    = default;
    optional(optional const&)
        requires std::is_trivially_copyable_v<T>
    = default;
    optional& operator=(optional&&)
        requires std::is_trivially_copyable_v<T>
    = default;
    optional& operator=(optional const&)
        requires std::is_trivially_copyable_v<T>
    = default;

    ~optional()
        requires std::is_trivially_destructible_v<T>
    = default;

    // non-trivial types
public:
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

    optional(optional const& rhs)
        requires(!std::is_trivially_copyable_v<T> && std::is_copy_constructible_v<T>)
      : _has_value(rhs._has_value)
    {
        if (_has_value)
            new (cc::placement_new, &_storage.value) T(rhs._storage.value);
    }

    optional& operator=(optional&& rhs) noexcept
        requires(!std::is_trivially_copyable_v<T>)
    {
        if (rhs._has_value)
        {
            if (_has_value)
                _storage.value = cc::move(rhs._storage.value);
            else
                new (cc::placement_new, &_storage.value) T(cc::move(rhs._storage.value));

            _has_value = true; // NOTE: _before_ rhs so self-move stays disengaged
            rhs._storage.value.~T();
            rhs._has_value = false;
        }
        else if (_has_value)
        {
            _storage.value.~T();
            _has_value = false;
        }

        return *this;
    }

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

    ~optional()
        requires(!std::is_trivially_destructible_v<T>)
    {
        if (_has_value)
            _storage.value.~T();
    }

    // api
public:
    // true if this holds a valid value
    // false if it doesn't
    [[nodiscard]] bool has_value() const { return _has_value; }

    // returns the value held by this (with same value category as the optional)
    // is only valid if has_value is true
    template <class Self>
    [[nodiscard]] auto&& value(this Self&& self)
    {
        CC_ASSERT(self.has_value(), "attempted to access value of empty optional");
        return static_cast<Self&&>(self)._storage.value;
    }

    // operators
public:
    [[nodiscard]] friend bool operator==(optional const& lhs, optional const& rhs)
        requires requires(T v) { bool(v == v); }
    {
        if (lhs._has_value != rhs._has_value)
            return false;
        if (lhs._has_value)
            return lhs._storage.value == rhs._storage.value;
        return true;
    };

private:
    // provides properly sized & aligned storage for T
    cc::storage_for<T> _storage;

    // trailing member so this == &_storage.value
    bool _has_value = false;
};
