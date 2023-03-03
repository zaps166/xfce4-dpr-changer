cmake_minimum_required(VERSION 3.8)
project(xfce4-dpr-changer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

find_package(Qt6 REQUIRED
    COMPONENTS
    Widgets
    DBus
)

add_executable(${PROJECT_NAME}
    main.cpp
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    Qt6::Widgets
    Qt6::DBus
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
    -DDATA_DIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}"
)

configure_file(
    "${CMAKE_SOURCE_DIR}/xfce4-dpr-changer.desktop.in"
    "${CMAKE_BINARY_DIR}/xfce4-dpr-changer.desktop"
    IMMEDIATE @ONLY
)

install(TARGETS ${PROJECT_NAME}
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES "${CMAKE_BINARY_DIR}/xfce4-dpr-changer.desktop"
    DESTINATION "${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart"
)

if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY
    )

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    )
endif()
