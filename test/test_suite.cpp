#include <list>
#include <algorithm>

#include <boost/ut.hpp>

#include <cpplot/cpplot.hpp>

using namespace boost::ut;

int main() {
    using namespace cpplot;
    using namespace cpplot::literals;

    set_style("ggplot");

    "fig_close"_test = [&] () {
        expect(eq(cpplot::detail::MPLWrapper::instance().number_of_active_figures(), std::size_t{0}));
        auto fig = figure();
        expect(eq(cpplot::detail::MPLWrapper::instance().number_of_active_figures(), std::size_t{1}));
        expect(fig.close());
        expect(eq(cpplot::detail::MPLWrapper::instance().number_of_active_figures(), std::size_t{0}));
    };

    "fig_title"_test = [] () {
        expect(figure().set_title("title"));
    };

    "plot_values_default_x_axis"_test = [&] () {
        expect(figure().plot(
            std::vector{3.0, 4.0, 5.0}
        ));
    };

    "plot_values"_test = [&] () {
        expect(figure().plot(
            std::vector{1.0, 2.0, 3.0},
            std::vector{3.0, 4.0, 5.0}
        ));
    };

    "plot_values_from_list"_test = [&] () {
        expect(figure().plot(
            std::list{1.0, 2.0, 3.0},
            std::list{3.0, 4.0, 5.0}
        ));
    };

    "plot_values_with_label"_test = [&] () {
        expect(figure().plot(
            std::vector{1.0, 2.0, 3.0},
            std::vector{3.0, 4.0, 5.0},
            with(kw("label") = "some_label")
        ));
    };

    "plot_values_default_x_axis_with_kwargs"_test = [&] () {
        expect(figure().plot(
            std::vector{3.0, 4.0, 5.0},
            with("label"_kw = "some_label")
        ));
    };

    "plot_values_with_label_from_string"_test = [&] () {
        expect(figure().plot(
            std::vector{1.0, 2.0, 3.0},
            std::vector{3.0, 4.0, 5.0},
            with("label"_kw = std::string{"some_label"})
        ));
    };

    "plot_values_with_color"_test = [&] () {
        expect(figure().plot(
            std::vector{1.0, 2.0, 3.0},
            std::vector{3.0, 4.0, 5.0},
            with("color"_kw = "blue")
        ));
    };

    "plot_image"_test = [&] () {
        set_style("default");
        auto fig = figure();
        expect(fig.set_image(
            std::vector<std::vector<int>>{{1, 2, 3}, {3, 4, 5}}
        ));
        expect(fig.add_colorbar());
    };

    "axis_title"_test = [&] () {
        set_style("default");
        auto fig = figure();
        expect(fig.set_title("axis"));
    };

    "figure_matrix_single_row"_test = [&] () {
        auto fig_matrix = figure({1, 2});
        fig_matrix.adjust_layout(with("wspace"_kw = 0.8));
        auto& img = fig_matrix.at({0, 0});
        img.set_image(std::vector<std::vector<double>>{
            {1, 2, 3},
            {4, 5, 6},
            {7, 8, 9}
        });
        img.add_colorbar();
        img.set_x_label("x values");
        img.set_y_label("y values");

        fig_matrix.at({0, 1}).plot(
            std::vector{1, 2, 3},
            std::vector{4, 5, 6},
            with("label"_kw = "some_label")
        );
        fig_matrix.at({0, 1}).set_x_label("x values");
        fig_matrix.at({0, 1}).set_y_label("y values");
        show_all_figures();
    };

    "figure_matrix_quadratic"_test = [&] () {
        auto fig_matrix = figure({.nrows = 2, .ncols = 2});
    };

    "figure_matrix_single_column"_test = [&] () {
        auto fig_matrix = figure({2, 1});
    };

    return 0;
}
