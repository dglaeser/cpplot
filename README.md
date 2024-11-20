<!--
SPDX-FileCopyrightText: 2024 Dennis Gläser <dennis.a.glaeser@gmail.com>
SPDX-License-Identifier: MIT
-->

`cpplot` is a small single-header C++-library that wraps Python's `matplotlib` for easy creation of plots.
To use it, you have several options:

- add this repository as git submodule to yours
- use `cmake`'s `FetchContent` to pull it when configuring your project
- install the library locally
- copy and include `cpplot.hpp` in your project

The library has no dependencies except Python itself, and `matplotlib` has to be in the Python path at runtime.
The library exposes the classes `figure` and `axis`, which wrap the respective `matplotlib` classes and expose
the most commonly used parts of their interfaces in C++. If you need to use functionality that is not exposed
in the C++ wrappers, you can also explicitly trigger invocations of the desired underlying Python function.
Here's a minimal full working example that shows both approaches (for more examples, see [example.cpp](examples/example.cpp)):

```cpp
#include <cpplot/cpplot.hpp>

int main() {
    using namespace cpplot;
    using namespace cpplot::literals;  // for convenient creation of keyword arguments

    figure fig;  // this creates a figure with a single axis to which we can add plots
    fig.axis().plot(std::vector{1, 2, 3}, kwargs("linestyle"_kw = "--", "color"_kw = "r"));

    // let's make a stair plot (currently not exposed in C++) by invoking python directly
    figure fig2;
    fig2.axis().py_invoke("stairs", args(std::vector{1, 2, 3}), kwargs("fill"_kw = true));

    // let's create two plots side-by-side and use a different style
    figure fig3{{.rows = 1, .cols = 2}, style{.name = "ggplot"}};
    fig3.axis_at({0, 0}).plot(std::vector{1, 2, 3});
    fig3.axis_at({0, 1}).plot(std::vector{1, 2, 3});
    fig3.axis_at({0, 0}).set_title("left plot");
    fig3.axis_at({0, 1}).set_title("right plot");
    fig3.set_title("figure title");

    // show all active figures (when the desctructor of `figure` is called, the python figure is closed)
    show();

    return 0;
}
```
