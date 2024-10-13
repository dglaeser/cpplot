include(FetchContent)
FetchContent_Declare(
  ut
  GIT_REPOSITORY https://github.com/boost-ext/ut
  GIT_TAG v1.1.9
)
FetchContent_MakeAvailable(ut)

find_package(cpplot)
function (cpplot_add_test NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE cpplot::cpplot Boost::ut)
    add_test(NAME ${NAME} COMMAND ./${NAME})
endfunction ()

cpplot_add_test(test_suite test_suite.cpp)
