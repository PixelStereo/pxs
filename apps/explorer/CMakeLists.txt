# OSCQuery Explorer: the devices of the network on the left, a page of controls on the right (pxs::oscclient, pxs::controls, Qt WebEngine).
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)
# Homebrew: the qt formula and qtwebengine can be at different versions, and Qt's prefix then lacks WebEngine's headers
get_filename_component(_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
get_filename_component(_qt_web "${_qt_prefix}/../qtwebengine" ABSOLUTE)
if(NOT EXISTS "${_qt_prefix}/include/QtWebEngineWidgets" AND EXISTS "${_qt_web}/include/QtWebEngineWidgets")
    foreach(_m WebEngineWidgets WebEngineCore WebEngineCoreTools)
        set(Qt6${_m}_DIR "${_qt_web}/lib/cmake/Qt6${_m}" CACHE PATH "" FORCE)
    endforeach()
endif()
find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)
if(NOT Qt6WebEngineWidgets_FOUND)
    message(STATUS "pxs explorer: Qt WebEngine not found, not built")
    return()
endif()

# The model: no widgets, so that it is tested alone
add_library(pxs_explorer_core STATIC Explorer.cpp Explorer.h)
set_target_properties(pxs_explorer_core PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_explorer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pxs_explorer_core PUBLIC pxs::oscclient pxs::controls Qt6::Network)

add_executable(pxs_explorer MACOSX_BUNDLE main.cpp MainWindow.cpp MainWindow.h)
set_target_properties(pxs_explorer PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "fr.pixelstereo.oscquery-explorer" AUTOMOC ON OUTPUT_NAME "OSCQuery Explorer")
target_link_libraries(pxs_explorer PRIVATE pxs_explorer_core pxs::theme Qt6::Widgets Qt6::WebEngineWidgets)

# A device to try it on
add_executable(pxs_demo_device demo_device.cpp)
target_link_libraries(pxs_demo_device PRIVATE pxs::oscquery pxs::tree Qt6::Core)

if(PXS_BUILD_TESTS)
    add_executable(pxs_explorer_test explorer_test.cpp)
    target_link_libraries(pxs_explorer_test PRIVATE pxs_explorer_core pxs::oscquery pxs::tree)
    add_test(NAME explorer COMMAND pxs_explorer_test)
endif()
