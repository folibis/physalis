# The Chipmunk2D engine, as a shared library the application discovers at
# startup. Nothing here is visible to the application except through
# IPhysicsEngine.

# Fetched here rather than by the project: it belongs to this engine alone.
#
# SOURCE_SUBDIR names a folder with no CMakeLists.txt, so the sources are
# downloaded but Chipmunk's own build is never configured. That build forces
# CMAKE_BUILD_TYPE for the whole tree when none is set, switches on
# -ffast-math, and builds its demos -- none of which this project wants. The
# library is a flat folder of C files, so it is compiled below instead.
include(FetchContent)
FetchContent_Declare(chipmunk2d
    GIT_REPOSITORY https://codeberg.org/slembcke/Chipmunk2D.git
    # Master as of January 2026: 7.0.3 plus the fixes made since that release.
    GIT_TAG 5b45034a8761518800e03dd79f33c3aad0bf7fee
    SOURCE_SUBDIR do-not-configure
)
FetchContent_MakeAvailable(chipmunk2d)

file(GLOB CHIPMUNK_SOURCES CONFIGURE_DEPENDS "${chipmunk2d_SOURCE_DIR}/src/*.c")
add_library(chipmunk_static STATIC ${CHIPMUNK_SOURCES})
target_include_directories(chipmunk_static PUBLIC "${chipmunk2d_SOURCE_DIR}/include")
set_target_properties(chipmunk_static PROPERTIES
    C_STANDARD 99
    C_EXTENSIONS ON
    # Linked into a shared library, so it has to be relocatable on Linux too.
    POSITION_INDEPENDENT_CODE ON
)
# Without NDEBUG Chipmunk prints a banner for every space it makes, and its
# soft assertions abort the process instead of reporting anything. The plugin
# checks what it hands over itself.
target_compile_definitions(chipmunk_static PRIVATE NDEBUG)
if(UNIX)
    target_link_libraries(chipmunk_static PUBLIC m)
endif()

add_library(physalis_chipmunk SHARED
    include/ChipmunkEngine.h
    src/ChipmunkPlugin.cpp
    src/ChipmunkEngine.cpp
    src/ChipmunkEvents.cpp
    src/ChipmunkJointTypes.cpp
    src/ChipmunkJoints.cpp
    src/ChipmunkReadback.cpp
    src/ChipmunkCatalogue.cpp
)

target_include_directories(physalis_chipmunk PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(physalis_chipmunk PRIVATE Qt6::Core Qt6::Gui chipmunk_static)

# Beside the executable, which is the only place the loader looks.
set_target_properties(physalis_chipmunk PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    PREFIX ""
)

# Beside the executable in an installed copy too, which is the only place the
# loader looks.
install(TARGETS physalis_chipmunk
        RUNTIME DESTINATION ${PHYSALIS_INSTALL_DIR}
        LIBRARY DESTINATION ${PHYSALIS_INSTALL_DIR})
