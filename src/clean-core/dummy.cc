#include "dummy.hh"

#include <nexus/test.hh>

int cc::foo() { return 10; }

TEST("foo == 10")
{
    CHECK(cc::foo() == 10);
}
