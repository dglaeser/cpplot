#include <numbers>
#include <ranges>
#include <cmath>
#include <iostream>

#include <cpplot/cpplot.hpp>

int main() {
    using namespace cpplot;
    using namespace cpplot::literals;

    const auto x_values = std::views::iota(0, 100) | std::views::transform([] (const std::size_t i) {
        return std::numbers::pi*2.0*static_cast<double>(i)/99.0;
    });
    const auto sine_values = x_values | std::views::transform([] (const double x) { return std::sin(x); });

    // let's make a single figure and plot a sine function, simply over the indices
    figure sine_default_x_axis;
    sine_default_x_axis.axis().plot(sine_values, kwargs::from("label"_kw = "sine"));
    sine_default_x_axis.set_title("The sine function");
    sine_default_x_axis.axis().add_legend();

    // let's do the same, but use the actual x values on the x axis and use a cool style
    figure sine{style{.name = "ggplot"}};
    sine.axis().plot(x_values, sine_values, kwargs::from("label"_kw = "sin(x)"));
    sine.axis().add_legend();

    // let's plot the two below each other
    figure stacked{{.rows = 2, .cols = 1}};
    stacked.axis({.row = 0, .col = 0}).plot(sine_values);
    stacked.axis({.row = 1, .col = 0}).plot(x_values, sine_values);

    // ... or side-by-side (also, let's add titles this time)
    figure side_by_side{{1, 2}};
    side_by_side.axis({0, 0}).plot(sine_values);
    side_by_side.axis({0, 1}).plot(x_values, sine_values);
    side_by_side.axis({0, 0}).set_title("sine");
    side_by_side.axis({0, 1}).set_title("sin(x)");
    side_by_side.set_title("2 sine plots");

    // finally, let's plot an image next to the sine function
    auto image_and_plot = figure({1, 2}, [] (const auto& axis_location) {
        return axis_location.col == 0
            ? default_style  // for images this is better than ggplot
            : style{.name = "ggplot"};
    });
    image_and_plot.axis({0, 0}).imshow(std::vector<std::vector<double>>{
        {1, 2, 3},
        {4, 5, 6},
        {7, 8, 9},
    }, no_kwargs, {.add_colorbar = true});
    image_and_plot.axis({0, 1}).plot(x_values, sine_values);

    // let's have a look at all the figures we created
    show();

    return 0;
}
