# SPDX-FileCopyrightText: 2024 Dennis Gläser <dennis.a.glaeser@gmail.com>
# SPDX-License-Identifier: MIT

function (cpplot_add_example NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE cpplot::cpplot)
    add_test(NAME ${NAME} COMMAND ./${NAME})
endfunction ()

cpplot_add_test(example example.cpp)
