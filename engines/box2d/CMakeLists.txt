# The Box2D engine, as a shared library the application discovers at startup.
# Nothing here is visible to the application except through IPhysicsEngine.

# The library this plugin wraps is fetched here rather than by the project:
# it belongs to this engine, and nothing outside this directory links it.
include(FetchContent)
set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX2D_DOCS OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_VALIDATE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG v3.1.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(box2d)

add_library(physalis_box2d SHARED
    include/Box2DEngine.h
    src/Box2DPlugin.cpp
    src/Box2DEngine.cpp
    src/Box2DJointTypes.cpp
    src/Box2DJoints.cpp
    src/Box2DReadback.cpp
    src/Box2DCatalogue.cpp
)

# The shared descriptors and the engine interface, nothing else of the app.
# The shared interface, and this engine's own headers. Nothing of the
# application beyond include/ is reachable from here.
target_include_directories(physalis_box2d PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Qt for the container and string types the interface is expressed in; Box2D
# because that is what this plugin actually wraps.
target_link_libraries(physalis_box2d PRIVATE Qt6::Core Qt6::Gui box2d)

# Beside the executable, which is the only place the loader looks.
set_target_properties(physalis_box2d PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    PREFIX ""
)
