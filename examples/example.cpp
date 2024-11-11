#include <numbers>
#include <ranges>
#include <cmath>

#include <cpplot/cpplot.hpp>

int main() {
    using namespace cpplot;
    using namespace cpplot::literals;

    // let's use a cool style for the upcoming figures
    set_style("ggplot");

    const auto x_values = std::views::iota(0, 100) | std::views::transform([] (const std::size_t i) {
        return std::numbers::pi*2.0*static_cast<double>(i)/99.0;
    });
    const auto sine_values = x_values | std::views::transform([] (const double x) { return std::sin(x); });

    // let's make a single figure and plot a sine function, simply over the indices
    auto sine_default_x_axis = figure();
    sine_default_x_axis.plot(sine_values, with("label"_kw = "sine"));
    sine_default_x_axis.set_title("The sine function");
    sine_default_x_axis.add_legend();

    // let's do the same, but use the actual x values on the x axis
    auto sine = figure();
    sine.plot(x_values, sine_values, with("label"_kw = "sin(x)"));
    sine.add_legend();

    // let's plot the two below each other
    auto stacked = figure({.nrows = 2, .ncols = 1});
    stacked.at({.row = 0, .col = 0}).plot(sine_values);
    stacked.at({.row = 1, .col = 0}).plot(x_values, sine_values);

    // ... or side-by-side (also, let's add titles this time)
    auto side_by_side = figure({1, 2});
    side_by_side.at({0, 0}).plot(sine_values);
    side_by_side.at({0, 1}).plot(x_values, sine_values);
    side_by_side.at({0, 0}).set_title("sine");
    side_by_side.at({0, 1}).set_title("sin(x)");
    side_by_side.set_title("2 sine plots");

    // finally, let's plot an image next to the sine function (for images, the default style is better)
    set_style("default");
    auto image_and_plot = figure({1, 2});
    image_and_plot.at({0, 0}).set_image(std::vector<std::vector<double>>{
        {1, 2, 3},
        {4, 5, 6},
        {7, 8, 9}
    });
    image_and_plot.at({0, 0}).add_colorbar();
    image_and_plot.at({0, 1}).plot(x_values, sine_values);

    // let's have a look at all the figures we created
    // show_all_figures();

    return 0;
}
