#include <list>
#include <algorithm>

#include <boost/ut.hpp>

#include <cpplot/cpplot.hpp>

using namespace boost::ut;

int main() {
    using namespace cpplot;
    using namespace cpplot::literals;

    set_style("ggplot");

    "plot_fig_id"_test = [&] () {
        expect(!figure_exists(42));
        figure(42);
        expect(figure_exists(42));
    };

    "plot_fig_close"_test = [&] () {
        expect(!figure_exists(43));
        auto fig = figure(43);
        expect(figure_exists(43));
        expect(fig.close());
        expect(!figure_exists(43));
        figure(43);
        expect(figure_exists(43));
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

    "figure_matrix"_test = [&] () {
        auto fig_matrix = figure_matrix(1, 2);
        auto& img = fig_matrix.at(0, 0);
        img.set_image(std::vector<std::vector<double>>{
            {1, 2, 3},
            {4, 5, 6}
        });
        img.add_colorbar();

        fig_matrix.at(0, 1).plot(std::vector{1, 2, 3}, std::vector{4, 5, 6}, with("label"_kw = "some_label"));
    };

    show_all_figures(false);

    "get_all"_test = [&] () {
        const auto ids = get_all_figure_ids();
        auto figs = get_all_figures();
        expect(eq(ids.size(), figs.size()));
        expect(std::all_of(figs.begin(), figs.end(), [&] (const auto& fig) {
            return std::count(ids.begin(), ids.end(), fig.id());
        }));
        for (auto& fig : figs)
            fig.close();
        expect(eq(get_all_figure_ids().size(), std::size_t{0}));
    };

    "close_all"_test = [] () {
        figure();
        figure();
        figure();
        expect(ge(get_all_figure_ids().size(), std::size_t{3}));
        close_all_figures();
        expect(eq(get_all_figure_ids().size(), std::size_t{0}));
    };

    return 0;
}
