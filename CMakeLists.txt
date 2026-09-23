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

# install() destinations are normalised, so "." means the install root rather
# than a directory called "." beside it.
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
# Forms live in ui/, not beside the sources that use them.
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

set(PHYSALIS_QT_COMPONENTS Widgets Svg Qml Test)

# Which Qt to use is settled before asking for it. CMake keeps the first Qt on
# CMAKE_PREFIX_PATH even when it turns out to be missing a module, and a second
# QtConfig cannot be loaded after it -- the first one's helpers are
# include-guarded, so the second's go missing. A distribution's Qt without its
# Svg and Qml packages would otherwise hide a complete Qt further down the path.
if(Qt6_DIR)
    set(QT_VERSION_MAJOR 6)
elseif(Qt5_DIR)
    set(QT_VERSION_MAJOR 5)
else()
    foreach(_major 6 5)
        foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
            file(GLOB _dirs "${_prefix}/lib/cmake/Qt${_major}"
                            "${_prefix}/lib/*/cmake/Qt${_major}")
            foreach(_dir IN LISTS _dirs)
                get_filename_component(_root "${_dir}" DIRECTORY)
                set(_complete TRUE)
                foreach(_c IN LISTS PHYSALIS_QT_COMPONENTS)
                    if(NOT EXISTS "${_root}/Qt${_major}${_c}/Qt${_major}${_c}Config.cmake")
                        set(_complete FALSE)
                        break()
                    endif()
                endforeach()
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
        if(QT_VERSION_MAJOR)
            break()
        endif()
    endforeach()
endif()

if(QT_VERSION_MAJOR)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${PHYSALIS_QT_COMPONENTS})
else()
    # Nothing on the prefix path carried all of them; let the default search
    # have its say. QUIET because Qt5Config turns a missing required component
    # into an aborted configure, and the message below is the better one.
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

# Engine plugins are found beside the executable and nowhere else, so an
# installed Physalis keeps them together with it. On Windows that is the
# install root; on Linux the executable itself lives in a library directory,
# with a link from the path -- Qt resolves that link before it looks for
# plugins, so they are still "beside" it.
if(WIN32)
    set(PHYSALIS_INSTALL_DIR "." CACHE STRING "Where the program and its plugins are installed")
else()
    set(PHYSALIS_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/physalis"
        CACHE STRING "Where the program and its plugins are installed")
endif()

# Qt 6.3+ only; what it would set is already set explicitly above.
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_standard_project_setup()
endif()

# Everything, dependencies included. The engines are shared libraries and the
# physics they wrap is fetched and built static, and a static library without
# it cannot go into a shared one: on Linux the plugin failed to link with
# "recompile with -fPIC".
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# What is built and what an installed copy carries. All of it by default; a
# part turned off is left out of both. The exporters are JavaScript, not
# compiled, so theirs decide only whether the folder is installed.
option(WITH_BOX2D "Build the Box2D engine plugin" ON)
option(WITH_CHIPMUNK "Build the Chipmunk2D engine plugin" ON)
option(WITH_EXPORTER_BOX2D_QT "Install the Box2D / Qt project export converter" ON)
option(WITH_EXPORTER_PLANCK "Install the Planck.js web page export converter" ON)
option(WITH_EXPORTER_QML "Install the QML / qml-box2d project export converter" ON)
option(WITH_TESTS "Build the test suite" ON)

# The suite runs scenes on both engines, so without one of them a good part of
# it cannot pass; better to say so here than in a hundred failures.
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
    # GoogleTest, for the test suite. The engines fetch their own
    # dependencies -- nothing up here knows what any of them wrap.
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# What this build calls itself, in the window's About box and on every package
# it produces. A release is built from a tag and handed it --
# cmake -DPHYSALIS_VERSION=0.1.58 -- and everything else falls back to what
# project() says above, so a working copy needs nothing.
set(PHYSALIS_VERSION "" CACHE STRING
    "The version to build as; project()'s own is used when this is empty")
if(PHYSALIS_VERSION STREQUAL "")
    set(PHYSALIS_FULL_VERSION "${PROJECT_VERSION}")
elseif(PHYSALIS_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
    set(PHYSALIS_FULL_VERSION "${PHYSALIS_VERSION}")
else()
    message(FATAL_ERROR
        "PHYSALIS_VERSION is \"${PHYSALIS_VERSION}\": it has to be digits and dots, as in 0.1.58.")
endif()

# OBJECT, not STATIC: a static library lets the linker drop the compiled .qrc
# because nothing references it by name, and every icon then comes out empty.
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
    src/RulesPanel.h
    src/RulesPanel.cpp
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
# No Box2D here any more: the engine lives in a plugin. Qml is here for
# QJSEngine alone -- export converters are JavaScript, and nothing in the
# application uses QML itself.
target_link_libraries(PhysalisCore PUBLIC Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Svg Qt${QT_VERSION_MAJOR}::Qml)

configure_file(cmake/Version.h.in "${CMAKE_BINARY_DIR}/generated/Version.h" @ONLY)

# The application itself is only main() plus the icon resource.
add_executable(Physalis
    # Gives the executable its icon in Explorer and the task bar; the Qt
    # window icon alone does not reach either.
    $<$<PLATFORM_ID:Windows>:resources/physalis.rc>
    src/main.cpp
)
target_link_libraries(Physalis PRIVATE PhysalisCore)
# A windowed program, not a console one: without this Windows opens a console
# behind the window and keeps it there for as long as the program runs.
set_target_properties(Physalis PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)

# QJSEngine runs the export converters, and the Qml DLL is not among those the
# rest of the Qt runtime was deployed with.
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

# --- what an installed copy is made of --------------------------------------

install(TARGETS Physalis RUNTIME DESTINATION ${PHYSALIS_INSTALL_DIR}
                         BUNDLE DESTINATION ${PHYSALIS_INSTALL_DIR})

# The terms the program is under travel with it: beside it on Windows, where a
# package is a folder, and under share/doc on Linux, where they belong.
if(WIN32)
    install(FILES LICENSE DESTINATION ${PHYSALIS_INSTALL_DIR})
else()
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/physalis)
endif()

# The export converters are read at run time from the folder named in Options,
# which defaults to this one. One folder each, as the options above choose.
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
    # The Qt runtime travels with the program on Windows: windeployqt copies
    # the libraries it actually uses, the platform and image plugins it cannot
    # start without, and the compiler's own runtime. Run against the installed
    # executable, so everything lands beside it -- the layout Qt finds by
    # itself, with no qt.conf to keep in step.
    #
    # Nothing of Qt is shipped on Linux: the package asks the distribution for
    # it instead.
    get_target_property(PHYSALIS_QMAKE Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(PHYSALIS_QT_TOOLS "${PHYSALIS_QMAKE}" DIRECTORY)
    find_program(PHYSALIS_WINDEPLOYQT windeployqt HINTS "${PHYSALIS_QT_TOOLS}")

    if(PHYSALIS_WINDEPLOYQT)
        # The on-screen keyboard drags Qt Quick in behind it, and a desktop
        # editor has no use for either. Qt5's windeployqt has no
        # --skip-plugin-types and stops at an option it does not know.
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
                        # QJSEngine is the only part of Qml this uses: there is
                        # no QML in the program, and nothing to scan for.
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
    # On the path, pointing at the copy in the library directory. Through
    # DESTDIR, which is how cpack installs into a staging folder: without it the
    # link is written to the real /usr/bin, which is refused -- and would not be
    # in the package anyway.
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
    # So a *.phys file opens in Physalis from the file manager.
    install(FILES deploy/physalis-scene.xml
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/mime/packages)
endif()

# --- packaging ---------------------------------------------------------------
# Windows: an installer carrying Qt. Linux: a .deb that depends on the
# distribution's Qt rather than carrying one.

set(CPACK_PACKAGE_NAME "Physalis")
set(CPACK_PACKAGE_VENDOR "Physalis")
set(CPACK_PACKAGE_VERSION "${PHYSALIS_FULL_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A 2D physics scene editor")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Physalis")
set(CPACK_PACKAGE_EXECUTABLES "Physalis" "Physalis")
set(CPACK_STRIP_FILES ON)
# Shown as the installer's licence page, and installed beside the program.
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

if(WIN32)
    # NSIS builds the installer, and is not part of this toolchain -- without
    # it a plain archive is still produced, which is the same tree unpacked.
    # It does not put itself on the PATH, so where it installs is looked in too.
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
    # The picture down the side of the first and last page, and the strip along
    # the top of the pages between. NSIS takes bitmaps of exactly these sizes
    # (164 x 314 and 150 x 57), and the path in its own escaped form.
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
    # The *.phys file association is left to the two .reg files in deploy/,
    # which the build writes with the path they are meant to register. NSIS
    # would have to carry the same registry keys as escaped script, and a
    # mis-escaped one writes a broken association rather than none.

else()
    # Which distribution a Linux package was built against decides where it can
    # be installed, so it is part of the name: physalis_0.1.58_ubuntu24.04_amd64.deb
    # says what "physalis_0.1.58_amd64.deb" leaves the reader to guess.
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

    # Which package this machine can build is what it has the tools for: a
    # Debian box has dpkg-shlibdeps, a Red Hat one rpmbuild, and asking for the
    # other fails the whole run. A tarball needs neither and is always made.
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

    # dpkg has its own names for the architectures -- amd64, not x86_64.
    execute_process(COMMAND dpkg --print-architecture
                    OUTPUT_VARIABLE PHYSALIS_DEB_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT PHYSALIS_DEB_ARCH)
        set(PHYSALIS_DEB_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    set(CPACK_ARCHIVE_FILE_NAME
        "physalis-${PHYSALIS_FULL_VERSION}-${PHYSALIS_DISTRO}-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_DEBIAN_PACKAGE_NAME "physalis")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Physalis")
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(CPACK_DEBIAN_FILE_NAME
        "physalis_${PHYSALIS_FULL_VERSION}_${PHYSALIS_DISTRO}_${PHYSALIS_DEB_ARCH}.deb")
    # Qt comes from the distribution: shlibdeps reads what the binaries
    # actually link and writes the dependencies from the packages providing
    # them, so the list cannot drift from the build.
    # Every binary in the package is scanned, the engine plugins included, so
    # the Qt packages they need are named whether or not the program itself
    # links the same ones.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_PACKAGE_CONTACT "Physalis <physalis@example.invalid>")

    # The same bargain as the .deb: rpmbuild reads what the binaries link and
    # writes the Requires itself, so the list cannot drift from the build.
    set(CPACK_RPM_PACKAGE_NAME "physalis")
    set(CPACK_RPM_FILE_NAME
        "physalis-${PHYSALIS_FULL_VERSION}-${PHYSALIS_DISTRO}.${CMAKE_SYSTEM_PROCESSOR}.rpm")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    # These belong to the distribution's own packages; an rpm claiming them
    # conflicts with filesystem and refuses to install beside it.
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

if(WITH_TESTS)
    enable_testing()
    # gtest_discover_tests(), so CTest lists each case rather than one binary.
    include(GoogleTest)
    add_subdirectory(tests)
endif()
