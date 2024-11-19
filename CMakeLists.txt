cmake_minimum_required(VERSION 3.18)

project(cpplot VERSION 0.1)
option(CPPLOT_INCLUDE_TESTS "Control whether or not to include the test suite" ON)
option(CPPLOT_INCLUDE_EXAMPLES "Control whether or not to include the examples" ON)
option(CPPLOT_DISABLE_PYTHON_DEBUG_BUILD "If set to true, python is included w/o debug info even for debug builds" OFF)

include(GNUInstallDirs)
find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development)
add_library(cpplot INTERFACE)
target_compile_features(cpplot INTERFACE cxx_std_20)
target_link_libraries(cpplot INTERFACE Python::Python)
if (CPPLOT_DISABLE_PYTHON_DEBUG_BUILD)
    target_compile_definitions(cpplot INTERFACE CPPLOT_DISABLE_PYTHON_DEBUG_BUILD)
endif ()
target_include_directories(cpplot
    INTERFACE $<BUILD_INTERFACE:${cpplot_SOURCE_DIR}/src>
              $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
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
