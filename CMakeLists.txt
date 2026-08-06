cmake_minimum_required(VERSION 3.20)
project(EmboNav LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BUILD_GUI "Build the Qt6 navigation workbench" ON)
option(BUILD_CLI "Build the head-less centerline tool"  ON)

# ---------------------------------------------------------------------------
# Geometry + navigation core — Qt-free static library
# ---------------------------------------------------------------------------
add_library(embonav_core STATIC
    src/core/Vec3.h
    src/core/StlMesh.cpp
    src/core/StlMesh.h
    src/core/MeshDistance.h
    src/core/CenterlineGraph.cpp
    src/core/CenterlineGraph.h
    src/core/CenterlineExtractor.cpp
    src/core/CenterlineExtractor.h
    src/core/VtpCenterlineLoader.cpp
    src/core/VtpCenterlineLoader.h
    src/core/SheathManager.cpp
    src/core/SheathManager.h
    src/core/AccessSites.cpp
    src/core/AccessSites.h
    src/core/NavigationController.cpp
    src/core/NavigationController.h
    src/core/CatheterModel.cpp
    src/core/CatheterModel.h
    src/core/CardiacCycle.cpp
    src/core/CardiacCycle.h
    src/core/CatheterCatalog.cpp
    src/core/CatheterCatalog.h
)
target_include_directories(embonav_core PUBLIC src src/core)
# VtpCenterlineLoader uses qUncompress (Qt-bundled zlib) — Qt6::Core provides it
target_link_libraries(embonav_core PUBLIC Qt6::Core)

if(MSVC)
    target_compile_options(embonav_core PRIVATE /W4)
else()
    target_compile_options(embonav_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Head-less CLI — no Qt needed
# ---------------------------------------------------------------------------
if(BUILD_CLI)
    add_executable(embonav_cli tools/centerline_cli.cpp)
    target_link_libraries(embonav_cli PRIVATE embonav_core)
endif()

# ---------------------------------------------------------------------------
# Qt6 GUI workbench
# ---------------------------------------------------------------------------
if(BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS
        Widgets Gui OpenGL OpenGLWidgets)
    qt_standard_project_setup()

    # WIN32 and MACOSX_BUNDLE are passed as keywords to qt_add_executable so
    # that CMake sets the correct subsystem and qt_add_executable can wire the
    # platform entry-point automatically.  Do NOT add Qt6::EntryPoint manually:
    # that target does not exist in Qt 6.4 and causes a CMake configuration
    # error.  qt_add_executable + WIN32 is the correct and supported way on all
    # Qt 6.x versions.
    qt_add_executable(EmboNav
        WIN32
        MACOSX_BUNDLE
        src/main.cpp
        src/ui/MainWindow.cpp
        src/ui/MainWindow.h
        src/ui/VesselView.cpp
        src/ui/VesselView.h
        src/ui/CineViewStudio.cpp
        src/ui/CineViewStudio.h
        src/ui/ECGWidget.cpp
        src/ui/ECGWidget.h
        src/ui/FluoroOverlay.cpp
        src/ui/FluoroOverlay.h
        src/ui/CarmView.cpp
        src/ui/CarmView.h
        src/ui/CarmController.cpp
        src/ui/CarmController.h
        src/ui/QuadPanel.cpp
        src/ui/QuadPanel.h
    )

    target_include_directories(EmboNav PRIVATE src)

    target_link_libraries(EmboNav PRIVATE
        embonav_core
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
    )

    # Copy assets next to the executable at build time
    set(EMBO_ASSETS_DIR "${CMAKE_SOURCE_DIR}/Data/3D" CACHE PATH
        "Directory with Vessel_Reconstructed.stl, VTPs, Muscular/Skeletal.stl and C-arm STLs")

    # Copy each asset individually so a missing optional file doesn't abort the build
    foreach(_asset
            Simplified_Vessel_2.stl
            Simplified_Vessel_2_centerlines.vtp
            Muscular.stl
            Skeletal.stl
            carm_head.stl
            carm_stand.stl
            carm_bed.stl
            carm_xray.stl)
        if(EXISTS "${EMBO_ASSETS_DIR}/${_asset}")
            add_custom_command(TARGET EmboNav POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${EMBO_ASSETS_DIR}/${_asset}"
                    "$<TARGET_FILE_DIR:EmboNav>"
                COMMENT "Copying ${_asset}")
        endif()
    endforeach()
endif()