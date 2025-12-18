# Coding Guidelines

This document defines the coding standards and design principles for the clean-core library.
These guidelines prioritize correctness, performance, maintainability, and readability for a foundational C++ library.

---

## Language & Compiler Requirements

**C++ Standard:** C++23 minimum

**Supported Platforms:**
- 64-bit only (Windows, Linux, macOS)
- Architectures: x64 and ARM64

**Compiler Support:**
- **First-class:** Clang and MSVC *(TODO: minimum versions)*
- **Second-class:** GCC (temporary issues may occur, fixes welcome)

---

## Naming Conventions

| Element                                       | Convention    | Example                            |
|-----------------------------------------------|---------------|------------------------------------|
| Types (struct, class, enum, concept, typedef) | `snake_case`  | `string_view`, `dynamic_array`     |
| Functions                                     | `snake_case`  | `to_string()`, `get_size()`        |
| Variables                                     | `snake_case`  | `buffer_size`, `input_data`        |
| Enum values                                   | `snake_case`  | `error_none`, `format_utf8`        |
| Namespaces                                    | `snake_case`  | `cc`, `detail`                     |
| Template parameters                           | `UpperCase`   | `template <class T, int Size>`     |
| Private members                               | `_snake_case` | `_internal_state`, `_cached_value` |
| Macros                                        | `UPPER_CASE`  | `CC_ASSERT`, `CC_FORCE_INLINE`     |

**Note:** Template parameters are often re-exposed as `snake_case` type aliases inside the struct/class.

---

## Code Style & Formatting

### General Principles

- **clang-format is mandatory.** Use trailing `//` comments to control line breaking when needed.
- One declaration per line. Never `int a, b;`
- Line length should be reasonable for diffs (~100 chars recommended).
- Prefer short, tight sections optimized for skimming.

### Type Declarations & Const

**East const everywhere:**
```cpp
T const x = ...;
span<T const> data;
cc::string const& name;
```

**Prefer almost-always-auto style:**
```cpp
auto const x = T{ ... };  // preferred
T const x = { ... };       // acceptable but less consistent
T x;                       // fine if initialized later
```

### Headers & Forward Declarations

- Forward-declare all important types in a `fwd.hh` file.
- Keep headers lightweight when possible. Use the [vimpl pattern](https://solidean.com/blog/2025/the-vimpl-pattern-for-cpp/) for non-performance-critical types.
- Avoid opening namespaces unnecessarily. Prefer qualified names:
  ```cpp
  struct cc::string { ... };
  void cc::to_string(...) { ... }
  ```
  This works when the declaration already exists.

### Performance-Critical Code

All functions expected to be inlined **must** be implemented in headers.
Non-performance-critical functions can live in `.cc` files to reduce compile times.

**Goal:** Maximum performance without requiring LTO, while remaining compile-time conscious.

---

## Language Features

### Templates & Constraints

- Use concepts, `requires` constraints, and `static_assert` judiciously.
- Prefer `requires` + `static_assert` over SFINAE and pre-C++20 template metaprogramming.
- Explicitly prefix `cc::` even inside the library when taking templated arguments to prevent unintended ADL capture.
- Non-trivial ADL usage must always be explicitly marked.

### constexpr & noexcept

- **constexpr:** Use only when you actively anticipate usage in a constexpr context.
- **noexcept:** Do not spam. Performance benefit is niche; only add where measurably beneficial.

### Attributes & Annotations

- `[[nodiscard]]` for non-void functions, **except** obvious getters (`get_xyz`, `is_xyz`, `has_xyz`).
- All single-argument constructors must be `explicit` unless there's a **documented** reason for implicit conversion.

### Constructors vs Factory Methods

Avoid non-trivial constructors. Prefer static factory methods instead:

```cpp
// Good: factory method with clear name, can fail, returns specific type
struct texture {
    [[nodiscard]] static cc::result<texture> from_file(cc::string_view path);
    [[nodiscard]] static texture from_dimensions(int width, int height);

private:
    texture() = default;  // keep ctor simple
};

// Avoid: complex ctor that can fail
struct texture {
    texture(cc::string_view path);  // what if file doesn't exist?
};
```

**Benefits of factory methods:**
- Can return `cc::result` or `optional` to properly handle failures
- Can return different types (base class, interface, optimized variants)
- Have descriptive names that clarify intent
- More flexible for future changes

### Initialization

Prefer designated initializers where possible:
```cpp
auto const config = Config{
    .buffer_size = 1024,
    .enable_cache = true,
    .timeout_ms = 5000
};
```

Fall back to braced initialization when designated initializers aren't applicable:
```cpp
auto const vec = std::vector<int>{1, 2, 3};
```

---

## API Design & Ownership Semantics

### Pointers & References

- **Raw pointers are non-owning optional references.** They never own memory (except internally in container implementations).
- Prefer view types (`span`, `string_view`, `function_ref`) when ownership is not required.
- When ownership is required, prefer value types or unique/move-only types over shared ownership.
  - Shared ownership (e.g., `shared_ptr`) adds complexity and overhead; use only when truly necessary.

### Passing Arguments

| Size / Ownership               | Convention                             |
|--------------------------------|----------------------------------------|
| Small value types (≤ 32 bytes) | Pass by value                          |
| No ownership transfer          | Pass by `const&` or view type          |
| Ownership transfer             | Pass by value, then `cc::move` internally |

Actively prevent unnecessary copies (e.g., `cc::vector` copies). Use `const&` or view types.

### Move & Copy Semantics

When defining copy/move constructors or assignment operators, **always specify all four** and explicitly default or delete them:
```cpp
T(T const&) = default;
T(T&&) = default;
T& operator=(T const&) = default;
T& operator=(T&&) = default;
```

---

## Error Handling

Clean-core uses a tiered error handling philosophy:

| Mechanism                 | Use Case                                                             |
|---------------------------|----------------------------------------------------------------------|
| `CC_ASSERT`               | Invariant violations, preconditions, postconditions (programmer bugs) |
| Exceptions                | Exceptional errors requiring non-local control flow                  |
| `cc::result` / `optional` | Frequent or expected failures                                        |

Use `CC_ASSERT` liberally to verify preconditions and invariants throughout the codebase.

---

## Integer & Numeric Types

- Use `int` when the size is unimportant (magnitude < millions).
- Use explicitly sized types (`i32`, `u64`, `f32`, etc.) when bit width or precision matters.
- Avoid "magic sentinels" like `-1` for invalid states. Prefer `optional` or `variant` unless there's a justified performance or memory reason.

---

## Concurrency

All functions and types are **single-threaded** ("externally synchronized") by default unless noted.
Thread-safe types typically include `atomic_` in their name.

---

## Operators & Overloading

Use **hidden friends** for operator overloading where possible:
```cpp
struct vec3 {
    friend vec3 operator+(vec3 a, vec3 b) { return { a.x + b.x, ... }; }
};
```
**Benefits:** Improved compile times, reduced symbol pollution, better ADL control.

---

## Visibility & API Grouping

Use **redundant visibility modifiers** to logically group APIs.
Place group comments above the modifier, preserving space for individual documentation comments:

```cpp
class foo {
    // construction
public:
    foo() = default;

    // queries
public:
    /// returns the current size
    int size() const;

    // modifiers
public:
    /// resizes the container to n elements
    void resize(int n);
};
```

---

## Comments & Documentation

### Code Comments

- Explain **rationale** and **why**, not just what.
- Use comments to provide grouping and structure.
- Skimming the comments of a longer function should reveal its logical flow.

### Documentation Comments

Use `///` for documentation. **No doc tags, no XML.**

**Design philosophy:** Comments are most often read directly in source code, so they must read naturally without rich formatting tools.

- Write plain, natural language that describes everything important.
- Insert blank lines every few lines to break up walls of text and improve skimmability.
- Keep line length reasonable for diffs.

```cpp
/// allocates a new buffer with the specified capacity
///
/// if allocation fails, returns an error
/// the buffer is zero-initialized
///
/// NOTE: capacity must be > 0
[[nodiscard]] cc::result<buffer> allocate_buffer(int capacity);
```

---

## Macros

Macros must be **justified.** They should provide value that language constructs cannot replicate.

**Valid use cases:**

| Macro              | Justification                                        |
|--------------------|------------------------------------------------------|
| `CC_ASSERT(expr)`  | Stringification and conditional compilation          |
| `CHECK(a < b)`     | Expression capture for diagnostics                   |
| `LOG(...)`         | Conditional compilation based on build config        |
| `CC_DEFER`         | Scope-exit semantics not realizable without macros   |
| `CC_FORCE_INLINE`  | Wraps compiler-dependent attributes portably         |

If a language feature (template, `constexpr`, inline function) can achieve the same result, prefer it over a macro.

---

## Testing

Test every feature you can using the **nexus companion library.**

**Core testing constructs:**

```cpp
TEST("descriptive test name")
{
    // test body
}

TEST("test with config", disabled, seed(123))
{
    // test body with configuration
}
```

**Organizing tests:**

```cpp
TEST("feature group")
{
    SECTION("specific behavior")
    {
        CHECK(value == expected);
    }

    for (auto i = 0; i < 10; ++i)
    {
        SECTION("subsection nr {}", i)  // parameterized sections
        {
            REQUIRE(critical_condition);
        }
    }
}
```

**Assertions:**
- `CHECK(expr)` — Record failure but continue test
- `REQUIRE(expr)` — Abort test on failure

**Advanced testing:**
- **Fuzz testing** — Property-based testing with random inputs
- **Performance testing** — Measure execution characteristics
- **Benchmarking** — Compare performance across implementations

---

## Stability & Evolution

**API Stability:** High priority. This is a foundational library. We reserve the right to make breaking changes where they significantly improve the library.

**ABI Stability:** Low priority. Expect to build clean-core from source in most environments.

---

## Summary Checklist

- [ ] East const (`T const`)
- [ ] Almost-always-auto style
- [ ] clang-format applied
- [ ] Designated initializers where possible
- [ ] Non-trivial logic uses static factory methods, not constructors
- [ ] Forward declarations in `fwd.hh`
- [ ] Single-argument ctors are `explicit`
- [ ] All four copy/move operations explicitly defined
- [ ] `[[nodiscard]]` on non-void, non-getter functions
- [ ] `CC_ASSERT` for preconditions and invariants
- [ ] Raw pointers are non-owning
- [ ] Prefer value types or move-only types over shared ownership
- [ ] Avoid unnecessary copies (use `const&` or view types)
- [ ] Small values (≤ 32 bytes) passed by value
- [ ] Documentation uses `///` with natural language
- [ ] Group comments above visibility modifiers
- [ ] Hidden friends for operators where possible
- [ ] No unnecessary `constexpr` or `noexcept`
- [ ] Macros are justified
- [ ] Tests written in nexus
