set(WIZARD
    componentselectionpage.cpp
    conclusionpage.cpp
    existinginstallationpage.cpp
    importpage.cpp
    installationtargetpage.cpp
    intropage.cpp
    languageselectionpage.cpp
    main.cpp
    mainwizard.cpp
    methodselectionpage.cpp
)

if(WIN32)
    list(APPEND WIZARD ${CMAKE_SOURCE_DIR}/files/windows/openmw-wizard.rc)
endif()

set(WIZARD_HEADER
    componentselectionpage.hpp
    conclusionpage.hpp
    existinginstallationpage.hpp
    importpage.hpp
    installationtargetpage.hpp
    intropage.hpp
    languageselectionpage.hpp
    mainwizard.hpp
    methodselectionpage.hpp
)

if (OPENMW_USE_UNSHIELD)
    set (WIZARD
         ${WIZARD}
         installationpage.cpp
         unshield/inisettings.cpp
         unshield/unshieldworker.cpp
    )
    set (WIZARD_HEADER
         ${WIZARD_HEADER}
         installationpage.hpp
         unshield/inisettings.hpp
         unshield/unshieldworker.hpp
    )
    add_compile_definitions(OPENMW_USE_UNSHIELD)
endif (OPENMW_USE_UNSHIELD)


source_group(wizard FILES ${WIZARD} ${WIZARD_HEADER})

set(QT_USE_QTGUI 1)

set (WIZARD_RES ${CMAKE_SOURCE_DIR}/files/wizard/wizard.qrc)

# Set some platform specific settings
if(WIN32)
    set(WIZARD_RES ${WIZARD_RES} ${CMAKE_SOURCE_DIR}/files/windows/QWindowsVistaDark/dark.qrc)
    set(GUI_TYPE WIN32)
    set(QT_USE_QTMAIN TRUE)
endif(WIN32)

# Manually compile Qt resources using rcc
foreach(res_file ${WIZARD_RES})
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

if (OPENMW_USE_UNSHIELD)
    include_directories(${LIBUNSHIELD_INCLUDE_DIRS})
endif()

openmw_add_executable(openmw-wizard
    ${GUI_TYPE}
    ${WIZARD}
    ${WIZARD_HEADER}
    ${RCC_SRCS}
)

add_dependencies(openmw-wizard qm-files)

target_link_libraries(openmw-wizard
    components_qt
)

target_link_libraries(openmw-wizard Qt::Widgets Qt::Core Qt::Svg)

if (OPENMW_USE_UNSHIELD)
    target_link_libraries(openmw-wizard ${LIBUNSHIELD_LIBRARIES})
endif()

if (WIN32)
    target_sources(openmw-wizard PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()

if(DPKG_PROGRAM)
    INSTALL(TARGETS openmw-wizard RUNTIME DESTINATION games COMPONENT openmw-wizard)
endif()

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-wizard PRIVATE --coverage)
    target_link_libraries(openmw-wizard gcov)
endif()

if (WIN32)
    INSTALL(TARGETS openmw-wizard RUNTIME DESTINATION ".")
endif(WIN32)

if(USE_QT)
    set_property(TARGET openmw-wizard PROPERTY AUTOMOC ON)
    set_property(TARGET openmw-wizard PROPERTY AUTOUIC ON)
    set_property(TARGET openmw-wizard PROPERTY AUTOUIC_SEARCH_PATHS ui)
endif(USE_QT)
