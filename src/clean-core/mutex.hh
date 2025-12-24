#pragma once

#include <clean-core/fwd.hh>

/// Thread-safe wrapper for data T protected by a mutex
/// Provides safe access to the protected data through RAII guards
template <class T>
struct cc::mutex
{
    // TODO: implement mutex
};
