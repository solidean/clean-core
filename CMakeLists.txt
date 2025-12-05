cmake_minimum_required(VERSION 3.28)
project(clean-core LANGUAGES CXX)

# dependencies
# - clean core has no dependencies

# Define the library and its sources
add_library(clean-core
    src/clean-core/dummy.cc
)

# Public headers live co-located in src/ for better editor experience.
# FILE_SET models the public API explicitly (install/export, IDEs, etc.).
target_sources(clean-core
    PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
    FILES
    src/clean-core/dummy.hh
    src/clean-core/fwd.hh
)

# Libraries should not set a global C++ standard here.
# Instead, declare a minimum on the target so parents stay in control.
# Require at least C++23 for this target; the final -std is chosen by the top-level project.
target_compile_features(clean-core
    PUBLIC
    cxx_std_23
)

# Expose "src" so '#include <clean-core/dummy.hh>' works for consumers.
target_include_directories(clean-core
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
