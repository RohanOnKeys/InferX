# Standalone build of the stage 1 harness. Needs only HPX:
#   cmake -S experiments/01_stateful_component -B <build dir> -G Ninja \
#     -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<hpx-install>
# Follows langX bench/hpx_repro/CMakeLists.txt.
cmake_minimum_required(VERSION 3.19)
project(inferx_stage1 CXX)

find_package(HPX REQUIRED)
if(NOT HPX_WITH_SUPERVISION OR NOT TARGET HPX::supervision_dispatch_component)
  message(FATAL_ERROR "HPX must be built with HPX_WITH_SUPERVISION=ON")
endif()

add_executable(stateful_component stateful_component.cpp)
target_link_libraries(stateful_component
  PRIVATE HPX::hpx HPX::supervision_dispatch_component)
target_compile_features(stateful_component PRIVATE cxx_std_20)
