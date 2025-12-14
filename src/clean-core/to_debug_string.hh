#pragma once

#include <clean-core/assert.hh>
#include <clean-core/to_string.hh>

#include <type_traits>
#include <utility> // for tuple_size

namespace cc
{
struct debug_string_config
{
    // not strict for now
    int max_length = 100;
};

// Converts a value to a developer-facing debug string.
// Best-effort, non-semantic, and intended only for diagnostics.
//
// Strategy (in rough order):
//   - Use to_string(v) if available
//   - Use v.to_string() if available
//   - For collections, recursively format elements as [v0, v1, ...]
//   - For tuple-likes, recursively format elements as (v0, v1, ...)
//   - Otherwise emit raw memory dump
//
// No stability, completeness, or user-facing guarantees.
// Output may change, be lossy, or depend on build/configuration.
template <class T>
[[nodiscard]] string to_debug_string(T const& v, debug_string_config const& cfg = {});

//
// Implementation
//

namespace impl
{
template <class T>
bool to_debug_string_append_elem(string& s, T const& v, debug_string_config const& cfg)
{
    if (int(s.size()) >= cfg.max_length)
    {
        s += ", ...";
        return false;
    }

    if (s.size() > 1)
        s += ", ";

    s += cc::to_debug_string(v, cfg);

    return true;
}
template <class T, std::size_t... I>
void to_debug_string_append_tuple(string& s, T const& v, debug_string_config const& cfg, std::index_sequence<I...>)
{
    (cc::impl::to_debug_string_append_elem(s, std::get<I>(v), cfg) && ...);
}
} // namespace impl

template <class T>
[[nodiscard]] string to_debug_string(T const& v, debug_string_config const& cfg)
{
    if constexpr (requires { to_string(v); })
    {
        return string(to_string(v));
    }
    else if constexpr (requires { v.to_string(); })
    {
        return string(v.to_string());
    }
    else if constexpr (requires {
                           std::begin(v);
                           std::end(v);
                       })
    {
        auto s = string("[");
        // FIXME: could cause recursive instantiation?
        for (auto&& e : v)
        {
            if (int(s.size()) >= cfg.max_length)
            {
                s += ", ...";
                break;
            }

            if (s.size() > 1)
                s += ", ";
            s += cc::to_debug_string(e, cfg);
        }
        s += "]";
        return s;
    }
    else if constexpr (requires { std::tuple_size<T>::value; })
    {
        auto s = string("(");
        cc::impl::to_debug_string_append_tuple(s, v, cfg, std::make_index_sequence<std::tuple_size<T>::value>{});
        s += ")";
        return s;
    }
    else
    {
        auto s = string("0x");
        auto const align = alignof(T);
        auto const p_v = (unsigned char const*)&v;
        for (size_t i = 0; i < sizeof(T); ++i)
        {
            if (i > 0 && i % align == 0)
                s += "_";
            s += std::format("{:02X}", p_v[i]);
        }
        return s;
    }
}
} // namespace cc
