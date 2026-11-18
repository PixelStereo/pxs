cmake_minimum_required(VERSION 3.21)
project(pxs VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PXS_BUILD_TESTS "Build the pxs tests" ${PROJECT_IS_TOP_LEVEL})

# Build identity helpers (pxs_build_suffix, pxs_nearest_release): include(<pxs>/cmake/PixelStereoBuildId.cmake)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets)

# OSC 1.0 codec: messages, bundles, address patterns. Qt Core only.
add_library(pxs_osc STATIC src/osc/OscCodec.cpp src/osc/OscCodec.h)
add_library(pxs::osc ALIAS pxs_osc)
target_include_directories(pxs_osc PUBLIC src/osc)
target_link_libraries(pxs_osc PUBLIC Qt6::Core)

# Parameters: how an application declares what can be read, written, saved and driven over OSC, and how the others
# are told it changed (ChangeFeed). Qt Core only.
add_library(pxs_params STATIC src/params/Parameter.cpp src/params/Parameter.h src/params/ChangeFeed.h)
add_library(pxs::params ALIAS pxs_params)
target_include_directories(pxs_params PUBLIC src/params)
target_link_libraries(pxs_params PUBLIC Qt6::Core)

# The look: colour tokens, dark palettes, the style sheet every application shares
add_library(pxs_theme STATIC src/theme/Theme.cpp src/theme/pxs/Theme.h)
add_library(pxs::theme ALIAS pxs_theme)
target_include_directories(pxs_theme PUBLIC src/theme)
target_link_libraries(pxs_theme PUBLIC Qt6::Widgets)

# Widgets used by several applications: the slider field (drag, type, snap) and the color picker
add_library(pxs_widgets STATIC src/widgets/Fields.cpp src/widgets/Fields.h src/widgets/SliderField.h
    src/widgets/ColorPicker.cpp src/widgets/ColorPicker.h)
add_library(pxs::widgets ALIAS pxs_widgets)
set_target_properties(pxs_widgets PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_widgets PUBLIC src/widgets)
target_link_libraries(pxs_widgets PUBLIC Qt6::Widgets)

if(PXS_BUILD_TESTS)
    enable_testing()
    add_executable(pxs_osc_test test/osc_test.cpp)
    target_link_libraries(pxs_osc_test PRIVATE pxs::osc)
    add_test(NAME osc COMMAND pxs_osc_test)
    add_executable(pxs_params_test test/params_test.cpp)
    target_link_libraries(pxs_params_test PRIVATE pxs::params)
    add_test(NAME params COMMAND pxs_params_test)
    add_executable(pxs_theme_test test/theme_test.cpp)
    target_link_libraries(pxs_theme_test PRIVATE pxs::theme)
    add_test(NAME theme COMMAND pxs_theme_test)
    add_executable(pxs_widgets_test test/widgets_test.cpp)
    target_link_libraries(pxs_widgets_test PRIVATE pxs::widgets)
    add_test(NAME widgets COMMAND pxs_widgets_test)
    add_test(NAME build_id COMMAND ${CMAKE_COMMAND} -DPXS_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
             -P ${CMAKE_CURRENT_SOURCE_DIR}/test/build_id_test.cmake)
endif()
