#include <clean-core/optional.hh>

#include <nexus/test.hh>

#include <memory>
#include <string>

// optional stays trivial
static_assert(std::is_constructible_v<cc::optional<int>>);
static_assert(std::is_constructible_v<cc::optional<int>, int>);
static_assert(std::is_constructible_v<cc::optional<int>, cc::nullopt_t>);
static_assert(std::is_trivially_copyable_v<cc::optional<int>>);
static_assert(std::is_trivially_destructible_v<cc::optional<int>>);

namespace
{
// test type for non-trivial operations
struct non_trivial
{
    int value = 0;
    bool* destroyed = nullptr;

    non_trivial() = default;
    explicit non_trivial(int v) : value(v) {}
    non_trivial(int v, bool* d) : value(v), destroyed(d) {}

    ~non_trivial()
    {
        if (destroyed)
            *destroyed = true;
    }

    non_trivial(non_trivial const&) = default;
    non_trivial(non_trivial&&) = default;
    non_trivial& operator=(non_trivial const&) = default;
    non_trivial& operator=(non_trivial&&) = default;

    friend bool operator==(non_trivial const&, non_trivial const&) = default;
};

// move-only type for testing
struct move_only
{
    int value = 0;

    move_only() = default;
    explicit move_only(int v) : value(v) {}

    move_only(move_only const&) = delete;
    move_only(move_only&& rhs) noexcept : value(rhs.value) { rhs.value = -1; }
    move_only& operator=(move_only const&) = delete;
    move_only& operator=(move_only&& rhs) noexcept
    {
        value = rhs.value;
        rhs.value = -1;
        return *this;
    }

    ~move_only() = default;
};

// counting type to track special member function calls
struct counting_type
{
    int value = 0;

    static inline int default_ctor_count = 0;
    static inline int value_ctor_count = 0;
    static inline int copy_ctor_count = 0;
    static inline int move_ctor_count = 0;
    static inline int copy_assign_count = 0;
    static inline int move_assign_count = 0;
    static inline int dtor_count = 0;

    static void reset_counters()
    {
        default_ctor_count = 0;
        value_ctor_count = 0;
        copy_ctor_count = 0;
        move_ctor_count = 0;
        copy_assign_count = 0;
        move_assign_count = 0;
        dtor_count = 0;
    }

    counting_type() { ++default_ctor_count; }
    explicit counting_type(int v) : value(v) { ++value_ctor_count; }

    counting_type(counting_type const& rhs) : value(rhs.value) { ++copy_ctor_count; }
    counting_type(counting_type&& rhs) noexcept : value(rhs.value) { ++move_ctor_count; }

    counting_type& operator=(counting_type const& rhs)
    {
        value = rhs.value;
        ++copy_assign_count;
        return *this;
    }

    counting_type& operator=(counting_type&& rhs) noexcept
    {
        value = rhs.value;
        ++move_assign_count;
        return *this;
    }

    ~counting_type() { ++dtor_count; }

    friend bool operator==(counting_type const&, counting_type const&) = default;
};
} // namespace

TEST("optional - trivial types")
{
    SECTION("default construction")
    {
        auto const opt = cc::optional<int>{};
        CHECK(!opt.has_value());
    }

    SECTION("nullopt construction")
    {
        auto const opt = cc::optional<int>{cc::nullopt};
        CHECK(!opt.has_value());
    }

    SECTION("value construction")
    {
        auto const opt = cc::optional<int>{42};
        CHECK(opt.has_value());
        CHECK(opt.value() == 42);
    }

    SECTION("copy construction")
    {
        auto const opt1 = cc::optional<int>{42};
        auto const opt2 = opt1;
        CHECK(opt2.has_value());
        CHECK(opt2.value() == 42);
        CHECK(opt1.has_value());
        CHECK(opt1.value() == 42);
    }

    SECTION("move construction")
    {
        auto opt1 = cc::optional<int>{42};
        auto const opt2 = cc::move(opt1);
        CHECK(opt2.has_value());
        CHECK(opt2.value() == 42);
        // trivial types remain engaged after move
        CHECK(opt1.has_value());
        CHECK(opt1.value() == 42);
    }

    SECTION("copy assignment")
    {
        auto opt1 = cc::optional<int>{42};
        auto opt2 = cc::optional<int>{};
        opt2 = opt1;
        CHECK(opt2.has_value());
        CHECK(opt2.value() == 42);
        CHECK(opt1.has_value());
        CHECK(opt1.value() == 42);
    }

    SECTION("move assignment")
    {
        auto opt1 = cc::optional<int>{42};
        auto opt2 = cc::optional<int>{};
        opt2 = cc::move(opt1);
        CHECK(opt2.has_value());
        CHECK(opt2.value() == 42);
        // trivial types remain engaged after move
        CHECK(opt1.has_value());
        CHECK(opt1.value() == 42);
    }

    SECTION("value assignment")
    {
        auto opt = cc::optional<int>{};
        opt = 42;
        CHECK(opt.has_value());
        CHECK(opt.value() == 42);
    }

    SECTION("nullopt assignment")
    {
        auto opt = cc::optional<int>{42};
        opt = cc::nullopt;
        CHECK(!opt.has_value());
    }
}

TEST("optional - non-trivial types")
{
    SECTION("default construction")
    {
        auto const opt = cc::optional<non_trivial>{};
        CHECK(!opt.has_value());
    }

    SECTION("value construction")
    {
        auto const opt = cc::optional<non_trivial>{non_trivial{42}};
        CHECK(opt.has_value());
        CHECK(opt.value().value == 42);
    }

    SECTION("copy construction")
    {
        auto const opt1 = cc::optional<non_trivial>{non_trivial{42}};
        auto const opt2 = opt1; // NOLINT
        CHECK(opt2.has_value());
        CHECK(opt2.value().value == 42);
        CHECK(opt1.has_value());
        CHECK(opt1.value().value == 42);
    }

    SECTION("move construction")
    {
        auto opt1 = cc::optional<non_trivial>{non_trivial{42}};
        auto const opt2 = cc::move(opt1);
        CHECK(opt2.has_value());
        CHECK(opt2.value().value == 42);
        CHECK(!opt1.has_value());
    }

    SECTION("copy assignment")
    {
        auto opt1 = cc::optional<non_trivial>{non_trivial{42}};
        auto opt2 = cc::optional<non_trivial>{};
        opt2 = opt1;
        CHECK(opt2.has_value());
        CHECK(opt2.value().value == 42);
        CHECK(opt1.has_value());
        CHECK(opt1.value().value == 42);
    }

    SECTION("move assignment")
    {
        auto opt1 = cc::optional<non_trivial>{non_trivial{42}};
        auto opt2 = cc::optional<non_trivial>{};
        opt2 = cc::move(opt1);
        CHECK(opt2.has_value());
        CHECK(opt2.value().value == 42);
        CHECK(!opt1.has_value());
    }

    SECTION("value types - string")
    {
        auto opt = cc::optional<std::string>{"hello"};
        CHECK(opt.has_value());
        CHECK(opt.value() == "hello");

        opt = std::string{"world"};
        CHECK(opt.has_value());
        CHECK(opt.value() == "world");

        opt = cc::nullopt;
        CHECK(!opt.has_value());
    }
}

TEST("optional - counting special member functions")
{
    SECTION("default construction")
    {
        counting_type::reset_counters();
        {
            auto const opt = cc::optional<counting_type>{};
            CHECK(!opt.has_value());
        }
        // No construction or destruction should occur
        CHECK(counting_type::default_ctor_count == 0);
        CHECK(counting_type::value_ctor_count == 0);
        CHECK(counting_type::copy_ctor_count == 0);
        CHECK(counting_type::move_ctor_count == 0);
        CHECK(counting_type::copy_assign_count == 0);
        CHECK(counting_type::move_assign_count == 0);
        CHECK(counting_type::dtor_count == 0);
    }

    SECTION("value construction")
    {
        counting_type::reset_counters();
        {
            auto const opt = cc::optional<counting_type>{counting_type{42}};
            CHECK(opt.has_value());
            CHECK(opt.value().value == 42);
        }
        // Should construct temp value (value_ctor), move it into optional (move_ctor), then destruct both
        CHECK(counting_type::value_ctor_count == 1);
        CHECK(counting_type::move_ctor_count == 1);
        CHECK(counting_type::dtor_count == 2); // temp + optional contents
        CHECK(counting_type::copy_ctor_count == 0);
        CHECK(counting_type::copy_assign_count == 0);
        CHECK(counting_type::move_assign_count == 0);
    }

    SECTION("copy construction")
    {
        counting_type::reset_counters();
        {
            auto const opt1 = cc::optional<counting_type>{counting_type{42}};
            counting_type::reset_counters(); // Reset to isolate copy construction
            auto const opt2 = opt1;
            CHECK(opt2.has_value());
            CHECK(opt2.value().value == 42);
        }
        // Should only copy construct
        CHECK(counting_type::copy_ctor_count == 1);
        CHECK(counting_type::move_ctor_count == 0);
        CHECK(counting_type::copy_assign_count == 0);
        CHECK(counting_type::move_assign_count == 0);
        CHECK(counting_type::dtor_count == 2); // both optionals destructed
    }

    SECTION("move construction")
    {
        counting_type::reset_counters();
        {
            auto opt1 = cc::optional<counting_type>{counting_type{42}};
            counting_type::reset_counters(); // Reset to isolate move construction
            auto const opt2 = cc::move(opt1);
            CHECK(opt2.has_value());
            CHECK(opt2.value().value == 42);
            CHECK(!opt1.has_value());
        }
        // Should only move construct, then destruct the moved value
        CHECK(counting_type::move_ctor_count == 1);
        CHECK(counting_type::dtor_count == 2); // moved-from value + opt2 contents
        CHECK(counting_type::copy_ctor_count == 0);
        CHECK(counting_type::copy_assign_count == 0);
        CHECK(counting_type::move_assign_count == 0);
    }

    SECTION("copy assignment - empty to empty")
    {
        counting_type::reset_counters();
        {
            auto opt1 = cc::optional<counting_type>{};
            auto opt2 = cc::optional<counting_type>{};
            opt2 = opt1;
            CHECK(!opt2.has_value());
        }
        // No operations should occur
        CHECK(counting_type::copy_ctor_count == 0);
        CHECK(counting_type::copy_assign_count == 0);
        CHECK(counting_type::move_ctor_count == 0);
        CHECK(counting_type::move_assign_count == 0);
        CHECK(counting_type::dtor_count == 0);
    }

    SECTION("copy assignment - value to empty")
    {
        counting_type::reset_counters();
        {
            auto opt1 = cc::optional<counting_type>{counting_type{42}};
            auto opt2 = cc::optional<counting_type>{};
            counting_type::reset_counters(); // Reset to isolate assignment
            opt2 = opt1;
            CHECK(opt2.has_value());
            CHECK(opt2.value().value == 42);
        }
        // Should copy construct into empty optional
        CHECK(counting_type::copy_ctor_count == 1);
        CHECK(counting_type::copy_assign_count == 0);
        CHECK(counting_type::dtor_count == 2); // both destructed at end
    }

    SECTION("copy assignment - value to value")
    {
        counting_type::reset_counters();
        {
            auto opt1 = cc::optional<counting_type>{counting_type{42}};
            auto opt2 = cc::optional<counting_type>{counting_type{99}};
            counting_type::reset_counters(); // Reset to isolate assignment
            opt2 = opt1;
            CHECK(opt2.has_value());
            CHECK(opt2.value().value == 42);
        }
        // Should use copy assignment operator (not reconstruct)
        CHECK(counting_type::copy_assign_count == 1);
        CHECK(counting_type::copy_ctor_count == 0);
        CHECK(counting_type::dtor_count == 2); // both destructed at end
    }

    SECTION("copy assignment - empty to value")
    {
        counting_type::reset_counters();
        {
            auto opt1 = cc::optional<counting_type>{};
            auto opt2 = cc::optional<counting_type>{counting_type{99}};
            counting_type::reset_counters(); // Reset to isolate assignment
            opt2 = opt1;
            CHECK(!opt2.has_value());
        }
        // Should destruct the value in opt2
        CHECK(counting_type::dtor_count == 1);
        CHECK(counting_type::copy_ctor_count == 0);
        CHECK(counting_type::copy_assign_count == 0);
    }

    SECTION("move assignment - empty to empty")
    {
        counting_type::reset_counters();
        {
            auto opt1 = cc::optional<counting_type>{};
            auto opt2 = cc::optional<counting_type>{};
            opt2 = cc::move(opt1);
            CHECK(!opt2.has_value());
        }
        // No operations should occur
        CHECK(counting_type::move_ctor_count == 0);
        CHECK(counting_type::move_assign_count == 0);
        CHECK(counting_type::dtor_count == 0);
    }

    SECTION("move assignment - value to empty")
    {
        counting_type::reset_counters();
        {
            auto opt1 = cc::optional<counting_type>{counting_type{42}};
            auto opt2 = cc::optional<counting_type>{};
            counting_type::reset_counters(); // Reset to isolate assignment
            opt2 = cc::move(opt1);
            CHECK(opt2.has_value());
            CHECK(opt2.value().value == 42);
            CHECK(!opt1.has_value());
        }
        // Should move construct into empty optional, then destruct moved-from value
        CHECK(counting_type::move_ctor_count == 1);
        CHECK(counting_type::move_assign_count == 0);
        CHECK(counting_type::dtor_count == 2); // moved-from + opt2 contents
    }

    SECTION("move assignment - value to value")
    {
        counting_type::reset_counters();
        {
            auto opt1 = cc::optional<counting_type>{counting_type{42}};
            auto opt2 = cc::optional<counting_type>{counting_type{99}};
            counting_type::reset_counters(); // Reset to isolate assignment
            opt2 = cc::move(opt1);
            CHECK(opt2.has_value());
            CHECK(opt2.value().value == 42);
            CHECK(!opt1.has_value());
        }
        // Should use move assignment operator (not reconstruct)
        CHECK(counting_type::move_assign_count == 1);
        CHECK(counting_type::move_ctor_count == 0);
        CHECK(counting_type::dtor_count == 2); // moved-from + opt2 contents
    }

    SECTION("move assignment - empty to value")
    {
        counting_type::reset_counters();
        {
            auto opt1 = cc::optional<counting_type>{};
            auto opt2 = cc::optional<counting_type>{counting_type{99}};
            counting_type::reset_counters(); // Reset to isolate assignment
            opt2 = cc::move(opt1);
            CHECK(!opt2.has_value());
        }
        // Should destruct the value in opt2
        CHECK(counting_type::dtor_count == 1);
        CHECK(counting_type::move_ctor_count == 0);
        CHECK(counting_type::move_assign_count == 0);
    }

    SECTION("constructor and destructor balance")
    {
        counting_type::reset_counters();
        {
            auto opt1 = cc::optional<counting_type>{counting_type{1}};
            auto opt2 = cc::optional<counting_type>{counting_type{2}};
            auto opt3 = opt1;
            auto opt4 = cc::move(opt2);
            opt1 = opt3;
            opt2 = cc::move(opt4);
        }
        // Total constructions must equal total destructions
        int const total_ctors = counting_type::default_ctor_count + counting_type::value_ctor_count
                              + counting_type::copy_ctor_count + counting_type::move_ctor_count;
        CHECK(total_ctors == counting_type::dtor_count);
    }

    SECTION("self-copy assignment")
    {
        counting_type::reset_counters();
        {
            auto opt = cc::optional<counting_type>{counting_type{42}};
            counting_type::reset_counters(); // Reset to isolate self-assignment
            opt = opt;                       // NOLINT(clang-diagnostic-self-assign-overloaded)
            CHECK(opt.has_value());
            CHECK(opt.value().value == 42);
        }
        // Self-assignment should be a no-op (no copy, no construction)
        CHECK(counting_type::copy_assign_count == 0);
        CHECK(counting_type::copy_ctor_count == 0);
        CHECK(counting_type::move_assign_count == 0);
        CHECK(counting_type::move_ctor_count == 0);
        // Only destructor at end of scope
        CHECK(counting_type::dtor_count == 1);
    }

    SECTION("self-move assignment")
    {
        counting_type::reset_counters();
        {
            auto opt = cc::optional<counting_type>{counting_type{42}};
            counting_type::reset_counters(); // Reset to isolate self-assignment
            opt = cc::move(opt);
            // After self-move, optional should become empty
            CHECK(!opt.has_value());
        }
        // Self-move should destruct the value and leave optional empty
        CHECK(counting_type::dtor_count == 1);
        CHECK(counting_type::move_assign_count == 1); // is also a self-assign
        CHECK(counting_type::move_ctor_count == 0);
        CHECK(counting_type::copy_assign_count == 0);
        CHECK(counting_type::copy_ctor_count == 0);
    }

    SECTION("self-copy assignment - empty")
    {
        counting_type::reset_counters();
        {
            auto opt = cc::optional<counting_type>{};
            opt = opt; // NOLINT(clang-diagnostic-self-assign-overloaded)
            CHECK(!opt.has_value());
        }
        // No operations should occur
        CHECK(counting_type::copy_assign_count == 0);
        CHECK(counting_type::copy_ctor_count == 0);
        CHECK(counting_type::dtor_count == 0);
    }

    SECTION("self-move assignment - empty")
    {
        counting_type::reset_counters();
        {
            auto opt = cc::optional<counting_type>{};
            opt = cc::move(opt);
            CHECK(!opt.has_value());
        }
        // No operations should occur
        CHECK(counting_type::move_assign_count == 0);
        CHECK(counting_type::move_ctor_count == 0);
        CHECK(counting_type::dtor_count == 0);
    }
}

TEST("optional - move-only types")
{
    SECTION("construction")
    {
        auto opt = cc::optional<move_only>{move_only{42}};
        CHECK(opt.has_value());
        CHECK(opt.value().value == 42);
    }

    SECTION("move construction")
    {
        auto opt1 = cc::optional<move_only>{move_only{42}};
        auto opt2 = cc::move(opt1);
        CHECK(opt2.has_value());
        CHECK(opt2.value().value == 42);
        CHECK(!opt1.has_value());
    }

    SECTION("move assignment")
    {
        auto opt1 = cc::optional<move_only>{move_only{42}};
        auto opt2 = cc::optional<move_only>{};
        opt2 = cc::move(opt1);
        CHECK(opt2.has_value());
        CHECK(opt2.value().value == 42);
        CHECK(!opt1.has_value());
    }

    SECTION("value move assignment")
    {
        auto opt = cc::optional<move_only>{};
        opt = move_only{99};
        CHECK(opt.has_value());
        CHECK(opt.value().value == 99);
    }

    SECTION("unique_ptr")
    {
        auto opt = cc::optional<std::unique_ptr<int>>{std::make_unique<int>(42)};
        CHECK(opt.has_value());
        CHECK(*opt.value() == 42);

        auto opt2 = cc::move(opt);
        CHECK(opt2.has_value());
        CHECK(*opt2.value() == 42);
        CHECK(!opt.has_value());
    }
}

TEST("optional - equality operator")
{
    SECTION("both empty")
    {
        auto const opt1 = cc::optional<int>{};
        auto const opt2 = cc::optional<int>{};
        CHECK(opt1 == opt2);
    }

    SECTION("one empty, one with value")
    {
        auto const opt1 = cc::optional<int>{};
        auto const opt2 = cc::optional<int>{42};
        CHECK(opt1 != opt2);
        CHECK(opt2 != opt1);
    }

    SECTION("both with same value")
    {
        auto const opt1 = cc::optional<int>{42};
        auto const opt2 = cc::optional<int>{42};
        CHECK(opt1 == opt2);
    }

    SECTION("both with different values")
    {
        auto const opt1 = cc::optional<int>{42};
        auto const opt2 = cc::optional<int>{99};
        CHECK(opt1 != opt2);
    }

    SECTION("non-trivial types")
    {
        auto const opt1 = cc::optional<non_trivial>{non_trivial{42}};
        auto const opt2 = cc::optional<non_trivial>{non_trivial{42}};
        auto const opt3 = cc::optional<non_trivial>{non_trivial{99}};
        auto const opt4 = cc::optional<non_trivial>{};

        CHECK(opt1 == opt2);
        CHECK(opt1 != opt3);
        CHECK(opt1 != opt4);
        CHECK(opt4 != opt1);
    }
}

TEST("optional - value and has_value")
{
    SECTION("has_value on empty")
    {
        auto const opt = cc::optional<int>{};
        CHECK(!opt.has_value());
    }

    SECTION("has_value on filled")
    {
        auto const opt = cc::optional<int>{42};
        CHECK(opt.has_value());
    }

    SECTION("value const reference")
    {
        auto const opt = cc::optional<int>{42};
        CHECK(opt.value() == 42);
    }

    SECTION("value reference")
    {
        auto opt = cc::optional<int>{42};
        opt.value() = 99;
        CHECK(opt.value() == 99);
    }

    SECTION("value rvalue reference")
    {
        auto opt = cc::optional<move_only>{move_only{42}};
        auto moved = cc::move(opt).value();
        CHECK(moved.value == 42);
    }

    SECTION("value preserves category - const")
    {
        auto const opt = cc::optional<std::string>{"hello"};
        auto const& ref = opt.value();
        CHECK(ref == "hello");
    }

    SECTION("value preserves category - mutable")
    {
        auto opt = cc::optional<std::string>{"hello"};
        opt.value() += " world";
        CHECK(opt.value() == "hello world");
    }
}

TEST("optional - assignment scenarios")
{
    SECTION("empty to value")
    {
        auto opt = cc::optional<int>{};
        opt = 42;
        CHECK(opt.has_value());
        CHECK(opt.value() == 42);
    }

    SECTION("value to different value")
    {
        auto opt = cc::optional<int>{42};
        opt = 99;
        CHECK(opt.has_value());
        CHECK(opt.value() == 99);
    }

    SECTION("value to empty")
    {
        auto opt = cc::optional<int>{42};
        opt = cc::nullopt;
        CHECK(!opt.has_value());
    }

    SECTION("empty to empty")
    {
        auto opt = cc::optional<int>{};
        opt = cc::nullopt;
        CHECK(!opt.has_value());
    }

    SECTION("copy from empty to empty")
    {
        auto opt1 = cc::optional<int>{};
        auto opt2 = cc::optional<int>{};
        opt2 = opt1;
        CHECK(!opt2.has_value());
    }

    SECTION("copy from value to value")
    {
        auto opt1 = cc::optional<int>{42};
        auto opt2 = cc::optional<int>{99};
        opt2 = opt1;
        CHECK(opt2.has_value());
        CHECK(opt2.value() == 42);
    }

    SECTION("copy from empty to value")
    {
        auto opt1 = cc::optional<int>{};
        auto opt2 = cc::optional<int>{99};
        opt2 = opt1;
        CHECK(!opt2.has_value());
    }

    SECTION("copy from value to empty")
    {
        auto opt1 = cc::optional<int>{42};
        auto opt2 = cc::optional<int>{};
        opt2 = opt1;
        CHECK(opt2.has_value());
        CHECK(opt2.value() == 42);
    }
}
