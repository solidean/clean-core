#include "node_allocation.hh"

namespace
{

constinit cc::node_memory_resource system_node_memory_resource = {
    // TODO
};

} // namespace

constinit cc::node_memory_resource* const cc::default_node_memory_resource = &system_node_memory_resource;
