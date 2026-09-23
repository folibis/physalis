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
    # Box2D installs its own library and headers when asked to install; a
    # subdirectory brought in this way has those rules ignored, which keeps a
    # Physalis package to Physalis.
    EXCLUDE_FROM_ALL
)
# FetchContent_Declare only learned EXCLUDE_FROM_ALL in CMake 3.28. Below that
# it is accepted and ignored, and Box2D's headers and static library end up in
# the .deb -- Ubuntu 22.04 carries 3.22, so this is not hypothetical. Adding the
# subdirectory by hand is the same exclusion, and the single-argument
# FetchContent_Populate it needs is deprecated from 3.30, hence the split.
if(CMAKE_VERSION VERSION_LESS 3.28)
    FetchContent_GetProperties(box2d)
    if(NOT box2d_POPULATED)
        FetchContent_Populate(box2d)
        add_subdirectory(${box2d_SOURCE_DIR} ${box2d_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
else()
    FetchContent_MakeAvailable(box2d)
endif()

# CMAKE_AUTOMOC is on for the project and reaches this target too, which adds a
# C++ moc file to a C library that has no Qt in it -- and that file then
# inherits Box2D's own -Wmissing-prototypes, a C-only flag, and warns.
set_target_properties(box2d PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

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
target_link_libraries(physalis_box2d PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui box2d)

# Beside the executable, which is the only place the loader looks.
set_target_properties(physalis_box2d PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    PREFIX ""
)

# Beside the executable in an installed copy too, which is the only place the
# loader looks.
install(TARGETS physalis_box2d
        RUNTIME DESTINATION ${PHYSALIS_INSTALL_DIR}
        LIBRARY DESTINATION ${PHYSALIS_INSTALL_DIR})
