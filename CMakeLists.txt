cmake_minimum_required(VERSION 3.24)
project(PlasmaAudioManager VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
add_library(pam_backend backend/pipewire/Graph.cpp backend/devices/TemporaryDevices.cpp backend/routing/Router.cpp backend/persistence/Configuration.cpp backend/persistence/Recording.cpp)
target_include_directories(pam_backend PUBLIC backend)
target_link_libraries(pam_backend PUBLIC Qt6::Core PkgConfig::PIPEWIRE)
target_compile_options(pam_backend PRIVATE -Wall -Wextra)
add_executable(pam-backend backend/main.cpp backend/Service.h)
target_link_libraries(pam-backend PRIVATE pam_backend Qt6::DBus)
option(BUILD_GUI "Build QML client and standalone GUI" ON)
if(BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickControls2 Qml)
    find_package(KF6Kirigami REQUIRED)
    qt_policy(SET QTP0001 NEW)
    qt_add_library(pam_frontend SHARED)
    qt_add_qml_module(pam_frontend
        URI PlasmaAudioManager
        VERSION 1.0
        OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/PlasmaAudioManager"
        SOURCES frontend/BackendClient.h frontend/BackendClient.cpp)
    target_include_directories(pam_frontend PRIVATE frontend)
    target_link_libraries(pam_frontend PRIVATE Qt6::Qml Qt6::DBus)
    qt_add_executable(pam-gui frontend/main.cpp)
    # Dynamic QML plugins are resolved at runtime, including the Plasma-only host.
    set_target_properties(pam-gui PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    target_link_libraries(pam-gui PRIVATE Qt6::Quick Qt6::QuickControls2)
    target_compile_definitions(pam-gui PRIVATE PAM_QML_BUILD_DIR="${CMAKE_BINARY_DIR}/qml"
        PAM_GUI_BUILD_DIR="${CMAKE_BINARY_DIR}")
    set(pam_ui_dir "${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/package/contents/ui")
    qt_add_resources(pam-gui "popup" PREFIX "/ui" BASE "${pam_ui_dir}" FILES
        "${pam_ui_dir}/AudioPopup.qml" "${pam_ui_dir}/DeviceRow.qml"
        "${pam_ui_dir}/CableRow.qml" "${pam_ui_dir}/AppRow.qml"
        "${pam_ui_dir}/SectionHeading.qml" "${pam_ui_dir}/EmptyState.qml"
        "${pam_ui_dir}/RoutingView.qml")
    qt_add_resources(pam-gui "preview" PREFIX "/" BASE frontend FILES frontend/Preview.qml)
    add_dependencies(pam-gui pam_frontendplugin)
    include(GNUInstallDirs)
    set(PAM_QML_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/qt6/qml/PlasmaAudioManager"
        CACHE PATH "Install directory for the native QML client module")
    install(TARGETS pam_frontend pam_frontendplugin LIBRARY DESTINATION "${PAM_QML_INSTALL_DIR}")
    set_target_properties(pam_frontendplugin PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    install(FILES "${CMAKE_BINARY_DIR}/qml/PlasmaAudioManager/qmldir"
        "${CMAKE_BINARY_DIR}/qml/PlasmaAudioManager/pam_frontend.qmltypes"
        DESTINATION "${PAM_QML_INSTALL_DIR}")
    set(pam_widget_dir "${CMAKE_INSTALL_DATADIR}/plasma/plasmoids/org.kooraseru.plasma.audiomanager")
    install(DIRECTORY plasmoid/package/ DESTINATION "${pam_widget_dir}" COMPONENT Widget)
    # Bundle the client module so a user-installed widget needs no global QML path changes.
    install(TARGETS pam_frontend pam_frontendplugin LIBRARY DESTINATION
        "${pam_widget_dir}/contents/ui/PlasmaAudioManager" COMPONENT Widget)
    install(FILES "${CMAKE_BINARY_DIR}/qml/PlasmaAudioManager/qmldir"
        "${CMAKE_BINARY_DIR}/qml/PlasmaAudioManager/pam_frontend.qmltypes"
        DESTINATION "${pam_widget_dir}/contents/ui/PlasmaAudioManager" COMPONENT Widget)
    add_custom_target(plasmoid-package ALL
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageWidget.cmake
        DEPENDS pam_frontend pam_frontendplugin
        COMMENT "Build self-contained native Plasma widget package")
    find_program(KPACKAGETOOL6_EXECUTABLE kpackagetool6)
    if(KPACKAGETOOL6_EXECUTABLE)
        add_custom_target(install-widget
            COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR}
                -DKPACKAGE_TOOL=${KPACKAGETOOL6_EXECUTABLE}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallWidget.cmake
            DEPENDS plasmoid-package
            COMMENT "Install Audio Manager into the current user's Plasma widget library")
    endif()
    install(TARGETS pam-backend pam-gui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES frontend/org.kooraseru.plasma.audiomanager.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
endif()
include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(pam-routing-fixture tests/routing_fixture.cpp)
    target_link_libraries(pam-routing-fixture PRIVATE Qt6::Core PkgConfig::PIPEWIRE)
    find_program(WIREPLUMBER_EXECUTABLE wireplumber)
    if(WIREPLUMBER_EXECUTABLE)
        add_test(NAME isolated-recording COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/lifecycle.py $<TARGET_FILE:pam-backend>
            --recording $<TARGET_FILE:pam-routing-fixture>)
        set_tests_properties(isolated-recording PROPERTIES TIMEOUT 60)
    endif()
    add_test(NAME isolated-persistence COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/lifecycle.py $<TARGET_FILE:pam-backend>
        --persistence $<TARGET_FILE:pam-routing-fixture>)
    set_tests_properties(isolated-persistence PROPERTIES TIMEOUT 60)
    add_test(NAME isolated-routing COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/lifecycle.py $<TARGET_FILE:pam-backend>
        --routing $<TARGET_FILE:pam-routing-fixture>)
    set_tests_properties(isolated-routing PROPERTIES TIMEOUT 50)
    add_executable(pam-graph-race tests/graph_race.cpp)
    target_link_libraries(pam-graph-race PRIVATE pam_backend)
    add_test(NAME isolated-lifecycle COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/lifecycle.py $<TARGET_FILE:pam-backend>
        --race $<TARGET_FILE:pam-graph-race>)
    set_tests_properties(isolated-lifecycle PROPERTIES TIMEOUT 40)
    if(BUILD_GUI)
        find_program(QT6_QMLTESTRUNNER_EXECUTABLE qmltestrunner
            HINTS "${Qt6_DIR}/../../qt6/bin" "${Qt6_DIR}/../../../bin"
            NO_DEFAULT_PATH REQUIRED)
        if(WIREPLUMBER_EXECUTABLE)
            add_test(NAME isolated-recording-gui COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/lifecycle.py $<TARGET_FILE:pam-backend>
                --recording $<TARGET_FILE:pam-routing-fixture>
                --recording-gui ${QT6_QMLTESTRUNNER_EXECUTABLE} "${CMAKE_BINARY_DIR}/qml")
            set_tests_properties(isolated-recording-gui PROPERTIES TIMEOUT 60)
        endif()
        add_test(NAME isolated-routing-gui COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/lifecycle.py $<TARGET_FILE:pam-backend>
            --routing $<TARGET_FILE:pam-routing-fixture>
            --routing-gui ${QT6_QMLTESTRUNNER_EXECUTABLE} "${CMAKE_BINARY_DIR}/qml")
        set_tests_properties(isolated-routing-gui PROPERTIES TIMEOUT 60)
        add_test(NAME isolated-gui COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/lifecycle.py $<TARGET_FILE:pam-backend>
            --gui ${QT6_QMLTESTRUNNER_EXECUTABLE} "${CMAKE_BINARY_DIR}/qml")
        set_tests_properties(isolated-gui PROPERTIES TIMEOUT 60)
        find_program(PLASMAWINDOWED_EXECUTABLE plasmawindowed)
        if(PLASMAWINDOWED_EXECUTABLE AND KPACKAGETOOL6_EXECUTABLE)
            add_test(NAME plasma-widget-host COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/plasmoid_smoke.py
                "${CMAKE_BINARY_DIR}/org.kooraseru.plasma.audiomanager.plasmoid"
                ${PLASMAWINDOWED_EXECUTABLE} ${KPACKAGETOOL6_EXECUTABLE})
            set_tests_properties(plasma-widget-host PROPERTIES TIMEOUT 30)
        endif()
    endif()
endif()
