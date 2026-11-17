cmake_minimum_required(VERSION 3.21)
project(pxs VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PXS_BUILD_TESTS "Build the pxs tests" ${PROJECT_IS_TOP_LEVEL})

# Build identity helpers (pxs_build_suffix, pxs_nearest_release): include(<pxs>/cmake/PixelStereoBuildId.cmake)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

# OSC 1.0 codec: messages, bundles, address patterns. Qt Core only.
add_library(pxs_osc STATIC src/osc/OscCodec.cpp src/osc/OscCodec.h)
add_library(pxs::osc ALIAS pxs_osc)
target_include_directories(pxs_osc PUBLIC src/osc)
target_link_libraries(pxs_osc PUBLIC Qt6::Core)

if(PXS_BUILD_TESTS)
    enable_testing()
    add_executable(pxs_osc_test test/osc_test.cpp)
    target_link_libraries(pxs_osc_test PRIVATE pxs::osc)
    add_test(NAME osc COMMAND pxs_osc_test)
    add_test(NAME build_id COMMAND ${CMAKE_COMMAND} -DPXS_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
             -P ${CMAKE_CURRENT_SOURCE_DIR}/test/build_id_test.cmake)
endif()
