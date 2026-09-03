# One binary for the whole suite. It links PhysalisCore -- the same objects the
# application runs on -- and a main() that stands a QApplication up before
# GoogleTest runs, which widgets need and GoogleTest's own main does not do.

# Qt is deployed beside the executables, but the binary still has to find the Qt
# runtime when CTest launches it, so hand it the bin directory the build used.
get_filename_component(PHYSALIS_QT_BIN "${Qt6_DIR}/../../../bin" ABSOLUTE)
file(TO_NATIVE_PATH "${PHYSALIS_QT_BIN}" PHYSALIS_QT_BIN)

# Every .cpp here is a scenario; dropping one in is all it takes to add a test.
file(GLOB PHYSALIS_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(PhysalisTests ${PHYSALIS_TEST_SOURCES})
# Qt6::Test is for the scenarios that drive real keystrokes.
target_link_libraries(PhysalisTests PRIVATE PhysalisCore GTest::gtest Qt6::Test)
target_compile_definitions(PhysalisTests PRIVATE
    PHYSALIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# The engine is a plugin: it has to exist before a test can find it.
add_dependencies(PhysalisTests physalis_box2d)

# The engine plugin is loaded from beside the running binary, and it is built
# to the top of the build tree. qt_standard_project_setup() only points
# executables there on Windows, so without this the tests land in tests/ on
# Linux and find no engine at all.
set_target_properties(PhysalisTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Qt6Test is the one Qt library the application itself does not use, so its DLL
# is not already beside the executable -- and test discovery runs the binary
# without the PATH the tests themselves are given. Qt6Qml is there for the same
# reason: QJSEngine runs the export converters, and it arrived after whatever
# put the rest of the Qt runtime beside the build.
if(WIN32)
    add_custom_command(TARGET PhysalisTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Qt6::Test> $<TARGET_FILE_DIR:PhysalisTests>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Qt6::Qml> $<TARGET_FILE_DIR:PhysalisTests>)
endif()

# CTest gets one entry per GoogleTest case. Discovery is deferred to test time:
# running the binary at build time would fail, since Qt is not on PATH then.
# Discovery times out after 5 seconds by default, and a cold Qt DLL load on a
# Windows CI runner comfortably exceeds that, so give it room.
gtest_discover_tests(PhysalisTests
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    # PATH goes through ENVIRONMENT_MODIFICATION because it needs prepending,
    # not overwriting -- the child would lose /usr/bin otherwise. PHYSALIS_SETTINGS
    # is just set outright and goes through ENVIRONMENT, which sidesteps a
    # gtest_discover_tests quirk: a semicolon-separated ENVIRONMENT_MODIFICATION
    # value gets re-emitted as separate arguments to set_tests_properties, and
    # everything after the first entry is silently mis-read as another property.
    # PHYSALIS_SETTINGS gives the run its own preferences file: a test must not
    # read, or overwrite, the settings of the application beside it.
    PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${PHYSALIS_QT_BIN}"
        ENVIRONMENT
            "PHYSALIS_SETTINGS=${CMAKE_CURRENT_BINARY_DIR}/test-settings.ini"
        TIMEOUT 180)
