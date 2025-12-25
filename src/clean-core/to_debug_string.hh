#pragma once

#include <clean-core/assert.hh>
#include <clean-core/fwd.hh>
#include <clean-core/string_view.hh>
#include <clean-core/to_string.hh>

#include <type_traits>
#include <utility> // for tuple_size

// remove me once we have it
#include <format>
#include <string_view>

namespace cc
{
struct debug_string_config
{
    // not strict for now
    isize max_length = 100;
};

// Converts a value to a developer-facing debug string.
// Best-effort, non-semantic, and intended only for diagnostics.
//
// Strategy (in order):
//   - String-likes: wrap in double quotes "..." (never empty output)
//   - char: wrap in single quotes '...' with escape sequences for control/non-printable chars
//     (printables and spaces show as-is, control chars like \n, \t are escaped,
//      other non-printables show as \xHH hex codes)
//   - Use to_string(v) if available
//   - Use v.to_string() if available
//   - For collections, recursively format elements as [v0, v1, ...]
//   - For tuple-likes, recursively format elements as (v0, v1, ...)
//   - Otherwise emit raw memory dump
//
// Design rationale:
//   - String-likes and chars are wrapped in quotes to ensure non-empty output
//     (e.g., empty string shows as "" instead of nothing)
//   - Chars use escape sequences to make control/special characters visible
//     (e.g., newline shows as '\n' instead of an actual line break)
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
    if (isize(s.size()) >= cfg.max_length)
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
    (void)(cc::impl::to_debug_string_append_elem(s, std::get<I>(v), cfg) && ...);
}
} // namespace impl

template <class T>
[[nodiscard]] string to_debug_string(T const& v, debug_string_config const& cfg)
{
    // TODO: migrate once cc::string landed
    if constexpr (requires { std::string_view(v); })
    {
        auto s = cc::string("\"");
        s += std::string_view(v);
        s += '\"';
        return s;
    }
    else if constexpr (std::is_same_v<T, char>)
    {
        auto s = cc::string("'");

        // Escape control and non-printable characters
        if (v == '\0')
            s += "\\0";
        else if (v == '\n')
            s += "\\n";
        else if (v == '\r')
            s += "\\r";
        else if (v == '\t')
            s += "\\t";
        else if (v == '\v')
            s += "\\v";
        else if (v == '\f')
            s += "\\f";
        else if (v == '\b')
            s += "\\b";
        else if (v == '\a')
            s += "\\a";
        else if (v == '\\')
            s += "\\\\";
        else if (v == '\'')
            s += "\\'";
        else if (v < 32 || v == 127) // Other control characters
            s += std::format("\\x{:02X}", static_cast<unsigned char>(v));
        else // Printable characters (including space)
            s += v;

        s += '\'';
        return s;
    }
    else if constexpr (requires { to_string(v); })
    {
        return cc::string(to_string(v));
    }
    else if constexpr (requires { v.to_string(); })
    {
        return cc::string(v.to_string());
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
