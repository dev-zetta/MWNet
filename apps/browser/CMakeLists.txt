
set(BROWSER_UI
        ${CMAKE_SOURCE_DIR}/files/tes3mp/ui/Main.ui
        ${CMAKE_SOURCE_DIR}/files/tes3mp/ui/ServerInfo.ui
        )
set(BROWSER
        main.cpp
        MainWindow.cpp
        ServerModel.cpp
        ServerInfoDialog.cpp
        MySortFilterProxyModel.cpp
        netutils/HTTPNetwork.cpp
        netutils/Utils.cpp
        netutils/QueryClient.cpp
        PingUpdater.cpp
        PingHelper.cpp
        QueryHelper.cpp
        ${CMAKE_SOURCE_DIR}/files/tes3mp/browser.rc
        )

set(BROWSER_HEADER_MOC
        MainWindow.hpp
        ServerModel.hpp
        ServerInfoDialog.hpp
        MySortFilterProxyModel.hpp
        PingUpdater.hpp
        PingHelper.hpp
        QueryHelper.hpp
        )

set(BROWSER_HEADER
        ${BROWSER_HEADER_MOC}
        netutils/HTTPNetwork.hpp
        netutils/Utils.hpp
        netutils/QueryClient.hpp
        Types.hpp
        )

source_group(browser FILES ${BROWSER} ${BROWSER_HEADER})

set(QT_USE_QTGUI 1)

# Set some platform specific settings
if(WIN32)
    set(GUI_TYPE WIN32)
    set(QT_USE_QTMAIN TRUE)
endif(WIN32)

if (DESIRED_QT_VERSION MATCHES 4)
    message(SEND_ERROR "QT4 is not supported.")
else()
    # Manually compile Qt resources using rcc
    set(BROWSER_RES ${CMAKE_SOURCE_DIR}/files/launcher/launcher.qrc)
    foreach(res_file ${BROWSER_RES})
        get_filename_component(res_name ${res_file} NAME_WE)
        set(res_output ${CMAKE_CURRENT_BINARY_DIR}/qrc_${res_name}.cpp)
        add_custom_command(
            OUTPUT ${res_output}
            COMMAND Qt5::rcc ${res_file} -o ${res_output}
            DEPENDS ${res_file}
            COMMENT "Compiling Qt resource ${res_file}"
        )
        list(APPEND RCC_SRCS ${res_output})
    endforeach()
    
    # Manually run moc on header files
    foreach(moc_file ${BROWSER_HEADER_MOC})
        get_filename_component(moc_name ${moc_file} NAME_WE)
        set(moc_output ${CMAKE_CURRENT_BINARY_DIR}/moc_${moc_name}.cpp)
        add_custom_command(
            OUTPUT ${moc_output}
            COMMAND Qt5::moc ${CMAKE_CURRENT_SOURCE_DIR}/${moc_file} -o ${moc_output}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${moc_file}
            COMMENT "Running moc on ${moc_file}"
        )
        list(APPEND MOC_SRCS ${moc_output})
    endforeach()
    
    # Manually run uic on UI files
    foreach(ui_file ${BROWSER_UI})
        get_filename_component(ui_name ${ui_file} NAME_WE)
        set(ui_output ${CMAKE_CURRENT_BINARY_DIR}/ui_${ui_name}.h)
        add_custom_command(
            OUTPUT ${ui_output}
            COMMAND Qt5::uic ${ui_file} -o ${ui_output}
            DEPENDS ${ui_file}
            COMMENT "Running uic on ${ui_file}"
        )
        list(APPEND UI_HDRS ${ui_output})
    endforeach()
endif()

include_directories(${CMAKE_CURRENT_BINARY_DIR})
if(NOT WIN32)
    include_directories(${LIBUNSHIELD_INCLUDE_DIR})
endif(NOT WIN32)

# Main executable
add_executable(tes3mp-browser
        ${GUI_TYPE}
        ${BROWSER}
        ${BROWSER_HEADER}
        ${RCC_SRCS}
        ${MOC_SRCS}
        ${UI_HDRS}
        )

if (WIN32)
    INSTALL(TARGETS tes3mp-browser RUNTIME DESTINATION ".")
endif (WIN32)

target_link_libraries(tes3mp-browser
        ${SDL2_LIBRARY_ONLY}
        ${RakNet_LIBRARY}
        components
        components_qt
        smhasher
        Boost::filesystem
        Boost::program_options
        Qt5::Svg
        )

if (DESIRED_QT_VERSION MATCHES 4)
#    target_link_libraries(tes3mp-browser ${QT_QTGUI_LIBRARY} ${QT_QTCORE_LIBRARY})
#    if(WIN32)
#        target_link_libraries(tes3mp-browser ${QT_QTMAIN_LIBRARY})
#    endif(WIN32)
else()
    target_link_libraries(tes3mp-browser Qt5::Widgets Qt5::Core)
endif()

if (BUILD_WITH_CODE_COVERAGE)
    add_definitions (--coverage)
    target_link_libraries(tes3mp-browser gcov)
endif()
