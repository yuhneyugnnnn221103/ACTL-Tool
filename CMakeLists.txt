cmake_minimum_required(VERSION 3.16)

project(ACTL_Tool VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets SerialPort Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets SerialPort Network)

set(PROJECT_SOURCES
        main.cpp app/settings.h app/context.h
        ui/auth.h ui/auth.cpp
        ui/trb_config_page.h ui/trb_config_page.cpp
        ui/firmware_page.h ui/firmware_page.cpp
        ui/main_window.h ui/main_window.cpp
        ui/overview_grid.h ui/overview_grid.cpp
        ui/trb_detail_page.h ui/trb_detail_page.cpp


)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(ACTL_Tool
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        core/frame.h core/frame.cpp
        core/frame_parser.h core/frame_parser.cpp
        core/transport.h
        core/serial_transport.h core/serial_transport.cpp
        core/tcp_server_transport.h core/tcp_server_transport.cpp
        core/replay_transport.h core/replay_transport.cpp
        core/link.h core/link.cpp
        proto/field_table.h proto/field_table.cpp
        proto/trb_monitor_proto.h proto/trb_monitor_proto.cpp
        proto/trb_control_proto.h proto/trb_control_proto.cpp
        services/trb_control.h services/trb_control.cpp
        proto/trb_config_proto.h proto/trb_config_proto.cpp
        services/trb_config.h services/trb_config.cpp
        proto/fpga_ota_proto.h proto/fpga_ota_proto.cpp
        services/fpga_ota.h services/fpga_ota.cpp
        proto/stm_ota_proto.h proto/stm_ota_proto.cpp
        services/stm_ota.h services/stm_ota.cpp
        model/thresholds.h model/thresholds.cpp
        services/alarm_engine.h services/alarm_engine.cpp
        services/csv_logger.h services/csv_logger.cpp
        model/device_store.h model/device_store.cpp
        services/trb_monitor.h services/trb_monitor.cpp
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET ACTL_Tool APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(ACTL_Tool SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(ACTL_Tool
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(ACTL_Tool PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::SerialPort
Qt${QT_VERSION_MAJOR}::Network)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.ACTL_Tool)
endif()
set_target_properties(ACTL_Tool PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS ACTL_Tool
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(ACTL_Tool)
endif()
