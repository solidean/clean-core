# clean-core

A modern C++23 foundational library focused on correctness, performance, and maintainability.

## Features

- **Modern C++23** - Leverages concepts, designated initializers, and contemporary language features
- **Performance-conscious** - Header-based for critical paths, compile-time optimized
- **Robust error handling** - Tiered approach with assertions, exceptions, and result types
- **Zero-cost abstractions** - View types, move semantics, and careful ownership design
- **Tested** - Comprehensive test coverage via the nexus companion library

## Platforms

- **Supported:** Windows, Linux, macOS (x64 and ARM64, 64-bit only)
- **Compilers:** Clang and MSVC (first-class), GCC (second-class)

## Code Style at a Glance

```cpp
/// texture resource with efficient loading and caching
///
/// textures are immutable after creation
/// use from_file or from_dimensions for construction
struct texture
{
    // factories
public:
    /// loads texture from file, returns error on failure
    [[nodiscard]] static cc::result<texture> from_file(cc::string_view path);

    [[nodiscard]] static texture from_dimensions(int width, int height);

    texture() = default;

    // queries
public:
    int width() const { return _width; }
    int height() const { return _height; }

    // operations
public:
    /// resizes the texture, preserving content where possible
    void resize(int new_width, int new_height);

private:
    int _width = 0;
    int _height = 0;
    cc::array<cc::byte> _data;
};
```

**Key principles demonstrated:**
- East const (`T const`) and almost-always-auto
- Static factory methods for non-trivial construction
- `[[nodiscard]]` on functions returning values
- Documentation with `///` in natural language
- Visibility grouping with comments above modifiers
- Private members with `_prefix`
- Explicit rule-of-four for special members
- Move-only types preferred over shared ownership

## Documentation

- [Coding Guidelines](docs/Coding%20Guidelines.md) - Comprehensive style and design principles
- API documentation in headers using `///` comments

## Testing

Tests use the **nexus** companion library:

```cpp
TEST("texture creation")
{
    SECTION("from valid file")
    {
        auto const result = texture::from_file("test.png");
        REQUIRE(result.has_value());
        CHECK(result->width() > 0);
    }

    SECTION("from invalid path")
    {
        auto const result = texture::from_file("nonexistent.png");
        CHECK(result.has_error());
    }
}
```

## Philosophy

clean-core prioritizes API stability and deliberate design over rapid iteration. 
Breaking changes are considered when they substantially improve the library. 
ABI stability is a non-goal, cc is built from source.

Performance matters. Correctness matters more. Maintainability matters most.
