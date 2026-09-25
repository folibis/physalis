cmake_minimum_required(VERSION 3.16)
project(Physalis VERSION 0.2.2 LANGUAGES C CXX)

# windres is beside the compiler but rarely on PATH; the bare name fails at build time.
if(WIN32 AND MINGW)
    get_filename_component(_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(_windres NAMES windres HINTS "${_mingw_bin}")
    if(_windres)
        set(CMAKE_RC_COMPILER "${_windres}" CACHE FILEPATH "Resource compiler" FORCE)
    endif()
endif()

# So install() reads "." as the install root, not a directory called ".".
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

set(PHYSALIS_QT_COMPONENTS Widgets Svg Qml Test)

# True when dir is a QtN package directory with every component above beside it.
function(_physalis_qt_complete dir major out)
    get_filename_component(_root "${dir}" DIRECTORY)
    foreach(_c IN LISTS PHYSALIS_QT_COMPONENTS)
        if(NOT EXISTS "${_root}/Qt${major}${_c}/Qt${major}${_c}Config.cmake")
            set(${out} FALSE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out} TRUE PARENT_SCOPE)
endfunction()

foreach(_major 6 5)
    if(QT_VERSION_MAJOR)
        break()
    endif()
    if(Qt${_major}_DIR)
        _physalis_qt_complete("${Qt${_major}_DIR}" ${_major} _complete)
        if(_complete)
            set(QT_VERSION_MAJOR ${_major})
            break()
        endif()
        unset(Qt${_major}_DIR CACHE)
    endif()
    foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
        file(GLOB _dirs "${_prefix}/lib/cmake/Qt${_major}"
                        "${_prefix}/lib/*/cmake/Qt${_major}")
        foreach(_dir IN LISTS _dirs)
            _physalis_qt_complete("${_dir}" ${_major} _complete)
            if(_complete)
                set(Qt${_major}_DIR "${_dir}" CACHE PATH "Qt package directory" FORCE)
                set(QT_VERSION_MAJOR ${_major})
                break()
            endif()
        endforeach()
        if(QT_VERSION_MAJOR)
            break()
        endif()
    endforeach()
endforeach()

if(QT_VERSION_MAJOR)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${PHYSALIS_QT_COMPONENTS})
else()
    find_package(Qt6 QUIET COMPONENTS ${PHYSALIS_QT_COMPONENTS})
    if(Qt6_FOUND)
        set(QT_VERSION_MAJOR 6)
    else()
        find_package(Qt5 5.15 QUIET COMPONENTS ${PHYSALIS_QT_COMPONENTS})
        if(Qt5_FOUND)
            set(QT_VERSION_MAJOR 5)
        endif()
    endif()
endif()

if(NOT QT_VERSION_MAJOR)
    message(FATAL_ERROR
        "Physalis needs Qt Widgets, Svg, Qml and Test, from Qt6 or Qt5 (5.15 or "
        "later). Nothing on CMAKE_PREFIX_PATH (${CMAKE_PREFIX_PATH}) provides all "
        "four. Point it at a complete Qt, or set Qt6_DIR.")
endif()

include(GNUInstallDirs)

# Plugins are found beside the executable and nowhere else. On Linux that means the
# executable lives in libdir with a link from bin; Qt resolves the link first.
if(WIN32)
    set(PHYSALIS_INSTALL_DIR "." CACHE STRING "Where the program and its plugins are installed")
else()
    set(PHYSALIS_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/physalis"
        CACHE STRING "Where the program and its plugins are installed")
endif()

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_standard_project_setup()
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(WITH_BOX2D "Build the Box2D engine plugin" ON)
option(WITH_CHIPMUNK "Build the Chipmunk2D engine plugin" ON)
option(WITH_EXPORTER_BOX2D_QT "Install the Box2D / Qt project export converter" ON)
option(WITH_EXPORTER_PLANCK "Install the Planck.js web page export converter" ON)
option(WITH_EXPORTER_QML "Install the QML / qml-box2d project export converter" ON)
option(WITH_TESTS "Build the test suite" ON)

if(WITH_TESTS AND NOT (WITH_BOX2D AND WITH_CHIPMUNK))
    message(FATAL_ERROR "WITH_TESTS needs WITH_BOX2D and WITH_CHIPMUNK: the test suite runs "
                        "scenes on both engines. Turn WITH_TESTS off to build without one.")
endif()
if(NOT WITH_BOX2D AND NOT WITH_CHIPMUNK)
    message(WARNING "No engine plugin is being built: Physalis will find nothing to run a "
                    "scene with.")
endif()

include(FetchContent)

if(WITH_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

set(PHYSALIS_VERSION "" CACHE STRING "The version to build as; project()'s own is used when this is empty")
if(PHYSALIS_VERSION STREQUAL "")
    set(PHYSALIS_FULL_VERSION "${PROJECT_VERSION}")
elseif(PHYSALIS_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
    set(PHYSALIS_FULL_VERSION "${PHYSALIS_VERSION}")
else()
    message(FATAL_ERROR "PHYSALIS_VERSION is \"${PHYSALIS_VERSION}\": it has to be digits and dots, as in 0.1.58.")
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
    src/FullScreenView.h
    src/FullScreenView.cpp
    src/ViewLayersCombo.h
    src/ViewLayersCombo.cpp
    src/PropertyPanel.h
    src/PropertyPanel.cpp
    src/SceneTree.h
    src/SceneTree.cpp
    src/Rule.h
    src/SceneVariable.h
    src/RulesPanel.h
    src/RulesPanel.cpp
    src/VariablesPanel.h
    src/VariablesPanel.cpp
    src/UndoStack.h
    src/UndoStack.cpp
    src/PropertyPane/PropertyPane.h
    src/PropertyPane/CatalogueRows.h
    src/PropertyPane/CatalogueRows.cpp
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
    src/SceneExporter.h
    src/SceneExporter.cpp
    src/SceneScreenshot.cpp
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

if(WIN32)
    set_property(SOURCE resources/physalis.rc APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/physalis.ico")
endif()

target_include_directories(PhysalisCore PUBLIC
    include
    src
    ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(PhysalisCore PUBLIC Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Svg Qt${QT_VERSION_MAJOR}::Qml)

configure_file(cmake/Version.h.in "${CMAKE_BINARY_DIR}/generated/Version.h" @ONLY)

add_executable(Physalis
    # The icon in Explorer and the task bar; Qt's window icon reaches neither.
    $<$<PLATFORM_ID:Windows>:resources/physalis.rc>
    src/main.cpp
)
target_link_libraries(Physalis PRIVATE PhysalisCore)
set_target_properties(Physalis PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)

if(WIN32)
    add_custom_command(TARGET Physalis POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Qt${QT_VERSION_MAJOR}::Qml> $<TARGET_FILE_DIR:Physalis>)
endif()

if(WITH_BOX2D)
    add_subdirectory(engines/box2d)
endif()
if(WITH_CHIPMUNK)
    add_subdirectory(engines/chipmunk)
endif()

install(TARGETS Physalis RUNTIME DESTINATION ${PHYSALIS_INSTALL_DIR}
                         BUNDLE DESTINATION ${PHYSALIS_INSTALL_DIR})

if(WIN32)
    install(FILES LICENSE DESTINATION ${PHYSALIS_INSTALL_DIR})
else()
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/physalis)
endif()

set(PHYSALIS_EXPORTERS)
if(WITH_EXPORTER_BOX2D_QT)
    list(APPEND PHYSALIS_EXPORTERS box2d-qt-project)
endif()
if(WITH_EXPORTER_PLANCK)
    list(APPEND PHYSALIS_EXPORTERS planck-js)
endif()
if(WITH_EXPORTER_QML)
    list(APPEND PHYSALIS_EXPORTERS qml-box2d)
endif()
foreach(exporter IN LISTS PHYSALIS_EXPORTERS)
    install(DIRECTORY exporters/${exporter} DESTINATION ${PHYSALIS_INSTALL_DIR}/exporters)
endforeach()

if(WIN32)
    get_target_property(PHYSALIS_QMAKE Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(PHYSALIS_QT_TOOLS "${PHYSALIS_QMAKE}" DIRECTORY)
    find_program(PHYSALIS_WINDEPLOYQT windeployqt HINTS "${PHYSALIS_QT_TOOLS}")

    if(PHYSALIS_WINDEPLOYQT)
        set(PHYSALIS_WINDEPLOYQT_SKIP)
        if(QT_VERSION_MAJOR GREATER_EQUAL 6)
            set(PHYSALIS_WINDEPLOYQT_SKIP
                --skip-plugin-types qmltooling,designer,qmllint,platforminputcontexts)
        endif()
        install(CODE "
            message(STATUS \"Collecting the Qt runtime with windeployqt\")
            execute_process(
                COMMAND \"${PHYSALIS_WINDEPLOYQT}\"
                        --dir \"\${CMAKE_INSTALL_PREFIX}/${PHYSALIS_INSTALL_DIR}\"
                        --no-translations
                        --no-system-d3d-compiler
                        --no-opengl-sw
                        # No QML in the program, so nothing to scan for.
                        --no-quick-import
                        ${PHYSALIS_WINDEPLOYQT_SKIP}
                        --compiler-runtime
                        \"\${CMAKE_INSTALL_PREFIX}/${PHYSALIS_INSTALL_DIR}/Physalis.exe\"
                COMMAND_ERROR_IS_FATAL ANY)
        ")
    else()
        message(WARNING "windeployqt was not found: a Windows package built from this "
                        "tree will not carry the Qt runtime.")
    endif()
else()
    install(CODE "
        set(where \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
        file(MAKE_DIRECTORY \"\${where}\")
        file(CREATE_LINK ../${PHYSALIS_INSTALL_DIR}/Physalis \"\${where}/physalis\" SYMBOLIC)
    ")
    configure_file(deploy/physalis.desktop.in
                   ${CMAKE_BINARY_DIR}/physalis.desktop @ONLY)
    install(FILES ${CMAKE_BINARY_DIR}/physalis.desktop
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    install(FILES resources/physalis.svg
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
    install(FILES deploy/physalis-scene.xml
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/mime/packages)
endif()

set(CPACK_PACKAGE_NAME "Physalis")
set(CPACK_PACKAGE_VENDOR "Physalis")
set(CPACK_PACKAGE_VERSION "${PHYSALIS_FULL_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A 2D physics scene editor")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Physalis")
set(CPACK_PACKAGE_EXECUTABLES "Physalis" "Physalis")
set(CPACK_STRIP_FILES ON)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

if(WIN32)
    find_program(PHYSALIS_MAKENSIS makensis
                 HINTS "$ENV{ProgramFiles}/NSIS" "$ENV{ProgramFiles\(x86\)}/NSIS"
                       "$ENV{LOCALAPPDATA}/Programs/NSIS")
    set(CPACK_GENERATOR "ZIP")
    if(PHYSALIS_MAKENSIS)
        list(PREPEND CPACK_GENERATOR "NSIS")
    endif()

    set(CPACK_NSIS_PACKAGE_NAME "Physalis")
    set(CPACK_NSIS_DISPLAY_NAME "Physalis")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/resources/physalis.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/resources/physalis.ico")
    # NSIS wants bitmaps of exactly 164x314 and 150x57, and an escaped path.
    set(PHYSALIS_NSIS_IMAGES
        WELCOMEFINISHPAGE_BITMAP "${CMAKE_SOURCE_DIR}/deploy/installer-welcome.bmp"
        UNWELCOMEFINISHPAGE_BITMAP "${CMAKE_SOURCE_DIR}/deploy/installer-welcome.bmp"
        HEADERIMAGE "${CMAKE_SOURCE_DIR}/deploy/installer-header.bmp")
    while(PHYSALIS_NSIS_IMAGES)
        list(POP_FRONT PHYSALIS_NSIS_IMAGES image path)
        file(TO_NATIVE_PATH "${path}" path)
        string(REPLACE "\\" "\\\\" path "${path}")
        set(CPACK_NSIS_MUI_${image} "${path}")
    endwhile()
    set(CPACK_NSIS_INSTALLED_ICON_NAME "Physalis.exe")

else()
    set(PHYSALIS_DISTRO "linux")
    if(EXISTS /etc/os-release)
        file(READ /etc/os-release PHYSALIS_OS_RELEASE)
        string(REGEX MATCH "(^|\n)ID=\"?([a-zA-Z0-9._-]+)\"?" _match "${PHYSALIS_OS_RELEASE}")
        set(PHYSALIS_DISTRO_ID "${CMAKE_MATCH_2}")
        string(REGEX MATCH "(^|\n)VERSION_ID=\"?([a-zA-Z0-9._-]+)\"?" _match "${PHYSALIS_OS_RELEASE}")
        if(PHYSALIS_DISTRO_ID)
            set(PHYSALIS_DISTRO "${PHYSALIS_DISTRO_ID}${CMAKE_MATCH_2}")
        endif()
    endif()

    # Asking for a package this machine has no tool for fails the whole run.
    find_program(PHYSALIS_DPKG_SHLIBDEPS dpkg-shlibdeps)
    find_program(PHYSALIS_RPMBUILD rpmbuild)
    set(CPACK_GENERATOR "TGZ")
    if(PHYSALIS_DPKG_SHLIBDEPS)
        list(PREPEND CPACK_GENERATOR "DEB")
    endif()
    if(PHYSALIS_RPMBUILD)
        list(PREPEND CPACK_GENERATOR "RPM")
    endif()
    if(NOT PHYSALIS_DPKG_SHLIBDEPS AND NOT PHYSALIS_RPMBUILD)
        message(WARNING "Neither dpkg-shlibdeps nor rpmbuild was found: cpack will produce "
                        "a tarball and no distribution package.")
    endif()

    execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE PHYSALIS_DEB_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT PHYSALIS_DEB_ARCH)
        set(PHYSALIS_DEB_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    set(CPACK_PACKAGE_FILE_NAME "physalis-${PHYSALIS_FULL_VERSION}-${PHYSALIS_DISTRO}-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_DEBIAN_PACKAGE_NAME "physalis")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Physalis")
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(CPACK_DEBIAN_FILE_NAME "physalis_${PHYSALIS_FULL_VERSION}_${PHYSALIS_DISTRO}_${PHYSALIS_DEB_ARCH}.deb")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_PACKAGE_CONTACT "Ruslan Muhlinin <ruslan@muhlinin.com>")
    set(CPACK_RPM_PACKAGE_NAME "physalis")
    set(CPACK_RPM_FILE_NAME "physalis-${PHYSALIS_FULL_VERSION}-${PHYSALIS_DISTRO}.${CMAKE_SYSTEM_PROCESSOR}.rpm")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")

    list(APPEND CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
         "${CMAKE_INSTALL_DATAROOTDIR}/applications"
         "${CMAKE_INSTALL_DATAROOTDIR}/icons"
         "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor"
         "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable"
         "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
         "${CMAKE_INSTALL_DATAROOTDIR}/mime"
         "${CMAKE_INSTALL_DATAROOTDIR}/mime/packages")
    set(CPACK_PACKAGE_DESCRIPTION
        "Physalis builds 2D physics scenes and runs them: shapes grouped into bodies,
"
        " joints between them, rules that fire on what happens. The simulation itself
"
        " comes from an engine plugin -- Box2D and Chipmunk2D are included.")
endif()

include(CPack)

if(WIN32)
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/Physalis.exe" PHYSALIS_EXE_NATIVE)
    string(REPLACE "\\" "\\\\" PHYSALIS_EXE_REG "${PHYSALIS_EXE_NATIVE}")
    configure_file(deploy/register-phys.reg.in ${CMAKE_BINARY_DIR}/register-phys.reg @ONLY)
    configure_file(deploy/unregister-phys.reg ${CMAKE_BINARY_DIR}/unregister-phys.reg COPYONLY)
endif()

if(WITH_TESTS)
    enable_testing()
    include(GoogleTest)
    add_subdirectory(tests)
endif()
