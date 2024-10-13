#include <boost/ut.hpp>

#include <cpplot/cpplot.hpp>

using namespace boost::ut;

int main() {

    // Todo: multiple instances seem to faile
    cpplot::Plot p{};

    "plot_values"_test = [&] () {
        expect(p.plot_to(
            p.figure().value(),
            std::vector{1.0, 2.0, 3.0},
            std::vector{3.0, 4.0, 5.0}
        ));
    };

    "plot_values_with_label"_test = [&] () {
        expect(p.plot_to(
            p.figure().value(),
            std::vector{1.0, 2.0, 3.0},
            std::vector{3.0, 4.0, 5.0},
            {.label = "label"}
        ));
    };

    "plot_values_with_color"_test = [&] () {
        expect(p.plot_to(
            p.figure().value(),
            std::vector{1.0, 2.0, 3.0},
            std::vector{3.0, 4.0, 5.0},
            {.color = "r"}
        ));
    };

    return 0;
}
