#include <stdlib.h>
#include <filesystem>
#include <algorithm>
#include <list>

#include <boost/ut.hpp>

#include <cpplot/cpplot.hpp>

using namespace boost::ut;

std::size_t get_number_of_figures() {
    auto result = cpplot::py_invoke(cpplot::pyplot(), "get_fignums");
    return static_cast<std::size_t>(PyList_Size(result.get()));
}

std::string as_string(const cpplot::pyobject& obj) {
    if (!PyUnicode_Check(obj.get()))
        throw std::runtime_error("Given object does not represent a string");

    std::size_t length = static_cast<std::size_t>(PyUnicode_GET_LENGTH(obj.get()));
    std::wstring wresult(length, wchar_t{' '});
    PyUnicode_AsWideChar(obj.get(), wresult.data(), length);

    std::string result(length, ' ');
    wcstombs(result.data(), wresult.data(), length);
    return result;
}

template<std::invocable F>
bool produces_pyerror(F&& f) {
    bool has_error = false;
    bool print = cpplot::detail::pyerr_observers.print_error;
    cpplot::detail::pyerr_observers.print_error = false;
    cpplot::detail::pyerr_observers.push_back([&] () { has_error = true; });
    f();
    cpplot::detail::pyerr_observers.pop_back();
    cpplot::detail::pyerr_observers.print_error = print;
    return has_error;
}

int main() {
    using namespace cpplot;
    using namespace cpplot::literals;

    "fig_close"_test = [&] () {
        expect(eq(get_number_of_figures(), std::size_t{0}));
        figure f;  expect(eq(get_number_of_figures(), std::size_t{1}));
        f.close(); expect(eq(get_number_of_figures(), std::size_t{0}));
    };

    "fig_title"_test = [] () {
        figure f;
        f.set_title("some_title");
        auto title = f.py_invoke("get_suptitle");
        expect(eq(as_string(title), std::string{"some_title"}));
    };

    "plot_values_default_x_axis"_test = [&] () {
        expect(!produces_pyerror([] () {
            figure{}.axis().plot(std::vector{3.0, 4.0, 5.0});
        }));
    };

    "plot_values"_test = [&] () {
        expect(!produces_pyerror([] () {
            figure{}.axis().plot(
                std::vector{1.0, 2.0, 3.0},
                std::vector{3.0, 4.0, 5.0}
            );
        }));
    };

    "plot_values_from_list"_test = [&] () {
        expect(!produces_pyerror([] () {
            figure{}.axis().plot(
                std::list{1.0, 2.0, 3.0},
                std::list{3.0, 4.0, 5.0}
            );
        }));
    };

    "plot_values_with_label"_test = [&] () {
        expect(!produces_pyerror([] () {
            figure{}.axis().plot(
                std::vector{1.0, 2.0, 3.0},
                std::vector{3.0, 4.0, 5.0},
                kwargs::from(kw("label") = "some_label")
            );
        }));
    };

    "plot_values_default_x_axis_with_kwargs"_test = [&] () {
        expect(!produces_pyerror([] () {
            figure{}.axis().plot(
                std::vector{3.0, 4.0, 5.0},
                kwargs::from("label"_kw = "some_label")
            );
        }));
    };

    "plot_values_with_label_from_string"_test = [&] () {
        expect(!produces_pyerror([] () {
            figure{}.axis().plot(
                std::vector{1.0, 2.0, 3.0},
                std::vector{3.0, 4.0, 5.0},
                kwargs::from("label"_kw = std::string{"some_label"})
            );
        }));
    };

    "plot_values_with_color"_test = [&] () {
        expect(!produces_pyerror([] () {
            figure{}.axis().plot(
                std::vector{1.0, 2.0, 3.0},
                std::vector{3.0, 4.0, 5.0},
                kwargs::from("color"_kw = "blue")
            );
        }));
    };

    "bar_plot"_test = [&] () {
        expect(!produces_pyerror([] () {
            figure{}.axis().bar(std::vector<int>{1, 2, 3});
        }));
    };

    "bar_plot_with_x_axis"_test = [&] () {
        expect(!produces_pyerror([] () {
            figure{}.axis().bar(
                std::vector<std::string>{"a", "b", "c"},
                std::vector<int>{3, 2, 4}
            );
        }));
    };

    "bar_plot_with_x_axis_mismatch"_test = [&] () {
        expect(produces_pyerror([] () {
            figure{}.axis().bar(
                std::vector<std::string>{"a", "b"},
                std::vector<int>{3, 2, 4}
            );
        }));
    };

    "bar_plots_with_labels"_test = [&] () {
        expect(!produces_pyerror([] () {
            auto fig = figure{};
            fig.axis().bar(
                std::vector<double>{0, 3, 6},
                std::vector<int>{1, 2, 3}, kwargs::from("label"_kw = "numbers")
            );
            fig.axis().bar(
                std::vector<double>{1, 4, 7},
                std::vector<int>{3, 4, 5}, kwargs::from("label"_kw = "numbers2")
            );
            fig.axis().add_legend();
            fig.axis().set_x_ticks(
                std::vector<double>{0.5, 3.5, 6.5},
                kwargs::from("labels"_kw = std::vector<std::string>{"a", "b", "c"})
            );
            fig.axis().set_y_ticks(
                std::vector<double>{4.0},
                kwargs::from("labels"_kw = std::vector<std::string>{"M"})
            );
        }));
    };

    "plot_image"_test = [&] () {
        expect(!produces_pyerror([] () {
            auto fig = figure();
            fig.axis().imshow(
                std::vector<std::vector<int>>{{1, 2, 3}, {3, 4, 5}}
            );
        }));
    };

    "axis_title"_test = [&] () {
        expect(!produces_pyerror([] () {
            auto fig = figure();
            fig.set_title("axis");
        }));
    };

    "figure_save"_test = [&] () {
        figure fig;
        std::filesystem::remove("some_figure.png");
        expect(!std::filesystem::exists("some_figure.png"));
        fig.save_to("some_figure.png");
        expect(std::filesystem::exists("some_figure.png"));
    };

    "figure_matrix_single_row"_test = [&] () {
        expect(!produces_pyerror([] () {
            figure fig_matrix{{1, 2}};
            fig_matrix.axis({0, 0}).imshow(std::vector<std::vector<double>>{
                {1, 2, 3},
                {4, 5, 6},
                {7, 8, 9}
            });
            fig_matrix.axis({0, 0}).set_x_label("x values");
            fig_matrix.axis({0, 0}).set_y_label("y values");

            fig_matrix.axis({0, 1}).plot(
                std::vector{1, 2, 3},
                std::vector{4, 5, 6},
                kwargs::from("label"_kw = "some_label")
            );
            fig_matrix.axis({0, 1}).set_x_label("x values");
            fig_matrix.axis({0, 1}).set_y_label("y values");
        }));
    };

    "figure_matrix_quadratic"_test = [&] () {
        expect(!produces_pyerror([] () {
            figure fig_matrix{{.rows = 2, .cols = 2}};
        }));
    };

    "figure_matrix_single_column"_test = [&] () {
        expect(!produces_pyerror([] () {
            figure fig_matrix{{2, 1}};
        }));
    };

    return 0;
}
