<!--
SPDX-FileCopyrightText: 2024 Dennis Gläser <dennis.a.glaeser@gmail.com>
SPDX-License-Identifier: MIT
-->

`cpplot` is a small single-header C++-library that wraps Python's `matplotlib` for easy creation of plots.
To use it, simply

- add this repository as git submodule to yours
- use `cmake`'s `FetchContent` to pull it when configuring your project
- copy and include `cpplot.hpp` in your project

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
    fig.axis().plot(std::views::iota(0, 100), kwargs("linestyle"_kw = "--", "color"_kw = "r"));

    // let's make a stair plot, which currently is not exposed in C++, by invoking python directly
    figure fig2;
    fig2.axis().py_invoke("stairs", args(std::vector{1, 2, 3}), kwargs("fill"_kw = true));

    // show all active figures (when the desctructor of `figure` is called, the python figure is closed)
    show();

    return 0;
}
```
