#include <boost/ut.hpp>

#include <cpplot/cpplot.hpp>

using namespace boost::ut;

int main() {
    using namespace cpplot;

    set_style("ggplot");

    "plot_values"_test = [&] () {
        expect(figure().plot(
            LinePlot::from(
                x = std::vector{1.0, 2.0, 3.0},
                y = std::vector{3.0, 4.0, 5.0}
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

    show_all(false);

    return 0;
}
