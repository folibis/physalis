cmake_minimum_required(VERSION 3.16)
# Major and minor are set here; the build number counts up on its own.
project(Physalis VERSION 0.1 LANGUAGES C CXX)

# windres ships beside the MinGW compiler but is usually not on PATH, and CMake
# then records the bare name, which fails at build time. Resolve it explicitly
# before the RC language is enabled.
if(WIN32 AND MINGW)
    get_filename_component(_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(_windres NAMES windres HINTS "${_mingw_bin}")
    if(_windres)
        set(CMAKE_RC_COMPILER "${_windres}" CACHE FILEPATH "Resource compiler" FORCE)
    endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
# Forms live in ui/, not beside the sources that use them.
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

find_package(Qt6 COMPONENTS Widgets Svg Test REQUIRED)

qt_standard_project_setup()

include(FetchContent)

# GoogleTest, for the test suite. The engines fetch their own
# dependencies -- nothing up here knows what any of them wrap.
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# OBJECT, not STATIC: a static library lets the linker drop the compiled .qrc
# because nothing references it by name, and every icon then comes out empty.
# Bumped before a Release build, so a shipped copy can be traced back to one.
# Debug builds skip it: the number would change on every build, which forces a
# recompile and relink even when nothing else moved.
set(PHYSALIS_BUILD_COUNTER "${CMAKE_BINARY_DIR}/build_number.txt")
# Only a build that ships gets a new number. In Debug the counter would move
# on every build, and because Version.h is an output of this command ninja
# would regenerate it, recompile main.cpp and relink -- every single time.
set(PHYSALIS_STAMPS_BUILDS OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"
   OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    set(PHYSALIS_STAMPS_BUILDS ON)
    add_custom_target(PhysalisVersion ALL
        COMMAND ${CMAKE_COMMAND}
                -DCOUNTER_FILE=${PHYSALIS_BUILD_COUNTER}
                -DTEMPLATE_FILE=${CMAKE_SOURCE_DIR}/cmake/Version.h.in
                -DOUTPUT_FILE=${CMAKE_BINARY_DIR}/generated/Version.h
                -DVERSION_MAJOR=${PROJECT_VERSION_MAJOR}
                -DVERSION_MINOR=${PROJECT_VERSION_MINOR}
                -P ${CMAKE_SOURCE_DIR}/cmake/BumpBuildNumber.cmake
        BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/Version.h
        COMMENT "Stamping the build number"
        VERBATIM
    )
endif()
add_library(PhysalisCore OBJECT
    src/MainWindow.h
    src/MainWindow.cpp
    src/CanvasScene.h
    src/CanvasScene.cpp
    src/ShapeItem.h
    src/ShapeItem.cpp
    src/ExplosionItem.h
    src/ExplosionItem.cpp
    src/RayItem.h
    src/RayItem.cpp
    src/RectangleItem.h
    src/RectangleItem.cpp
    src/CircleItem.h
    src/CircleItem.cpp
    src/PolygonItem.h
    src/PolygonItem.cpp
    src/RulerWidget.h
    src/RulerWidget.cpp
    src/PropertyPanel.h
    src/PropertyPanel.cpp
    src/SceneTree.h
    src/SceneTree.cpp
    src/Rule.h
    src/RulesPanel.h
    src/RulesPanel.cpp
    src/UndoStack.h
    src/UndoStack.cpp
    src/PropertyPane/PropertyPane.h
    src/PropertyPane/ExplosionPropertyPane.h
    src/PropertyPane/ExplosionPropertyPane.cpp
    src/PropertyPane/RayPropertyPane.h
    src/PropertyPane/RayPropertyPane.cpp
    src/PropertyPane/SensorPropertyPane.h
    src/PropertyPane/SensorPropertyPane.cpp
    src/PropertyPane/PropertyPaneFactory.h
    src/PropertyPane/PropertyPaneFactory.cpp
    src/PropertyPane/ShapePropertyPaneBase.h
    src/PropertyPane/ShapePropertyPaneBase.cpp
    src/PropertyPane/RectanglePropertyPane.h
    src/PropertyPane/CirclePropertyPane.h
    src/PropertyPane/PolygonPropertyPane.h
    src/PropertyPane/PolygonPropertyPane.cpp
    src/PropertyPane/FieldPropertyPane.h
    src/PropertyPane/FieldPropertyPane.cpp
    src/PropertyPane/PhysicsPropertyPane.h
    src/PropertyPane/PhysicsPropertyPane.cpp
    src/PropertyPane/JointPropertyPane.h
    src/PropertyPane/JointPropertyPane.cpp
    src/OptionsDialog.h
    src/OptionsDialog.cpp
    src/AboutDialog.h
    src/AboutDialog.cpp
    src/EditorMode.h
    src/Naming.h
    src/PhysicsBody.h
    src/PhysicsBody.cpp
    src/Joint.h
    src/Joint.cpp
    src/SimulationController.h
    src/SimulationController.cpp
    src/SceneSerializer.h
    src/SceneSerializer.cpp
    src/Icons.h
    src/Icons.cpp
    src/ObjectIcons.cpp
    include/PhysicsTypes.h
    include/IPhysicsEngine.h
    include/EngineRegistry.h
    include/PluginApi.h
    include/JointTypes.h
    src/EngineRegistry.cpp
    ui/MainWindow.ui
    ui/OptionsDialog.ui
    ui/AboutDialog.ui
    resources/icons.qrc
)

# windres does not report what the .rc pulls in, so without this an edited
# icon leaves the executable carrying the previous one.
if(WIN32)
    set_property(SOURCE resources/physalis.rc APPEND PROPERTY
                 OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/physalis.ico")
endif()

target_include_directories(PhysalisCore PUBLIC
    include
    src
    ${CMAKE_BINARY_DIR}/generated)
# No Box2D here any more: the engine lives in a plugin.
target_link_libraries(PhysalisCore PUBLIC Qt6::Widgets Qt6::Svg)

# The header has to exist for any build, stamped or not. Written once here at
# configure time, with whatever the counter last reached.
if(EXISTS "${PHYSALIS_BUILD_COUNTER}")
    file(READ "${PHYSALIS_BUILD_COUNTER}" PHYSALIS_BUILD_NUMBER)
    string(STRIP "${PHYSALIS_BUILD_NUMBER}" PHYSALIS_BUILD_NUMBER)
endif()
if(NOT PHYSALIS_BUILD_NUMBER MATCHES "^[0-9]+$")
    set(PHYSALIS_BUILD_NUMBER 0)
endif()
set(PHYSALIS_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(PHYSALIS_VERSION_MINOR ${PROJECT_VERSION_MINOR})
configure_file(cmake/Version.h.in "${CMAKE_BINARY_DIR}/generated/Version.h" @ONLY)

# The application itself is only main() plus the icon resource.
add_executable(Physalis
    # Gives the executable its icon in Explorer and the task bar; the Qt
    # window icon alone does not reach either.
    $<$<PLATFORM_ID:Windows>:resources/physalis.rc>
    src/main.cpp
)
target_link_libraries(Physalis PRIVATE PhysalisCore)
# Only a build that stamps needs to wait for the stamp.
if(PHYSALIS_STAMPS_BUILDS)
    add_dependencies(Physalis PhysalisVersion)
endif()

add_subdirectory(engines/box2d)

# Windows file association for *.phys. The template carries the keys; the path
# to the executable is filled in here, so a moved or renamed build cannot leave
# a .reg file pointing at nothing. Neither file is run by the build -- they are
# for the user to double-click.
if(WIN32)
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/Physalis.exe" PHYSALIS_EXE_NATIVE)
    # A .reg file wants every backslash doubled.
    string(REPLACE "\\" "\\\\" PHYSALIS_EXE_REG "${PHYSALIS_EXE_NATIVE}")
    configure_file(deploy/register-phys.reg.in
                   ${CMAKE_BINARY_DIR}/register-phys.reg @ONLY)
    configure_file(deploy/unregister-phys.reg
                   ${CMAKE_BINARY_DIR}/unregister-phys.reg COPYONLY)
endif()

enable_testing()
# gtest_discover_tests(), so CTest lists each case rather than one binary.
include(GoogleTest)
add_subdirectory(tests)
