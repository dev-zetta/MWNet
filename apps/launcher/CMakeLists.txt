set(LAUNCHER
    datafilespage.cpp
    graphicspage.cpp
    sdlinit.cpp
    main.cpp
    maindialog.cpp
    textslotmsgbox.cpp
    importpage.cpp
    settingspage.cpp

    utils/cellnameloader.cpp
    utils/profilescombobox.cpp
    utils/textinputdialog.cpp
    utils/lineedit.cpp
    utils/openalutil.cpp

    ${CMAKE_SOURCE_DIR}/files/windows/launcher.rc
)

set(LAUNCHER_HEADER
    datafilespage.hpp
    graphicspage.hpp
    sdlinit.hpp
    maindialog.hpp
    textslotmsgbox.hpp
    importpage.hpp
    settingspage.hpp

    utils/cellnameloader.hpp
    utils/profilescombobox.hpp
    utils/textinputdialog.hpp
    utils/lineedit.hpp
    utils/openalutil.hpp
)

source_group(launcher FILES ${LAUNCHER} ${LAUNCHER_HEADER})

set(QT_USE_QTGUI 1)

set (LAUNCHER_RES ${CMAKE_SOURCE_DIR}/files/launcher/launcher.qrc)

# Set some platform specific settings
if(WIN32)
    set(LAUNCHER_RES ${LAUNCHER_RES} ${CMAKE_SOURCE_DIR}/files/windows/QWindowsVistaDark/dark.qrc)
    set(GUI_TYPE WIN32)
    set(QT_USE_QTMAIN TRUE)
endif(WIN32)

# Manually compile Qt resources using rcc
foreach(res_file ${LAUNCHER_RES})
    get_filename_component(res_name ${res_file} NAME_WE)
    set(res_output ${CMAKE_CURRENT_BINARY_DIR}/qrc_${res_name}.cpp)
    add_custom_command(
        OUTPUT ${res_output}
        COMMAND Qt6::rcc ${res_file} -o ${res_output}
        DEPENDS ${res_file}
        COMMENT "Compiling Qt resource ${res_file}"
    )
    list(APPEND RCC_SRCS ${res_output})
endforeach()

if(NOT WIN32)
    include_directories(${LIBUNSHIELD_INCLUDE_DIR})
endif(NOT WIN32)

# Main executable
openmw_add_executable(openmw-launcher
    ${GUI_TYPE}
    ${LAUNCHER}
    ${LAUNCHER_HEADER}
    ${RCC_SRCS}
)

add_dependencies(openmw-launcher qm-files)

if (WIN32)
    INSTALL(TARGETS openmw-launcher RUNTIME DESTINATION ".")
endif (WIN32)

target_link_libraries(openmw-launcher
    SDL2::SDL2
    ${OPENAL_LIBRARY}
    components_qt
    components
    smhasher
    Boost::filesystem
    Boost::program_options
)

target_link_libraries(openmw-launcher Qt::Widgets Qt::Core Qt::Svg)

if (WIN32)
    target_sources(openmw-launcher PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-launcher PRIVATE --coverage)
    target_link_libraries(openmw-launcher gcov)
endif()

if(USE_QT)
    set_property(TARGET openmw-launcher PROPERTY AUTOMOC ON)
    set_property(TARGET openmw-launcher PROPERTY AUTOUIC ON)
    set_property(TARGET openmw-launcher PROPERTY AUTOUIC_SEARCH_PATHS ui)
endif(USE_QT)

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(openmw-launcher PRIVATE
        <boost/program_options/options_description.hpp>

        <algorithm>
        <string>
        <vector>
    )
endif()
