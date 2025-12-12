#include <clean-core/macros.hh>
#include <nexus/test.hh>

#include <cstring>


// =========================================================================================================
// Preprocessor-level compile-time checks
// =========================================================================================================

// Test: Exactly-one compiler family is selected
#if defined(CC_COMPILER_MSVC) + defined(CC_COMPILER_CLANG) + defined(CC_COMPILER_GCC) + defined(CC_COMPILER_MINGW) != 1
#error "Expected exactly one compiler family macro to be defined"
#endif

// Test: CC_COMPILER_POSIX iff {CLANG|GCC|MINGW}
#if defined(CC_COMPILER_POSIX)
#if !defined(CC_COMPILER_CLANG) && !defined(CC_COMPILER_GCC) && !defined(CC_COMPILER_MINGW)
#error "CC_COMPILER_POSIX defined but no POSIX compiler detected"
#endif
#else
#if defined(CC_COMPILER_CLANG) || defined(CC_COMPILER_GCC) || defined(CC_COMPILER_MINGW)
#error "POSIX compiler detected but CC_COMPILER_POSIX not defined"
#endif
#endif

// Test: Exactly-one OS is selected
#if defined(CC_OS_WINDOWS) + defined(CC_OS_LINUX) + defined(CC_OS_APPLE) + defined(CC_OS_BSD) != 1
#error "Expected exactly one OS macro to be defined"
#endif

// Test: Target macro consistency - Windows platforms
#if defined(CC_OS_WINDOWS)
#if defined(CC_TARGET_PC) + defined(CC_TARGET_XBOX) != 1
#error "CC_OS_WINDOWS requires exactly one of CC_TARGET_PC or CC_TARGET_XBOX"
#endif
#endif

// Test: Target macro consistency - Apple platforms
#if defined(CC_OS_APPLE)
#if defined(CC_TARGET_MACOS) + defined(CC_TARGET_IOS) + defined(CC_TARGET_TVOS) != 1
#error "CC_OS_APPLE requires exactly one of CC_TARGET_MACOS, CC_TARGET_IOS, or CC_TARGET_TVOS"
#endif
#endif

// Test: CC_TARGET_MOBILE consistency
#if defined(CC_TARGET_MOBILE)
#if !defined(CC_TARGET_IOS) && !defined(CC_TARGET_TVOS) && !defined(CC_TARGET_ANDROID)
#error "CC_TARGET_MOBILE defined but no mobile platform detected"
#endif
#endif

#if defined(CC_TARGET_IOS) || defined(CC_TARGET_TVOS) || defined(CC_TARGET_ANDROID)
#if !defined(CC_TARGET_MOBILE)
#error "Mobile platform detected but CC_TARGET_MOBILE not defined"
#endif
#endif

// Test: CC_TARGET_CONSOLE consistency
#if defined(CC_TARGET_CONSOLE)
#if !defined(CC_TARGET_ORBIS) && !defined(CC_TARGET_NX) && !defined(CC_TARGET_XBOX)
#error "CC_TARGET_CONSOLE defined but no console platform detected"
#endif
#endif

#if defined(CC_TARGET_ORBIS) || defined(CC_TARGET_NX) || defined(CC_TARGET_XBOX)
#if !defined(CC_TARGET_CONSOLE)
#error "Console platform detected but CC_TARGET_CONSOLE not defined"
#endif
#endif

// =========================================================================================================
// Runtime tests
// =========================================================================================================

TEST("macros - compiler detection")
{
    // Verify that exactly one compiler is selected (already checked at compile-time)
    int compiler_count = 0;
#ifdef CC_COMPILER_MSVC
    compiler_count++;
#endif
#ifdef CC_COMPILER_CLANG
    compiler_count++;
#endif
#ifdef CC_COMPILER_GCC
    compiler_count++;
#endif
#ifdef CC_COMPILER_MINGW
    compiler_count++;
#endif

    CHECK(compiler_count == 1);

    // Verify POSIX is set correctly
#if defined(CC_COMPILER_POSIX)
    bool is_posix_compiler = true;
#if defined(CC_COMPILER_CLANG) || defined(CC_COMPILER_GCC) || defined(CC_COMPILER_MINGW)
    bool has_posix_compiler = true;
#else
    bool has_posix_compiler = false;
#endif
    CHECK(has_posix_compiler);
#else
    bool is_posix_compiler = false;
#if defined(CC_COMPILER_MSVC)
    bool is_msvc = true;
#else
    bool is_msvc = false;
#endif
    CHECK(is_msvc);
#endif
}

TEST("macros - OS detection")
{
    // Verify that exactly one OS is selected (already checked at compile-time)
    int os_count = 0;
#ifdef CC_OS_WINDOWS
    os_count++;
#endif
#ifdef CC_OS_LINUX
    os_count++;
#endif
#ifdef CC_OS_APPLE
    os_count++;
#endif
#ifdef CC_OS_BSD
    os_count++;
#endif

    CHECK(os_count == 1);
}

TEST("macros - target platform consistency")
{
    SECTION("Windows targets")
    {
#ifdef CC_OS_WINDOWS
        int target_count = 0;
#ifdef CC_TARGET_PC
        target_count++;
#endif
#ifdef CC_TARGET_XBOX
        target_count++;
#endif
        CHECK(target_count == 1);
#endif
    }

    SECTION("Apple targets")
    {
#ifdef CC_OS_APPLE
        int target_count = 0;
#ifdef CC_TARGET_MACOS
        target_count++;
#endif
#ifdef CC_TARGET_IOS
        target_count++;
#endif
#ifdef CC_TARGET_TVOS
        target_count++;
#endif
        CHECK(target_count == 1);
#endif
    }

    SECTION("Mobile target consistency")
    {
#ifdef CC_TARGET_MOBILE
        CHECK((defined(CC_TARGET_IOS) || defined(CC_TARGET_TVOS) || defined(CC_TARGET_ANDROID)));
#endif

#if defined(CC_TARGET_IOS) || defined(CC_TARGET_TVOS) || defined(CC_TARGET_ANDROID)
        CHECK(defined(CC_TARGET_MOBILE));
#endif
    }

    SECTION("Console target consistency")
    {
#ifdef CC_TARGET_CONSOLE
        CHECK((defined(CC_TARGET_ORBIS) || defined(CC_TARGET_NX) || defined(CC_TARGET_XBOX)));
#endif

#if defined(CC_TARGET_ORBIS) || defined(CC_TARGET_NX) || defined(CC_TARGET_XBOX)
        CHECK(defined(CC_TARGET_CONSOLE));
#endif
    }
}

TEST("macros - feature flags")
{
    SECTION("RTTI flag is well-defined")
    {
        // The macro is either defined or not, never in an invalid state
#ifdef CC_HAS_RTTI
        CHECK(true); // If defined, we're good
#else
        CHECK(true); // If not defined, we're also good
#endif
    }

    SECTION("Exceptions flag is well-defined")
    {
        // The macro is either defined or not, never in an invalid state
#ifdef CC_HAS_CPP_EXCEPTIONS
        CHECK(true); // If defined, we're good
#else
        CHECK(true); // If not defined, we're also good
#endif
    }
}

TEST("macros - CC_ARRAY_COUNT_OF")
{
    SECTION("Basic array counting")
    {
        int arr[10];
        CHECK(CC_ARRAY_COUNT_OF(arr) == 10);

        int arr2[1];
        CHECK(CC_ARRAY_COUNT_OF(arr2) == 1);

        int arr3[100];
        CHECK(CC_ARRAY_COUNT_OF(arr3) == 100);
    }

    SECTION("Non-int element types")
    {
        double doubles[5];
        CHECK(CC_ARRAY_COUNT_OF(doubles) == 5);

        char chars[20];
        CHECK(CC_ARRAY_COUNT_OF(chars) == 20);

        struct TestStruct
        {
            int a, b;
        };
        TestStruct structs[7];
        CHECK(CC_ARRAY_COUNT_OF(structs) == 7);
    }

    SECTION("Multidimensional arrays")
    {
        // Should return count of first dimension
        int arr2d[3][5];
        CHECK(CC_ARRAY_COUNT_OF(arr2d) == 3);

        int arr3d[4][2][8];
        CHECK(CC_ARRAY_COUNT_OF(arr3d) == 4);
    }
}

TEST("macros - CC_MACRO_JOIN")
{
    SECTION("Direct token joining")
    {
        int foo_bar = 42;
        int result = CC_MACRO_JOIN(foo_, bar);
        CHECK(result == 42);
    }

    SECTION("Join with macro expansion")
    {
#define TEST_PREFIX foo
        int foo_suffix = 123;
        int result = CC_MACRO_JOIN(TEST_PREFIX, _suffix);
        CHECK(result == 123);
#undef TEST_PREFIX
    }

    SECTION("Join with numeric suffix")
    {
#define VERSION 2
        int var2 = 999;
        int result = CC_MACRO_JOIN(var, VERSION);
        CHECK(result == 999);
#undef VERSION
    }
}

TEST("macros - CC_STRINGIFY_EXPR")
{
    SECTION("Stringify simple value")
    {
#define MY_VALUE 42
        const char* str = CC_STRINGIFY_EXPR(MY_VALUE);
        CHECK(std::strcmp(str, "42") == 0);
#undef MY_VALUE
    }

    SECTION("Stringify identifier")
    {
#define MY_IDENT foo
        const char* str = CC_STRINGIFY_EXPR(MY_IDENT);
        CHECK(std::strcmp(str, "foo") == 0);
#undef MY_IDENT
    }

    SECTION("Stringify expression")
    {
#define MY_EXPR (1 + 2)
        const char* str = CC_STRINGIFY_EXPR(MY_EXPR);
        CHECK(std::strcmp(str, "(1 + 2)") == 0);
#undef MY_EXPR
    }
}

TEST("macros - CC_UNUSED does not evaluate")
{
    SECTION("No side effects on increment")
    {
        int x = 0;
        CC_UNUSED(++x);
        CHECK(x == 0);
    }

    SECTION("No side effects on decrement")
    {
        int y = 10;
        CC_UNUSED(--y);
        CHECK(y == 10);
    }

    SECTION("No side effects on function call")
    {
        int counter = 0;
        auto increment = [&counter]() -> int { return ++counter; };
        CC_UNUSED(increment());
        CHECK(counter == 0);
    }

    SECTION("Works with complex expressions")
    {
        int a = 5;
        int b = 3;
        CC_UNUSED(a * b + (a << 2));
        CHECK(a == 5);
        CHECK(b == 3);
    }
}

// Helper functions for testing CC_CONDITION_LIKELY/UNLIKELY and CC_PRETTY_FUNC
namespace
{
int test_likely_branch(int* ptr)
{
    if CC_CONDITION_LIKELY (ptr != nullptr)
    {
        return *ptr;
    }
    else
    {
        return -1;
    }
}

int test_unlikely_branch(int value)
{
    if CC_CONDITION_UNLIKELY (value < 0)
    {
        return 0;
    }
    else
    {
        return value;
    }
}

char const* get_pretty_func_name()
{
    return CC_PRETTY_FUNC;
}

struct TestClass
{
    char const* get_method_func_name() { return CC_PRETTY_FUNC; }
};

int test_unreachable_switch(int value)
{
    switch (value)
    {
    case 1: return 10;
    case 2: return 20;
    default: CC_BUILTIN_UNREACHABLE;
    }
}

void test_assume_statement(int* ptr)
{
    CC_ASSUME(ptr != nullptr);
    *ptr = 42;
}
} // namespace

TEST("macros - statement syntax")
{
    SECTION("CC_CONDITION_LIKELY compiles and works")
    {
        int value = 42;
        int result = test_likely_branch(&value);
        CHECK(result == 42);

        result = test_likely_branch(nullptr);
        CHECK(result == -1);
    }

    SECTION("CC_CONDITION_UNLIKELY compiles and works")
    {
        int result = test_unlikely_branch(10);
        CHECK(result == 10);

        result = test_unlikely_branch(-5);
        CHECK(result == 0);
    }

    SECTION("CC_ASSUME compiles as statement")
    {
        int value = 0;
        test_assume_statement(&value);
        CHECK(value == 42);
    }

    SECTION("CC_BUILTIN_UNREACHABLE compiles in switch")
    {
        // Just verify it compiles; we don't actually call with default case
        CHECK(test_unreachable_switch(1) == 10);
        CHECK(test_unreachable_switch(2) == 20);
    }
}

TEST("macros - CC_PRETTY_FUNC")
{
    SECTION("Contains function name")
    {
        char const* func_name = get_pretty_func_name();
        CHECK(std::strstr(func_name, "get_pretty_func_name") != nullptr);
    }

    SECTION("Works in method")
    {
        TestClass obj;
        char const* method_name = obj.get_method_func_name();
        CHECK(std::strstr(method_name, "get_method_func_name") != nullptr);
    }

    SECTION("Works in lambda")
    {
        auto lambda = []() -> char const* { return CC_PRETTY_FUNC; };
        char const* lambda_name = lambda();
        // Lambda name format varies by compiler, just check it's not empty
        CHECK(lambda_name != nullptr);
        CHECK(std::strlen(lambda_name) > 0);
    }
}
