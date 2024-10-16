#include <list>
#include <algorithm>

#include <boost/ut.hpp>

#include <cpplot/cpplot.hpp>

using namespace boost::ut;

int main() {
    using namespace cpplot;

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
            LinePlot::from(
                x = std::vector{1.0, 2.0, 3.0},
                y = std::vector{3.0, 4.0, 5.0}
            )
        ));
    };

    "plot_values_from_list"_test = [&] () {
        expect(figure().plot(
            LinePlot::from(
                x = std::list{1.0, 2.0, 3.0},
                y = std::list{3.0, 4.0, 5.0}
            )
        ));
    };

    "plot_values_with_label"_test = [&] () {
        expect(figure().plot(
            LinePlot::from(
                x = std::vector{1.0, 2.0, 3.0},
                y = std::vector{3.0, 4.0, 5.0}
            ),
            with(label = "some_label")
        ));
    };

    "plot_values_with_label_from_string"_test = [&] () {
        expect(figure().plot(
            LinePlot::from(
                x = std::vector{1.0, 2.0, 3.0},
                y = std::vector{3.0, 4.0, 5.0}
            ),
            with(label = std::string{"some_label"})
        ));
    };

    "plot_values_with_color"_test = [&] () {
        expect(figure().plot(
            LinePlot::from(
                x = std::vector{1.0, 2.0, 3.0},
                y = std::vector{3.0, 4.0, 5.0}
            ),
            with(color = "blue")
        ));
    };

    "plot_image"_test = [&] () {
        set_style("default");
        expect(figure().set_image(
            std::vector<std::vector<int>>{{1, 2, 3}, {3, 4, 5}}
        ));
    };

    "get_all"_test = [&] () {
        const auto ids = get_all_figure_ids();
        const auto figs = get_all_figures();
        expect(eq(ids.size(), figs.size()));
        expect(std::all_of(figs.begin(), figs.end(), [&] (const auto& fig) {
            return std::count(ids.begin(), ids.end(), fig.id());
        }));
    };

    show_all(false);

    return 0;
}
