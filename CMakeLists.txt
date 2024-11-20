# SPDX-FileCopyrightText: 2024 Dennis Gläser <dennis.a.glaeser@gmail.com>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.18)

project(cpplot VERSION 0.1)
option(CPPLOT_INCLUDE_TESTS "Control whether or not to include the test suite" ON)
option(CPPLOT_INCLUDE_EXAMPLES "Control whether or not to include the examples" ON)
option(CPPLOT_DISABLE_PYTHON_DEBUG_BUILD "If set to true, python is included w/o debug info even for debug builds" OFF)

find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development)
add_library(cpplot INTERFACE)
target_compile_features(cpplot INTERFACE cxx_std_20)
target_link_libraries(cpplot INTERFACE Python::Python)
if (CPPLOT_DISABLE_PYTHON_DEBUG_BUILD)
    target_compile_definitions(cpplot INTERFACE CPPLOT_DISABLE_PYTHON_DEBUG_BUILD)
endif ()

include(GNUInstallDirs)
set(CPPLOT_INSTALL_CMAKE_DATA_DIR "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake")

target_include_directories(cpplot
    INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
              $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/src/cpplot
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(
    TARGETS cpplot
    EXPORT ${PROJECT_NAME}_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(
    EXPORT ${PROJECT_NAME}_Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE cpplot::
    DESTINATION ${CPPLOT_INSTALL_CMAKE_DATA_DIR}
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}-config.cmake.in"
    "${PROJECT_BINARY_DIR}/cmake/pkg/${PROJECT_NAME}Config.cmake"
    INSTALL_DESTINATION ${CPPLOT_INSTALL_CMAKE_DATA_DIR}
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/cpplotConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(
    FILES
        "${PROJECT_BINARY_DIR}/cmake/pkg/${PROJECT_NAME}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/cpplotConfigVersion.cmake"
    DESTINATION ${CPPLOT_INSTALL_CMAKE_DATA_DIR}
)

# Alias to be used in test suite or when included as sub-project
add_library(cpplot::cpplot ALIAS cpplot)

if (CPPLOT_INCLUDE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif ()
if (CPPLOT_INCLUDE_EXAMPLES)
    enable_testing()
    add_subdirectory(examples)
endif ()
