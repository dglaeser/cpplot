// SPDX-FileCopyrightText: 2024 Dennis Gläser <dennis.a.glaeser@gmail.com>
// SPDX-License-Identifier: MIT

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

    auto str_length = PyUnicode_GetLength(obj.get());
    if (str_length < 0)
        throw std::runtime_error("Could not get string length");

    std::size_t length = static_cast<std::size_t>(str_length);
    std::wstring wresult(length, wchar_t{' '});
    PyUnicode_AsWideChar(obj.get(), wresult.data(), length);

    std::string result(length, ' ');
    wcstombs(result.data(), wresult.data(), length);
    return result;
}

template<std::invocable F>
bool raises_pyerror(F&& f) {
    bool has_error = false;
    auto original_observer = cpplot::pyerror_observer.swap_with([&] () { has_error = true; PyErr_Clear(); });
    f();
    cpplot::pyerror_observer.swap_with(original_observer);
    return has_error;
}

struct test_point {
    double x;
    double y;
};

struct test_image {
    static constexpr std::size_t rows = 2;
    static constexpr std::size_t cols = 3;
    std::array<std::array<int, cols>, rows> values{{
        {1, 2, 3},
        {4, 5, 6}
    }};
};

namespace cpplot::traits {

template<>
struct image_size<test_image> {
    static constexpr grid get(const test_image& img) {
        return {.rows = img.rows, .cols = img.cols};
    }
};

template<>
struct image_access<test_image> {
    static constexpr auto at(const grid_location& loc, const test_image& img) {
        return img.values.at(loc.row).at(loc.col);
    }
};

template<std::size_t dimension>
struct point_access<test_point, dimension> {
    static auto get(const test_point& point) {
        return dimension == 0 ? point.x : point.y;
    }
};

}  // namespace cpplot::traits


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
        expect(f.set_title("some_title"));
        expect(eq(
            as_string(f.py_invoke("get_suptitle")),
            std::string{"some_title"}
        ));
    };

    "plot_values_default_x_axis"_test = [&] () {
        expect(!raises_pyerror([] () {
            expect(figure{}.axis().plot(std::vector{3.0, 4.0, 5.0}));
        }));
    };

    "plot_values"_test = [&] () {
        expect(!raises_pyerror([] () {
            expect(figure{}.axis().plot(
                std::vector{1.0, 2.0, 3.0},
                std::vector{3.0, 4.0, 5.0}
            ));
        }));
    };

    "plot_values_from_list"_test = [&] () {
        expect(!raises_pyerror([] () {
            expect(figure{}.axis().plot(
                std::list{1.0, 2.0, 3.0},
                std::list{3.0, 4.0, 5.0}
            ));
        }));
    };

    "plot_values_with_label"_test = [&] () {
        expect(!raises_pyerror([] () {
            expect(figure{}.axis().plot(
                std::vector{1.0, 2.0, 3.0},
                std::vector{3.0, 4.0, 5.0},
                kwargs(kw("label") = "some_label")
            ));
        }));
    };

    "plot_values_default_x_axis_with_kwargs"_test = [&] () {
        expect(!raises_pyerror([] () {
            expect(figure{}.axis().plot(
                std::vector{3.0, 4.0, 5.0},
                kwargs("label"_kw = "some_label")
            ));
        }));
    };

    "plot_values_with_label_from_string"_test = [&] () {
        expect(!raises_pyerror([] () {
            expect(figure{}.axis().plot(
                std::vector{1.0, 2.0, 3.0},
                std::vector{3.0, 4.0, 5.0},
                kwargs("label"_kw = std::string{"some_label"})
            ));
        }));
    };

    "plot_values_with_color"_test = [&] () {
        expect(!raises_pyerror([] () {
            expect(figure{}.axis().plot(
                std::vector{1.0, 2.0, 3.0},
                std::vector{3.0, 4.0, 5.0},
                kwargs("color"_kw = "blue")
            ));
        }));
    };

    "bar_plot"_test = [&] () {
        expect(!raises_pyerror([] () {
            expect(figure{}.axis().bar(std::vector<int>{1, 2, 3}));
        }));
    };

    "bar_plot_with_x_axis"_test = [&] () {
        expect(!raises_pyerror([] () {
            expect(figure{}.axis().bar(
                std::vector<std::string>{"a", "b", "c"},
                std::vector<int>{3, 2, 4}
            ));
        }));
    };

    "bar_plot_with_x_axis_mismatch__should_raise_pyerror"_test = [&] () {
        expect(raises_pyerror([] () {
            expect(!figure{}.axis().bar(
                std::vector<std::string>{"a", "b"},
                std::vector<int>{3, 2, 4}
            ));
        }));
    };

    "bar_plots_with_custom_labels_and_ticks"_test = [&] () {
        expect(!raises_pyerror([] () {
            auto fig = figure{};
            fig.axis().bar(
                std::vector<double>{0, 3, 6},
                std::vector<int>{1, 2, 3}, kwargs("label"_kw = "numbers")
            );
            fig.axis().bar(
                std::vector<double>{1, 4, 7},
                std::vector<int>{3, 4, 5}, kwargs("label"_kw = "numbers2")
            );
            fig.axis().add_legend();
            fig.axis().set_x_ticks(
                std::vector<double>{0.5, 3.5, 6.5},
                kwargs("labels"_kw = std::vector<std::string>{"a", "b", "c"})
            );
            fig.axis().set_y_ticks(
                std::vector<double>{4.0},
                kwargs("labels"_kw = std::vector<std::string>{"M"})
            );
        }));
    };

    "scatter_plot"_test = [&] () {
        expect(!raises_pyerror([] () {
            auto fig = figure{};
            fig.axis().scatter(
                std::vector<double>{0, 3, 6},
                std::vector<int>{1, 2, 3},
                kwargs("label"_kw = "numbers")
            );
        }));
    };

    "plot_histogram"_test = [&] () {
        expect(!raises_pyerror([] () {
            expect(figure{}.axis().hist(std::vector<int>{0, 1, 2, 10, 11, 12}, kwargs("bins"_kw = 3)));
        }));
    };

    "plot_image_from_range"_test = [&] () {
        expect(!raises_pyerror([] () {
            expect(figure{}.axis().imshow(std::vector<std::vector<int>>{{1, 2, 3}, {3, 4, 5}}));
        }));
    };

    "plot_image"_test = [&] () {
        expect(!raises_pyerror([] () {
            expect(figure{}.axis().imshow(test_image{}));
        }));
    };

    "fill_from_std_array"_test = [] () {
        expect(!raises_pyerror([] () {
            figure f;
            f.axis().imshow(std::vector<std::vector<int>>{{0, 1}, {2, 3}});
            expect(f.axis().fill(std::vector<std::array<int, 2>>{{0, 0}, {1, 0}, {1, 1}, {0, 1}}));
        }));
    };

    "fill_from_custom_point"_test = [] () {
        expect(!raises_pyerror([] () {
            figure f;
            f.axis().imshow(std::vector<std::vector<int>>{{0, 1}, {2, 3}});
            expect(f.axis().fill(std::vector<test_point>{{0, 0}, {1, 0}, {1, 1}, {0, 1}}));
        }));
    };

    "axis_title"_test = [&] () {
        expect(!raises_pyerror([] () {
            auto fig = figure();
            fig.axis().set_title("axis_title");
            expect(eq(
                as_string(fig.axis().py_invoke("get_title")),
                std::string{"axis_title"}
            ));
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
        expect(!raises_pyerror([] () {
            figure fig_matrix{{1, 2}};
            fig_matrix.axis_at({0, 0}).imshow(std::vector<std::vector<double>>{
                {1, 2, 3},
                {4, 5, 6},
                {7, 8, 9}
            });
            fig_matrix.axis_at({0, 0}).set_x_label("x values");
            fig_matrix.axis_at({0, 0}).set_y_label("y values");

            fig_matrix.axis_at({0, 1}).plot(
                std::vector{1, 2, 3},
                std::vector{4, 5, 6},
                kwargs("label"_kw = "some_label")
            );
            fig_matrix.axis_at({0, 1}).set_x_label("x values");
            fig_matrix.axis_at({0, 1}).set_y_label("y values");
        }));
    };

    "figure_matrix_quadratic"_test = [&] () {
        expect(!raises_pyerror([] () {
            figure fig_matrix{{.rows = 2, .cols = 2}};
        }));
    };

    "figure_matrix_single_column"_test = [&] () {
        expect(!raises_pyerror([] () {
            figure fig_matrix{{2, 1}};
        }));
    };

    return 0;
}
