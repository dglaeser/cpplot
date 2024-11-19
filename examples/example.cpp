#include <numbers>
#include <ranges>
#include <cmath>
#include <iostream>

#include <cpplot/cpplot.hpp>

int main() {
    using namespace cpplot;
    using namespace cpplot::literals;

    const auto x_values = std::views::iota(0, 100) | std::views::transform([] (const std::size_t i) -> double {
        return std::numbers::pi*2.0*static_cast<double>(i)/99.0;
    });
    const auto sine_values = x_values | std::views::transform([] (const double x) { return std::sin(x); });

    // let's make a single figure and plot a sine function (using the data point indices on the x-axis)
    figure sine_default_x_axis;
    sine_default_x_axis.axis().plot(sine_values, kwargs("label"_kw = "sine"));
    sine_default_x_axis.axis().add_legend();
    sine_default_x_axis.set_title("The sine function");

    // let's do the same, but use the actual x values on the x axis and use a cool style
    figure sine{style{.name = "ggplot"}};
    sine.axis().plot(x_values, sine_values, kwargs("label"_kw = "sin(x)"));
    sine.axis().add_legend();

    // let's plot the two below each other
    figure stacked{{.rows = 2, .cols = 1}};
    stacked.axis_at({.row = 0, .col = 0}).plot(sine_values);
    stacked.axis_at({.row = 1, .col = 0}).plot(x_values, sine_values);

    // ... or side-by-side (also, let's add axis titles this time)
    figure side_by_side{{.rows = 1, .cols = 2}};
    side_by_side.axis_at({0, 0}).plot(sine_values);
    side_by_side.axis_at({0, 1}).plot(x_values, sine_values);
    side_by_side.axis_at({0, 0}).set_title("sine");
    side_by_side.axis_at({0, 1}).set_title("sin(x)");
    side_by_side.set_title("2 sine plots");

    // finally, let's plot an image next to the sine function and use a different style for the plot & image
    figure image_and_plot{{1, 2}, [] (const grid_location& loc) -> style {
        return loc.col == 0
            ? default_style  // for images this is better than ggplot
            : style{.name = "ggplot"};
    }};
    // Images can be 2d ranges, which are supported out-of-the-box. If you have a custom image type, you may
    // specialize the `image_size` and `image_access` traits defined in the namespace `cpplot::traits`
    const std::vector<std::vector<double>> image{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    // ... with imshow, we can directly add a colorbar to the axis, and via the kwargs we can set the color map
    image_and_plot.axis_at({0, 0}).imshow(image, no_kwargs, {.add_colorbar = true});
    image_and_plot.axis_at({0, 1}).plot(x_values, sine_values);

    // If you need some feature that is not exposed via the libary, you can also let it invoke python functions
    image_and_plot.py_invoke("text", no_args, kwargs(
        "x"_kw = 0.5,
        "y"_kw = 0.5,
        "s"_kw = "this is text"
    ));  // ... adds text over the figure
    image_and_plot.axis_at({0, 1}).py_invoke(
        "fill",
        args(
            std::vector{0.0, 1.0, 1.0, 0.0},  // x-coordinates
            std::vector{0.0, 0.0, 1.0, 1.0}   // y-coordinates
        ),
        kwargs(
            "edgecolor"_kw = "k",
            "fill"_kw = false
        )
    );  // ... draws a polygon, as specified by the coordinates, over this axis

    // let's have a look at all the figures we created
    // show();

    return 0;
}
