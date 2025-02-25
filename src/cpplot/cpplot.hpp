// SPDX-FileCopyrightText: 2024 Dennis Gläser <dennis.a.glaeser@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <exception>
#include <stdexcept>
#include <source_location>
#include <type_traits>
#include <functional>
#include <utility>
#include <algorithm>
#include <concepts>
#include <ranges>

#include <string_view>
#include <vector>


#ifdef CPPLOT_DISABLE_PYTHON_DEBUG_BUILD
    #ifdef _DEBUG
        #undef _DEBUG
        #include <Python.h>
        #define _DEBUG
    #else
        #include <Python.h>
    #endif
#else
    #include <Python.h>
#endif


namespace cpplot {

namespace traits {

//! Customization point to get the size of an image type
template<typename T> struct image_size;
//! Customization point to get a value in an image
template<typename T> struct image_access;
//! Customization point to get a coordinate value in a point
template<typename T, std::size_t direction> struct point_access;
//! Customization point for converting types into python objects
template<typename T> struct to_pyobject;

}  // namespace traits

namespace exceptions {

class exception : public std::exception {
 public:
    explicit exception(std::string_view what, std::source_location loc = std::source_location::current()) {
        _what = what;
        _what += "\n";
        _what += "\tFunction: " + std::string(loc.function_name()) + "\n";
        _what += "\tFile:     " + std::string(loc.file_name()) + "\n";
        _what += "\tLine:     " + std::to_string(loc.line()) + "\n";
    }

    const char* what() const noexcept override { return _what.data(); }

 private:
    std::string _what;
};

struct python_error : public exception { using exception::exception; };
struct size_error : public exception { using exception::exception; };

}  // namespace exceptions

//! Data structure to define a 2d grid
struct grid {
    std::size_t rows;
    std::size_t cols;
};

//! Data structure for accessing entries on a 2d grid
struct grid_location {
    std::size_t row;
    std::size_t col;
};

#ifndef DOXYGEN
namespace detail {

    template<typename T, std::size_t s = sizeof(T)>
    std::false_type is_incomplete(T*);
    std::true_type is_incomplete(...);
    template<typename T>
    inline constexpr bool is_complete = !decltype(is_incomplete(std::declval<T*>()))::value;

}  // namespace detail
#endif  // DOXYGEN

namespace concepts {

template<typename T>
concept scalar = std::floating_point<std::remove_cvref_t<T>> or std::integral<std::remove_cvref_t<T>>;

template<typename T>
concept range_1d = std::ranges::range<T> and not std::ranges::range<std::ranges::range_value_t<T>>;

template<typename T>
concept range_2d = std::ranges::range<T> and range_1d<std::ranges::range_value_t<T>>;

template<typename T>
concept point_2d = detail::is_complete<traits::point_access<T, 0>>
    and detail::is_complete<traits::point_access<T, 1>>
    and requires(const T& t) {
        { traits::point_access<T, 0>::get(t) } -> scalar;
        { traits::point_access<T, 1>::get(t) } -> scalar;
    };

template<typename T>
concept as_image = detail::is_complete<traits::image_size<T>>
    and detail::is_complete<traits::image_access<T>>
    and requires(const T& t) {
        { traits::image_size<T>::get(t) } -> std::convertible_to<grid>;
        { traits::image_access<T>::at(grid_location{0, 0}, t) } -> scalar;
    };

template<typename T>
concept image = range_2d<T> or as_image<T>;

template<typename T>
concept to_pyobject = detail::is_complete<traits::to_pyobject<T>>
    and requires(const T& t) {
        { traits::to_pyobject<T>::from(t) } -> std::convertible_to<PyObject*>;
    };

template<typename T>
concept kwarg = requires(const T& t) {
        { t.name } -> std::convertible_to<std::string>;
        { t.value };
    };

}  // namespace concepts

//! Data structure to collect arguments to be forwarded to python
template<typename... T>
struct py_args { std::tuple<T...> values; };

//! Data structure to collect keyword arguments to be forwarded to python
template<concepts::kwarg... K>
struct py_kwargs { std::tuple<K...> values; };

//! Instance of an empty args data structure
inline constexpr py_args<> no_args{};

//! Instance of an empty kwargs data structure
inline constexpr py_kwargs<> no_kwargs{};


//! Global error observer to customize behaviour when python errors are raised
static struct {
    using observer = std::function<void()>;

    //! Called by the library when an error occurs
    void notify() {
        _observer();
    }

    //! Replace the underlying observer by the given one
    observer swap_with(observer other) {
        observer tmp = _observer;
        _observer = other;
        return tmp;
    }

 private:
    observer _observer = [] () {
        PyErr_Print();
        throw exceptions::python_error("Python error occurred");
    };
} pyerror_observer;


#ifndef DOXYGEN
namespace detail {
    class python {
        explicit python() {
            Py_Initialize();
            if (!Py_IsInitialized())
                throw exceptions::python_error("Could not initialize Python.");
        };

     public:
        python(const python&) = delete;
        ~python() {
            if (Py_IsInitialized())
                Py_Finalize();
        }

        static const python& instance() {
            static python py{};
            return py;
        }
    };

    struct pycontext {
        pycontext() { python::instance(); }
    };

}  // namespace detail
#endif  // DOXYGEN

//! Wrapper around a PyObject*, i.e. the python object representation
class pyobject {
 public:
    ~pyobject() { if (_obj) Py_XDECREF(_obj); }

    explicit pyobject(PyObject* obj) : _obj{obj} {}
    pyobject(const pyobject& other) : pyobject{Py_XNewRef(other._obj)} {}
    pyobject(pyobject&& other) : pyobject{other.release()} {}
    pyobject() = default;

    pyobject& operator=(const pyobject& other) {
        pyobject{release()};
        _obj = other._obj;
        Py_XINCREF(_obj);
        return *this;
    }

    pyobject& operator=(pyobject&& other) {
        std::swap(_obj, other._obj);
        return *this;
    }

    static pyobject from(PyObject* obj) {
        if (!obj)
            pyerror_observer.notify();
        return pyobject{obj};
    }

    static pyobject none() {
        detail::pycontext{};
        return pyobject{Py_None};
    }

    PyObject* get() const noexcept { return _obj; }
    PyObject* release() noexcept { PyObject* tmp = _obj; _obj = nullptr; return tmp; }
    operator bool() const noexcept { return static_cast<bool>(_obj); }

 private:
    PyObject* _obj{nullptr};
    detail::pycontext _context;
};

//! Type to represent the absence of a value
struct none_t {};
inline constexpr none_t none;


#ifndef DOXYGEN
namespace detail {

    template<typename... Ts>
    struct overloads : Ts... { using Ts::operator()...; };
    template<typename... Ts> overloads(Ts...) -> overloads<Ts...>;

    template<typename T>
    pyobject to_pyobject(const T& t) {
        return pyobject::from(overloads{
            [] (none_t) { return Py_None; },
            [] (bool b) { return b ? Py_True : Py_False; },
            [] (std::integral auto i) { return PyLong_FromLong(static_cast<long>(i)); },
            [] (std::unsigned_integral auto i) { return PyLong_FromSize_t(static_cast<std::size_t>(i)); },
            [] (std::floating_point auto f) { return PyFloat_FromDouble(static_cast<double>(f)); },
            [] (const char* s) { return PyUnicode_FromString(s); },
            [] (const std::string& s) { return PyUnicode_FromString(s.c_str()); },
            [] (const std::wstring& s) { return PyUnicode_FromWideChar(s.data(), s.size()); },
            [] (const pyobject& p) { return pyobject{p}.release(); },
            [] <concepts::to_pyobject O> (const O& o) { return traits::to_pyobject<O>::from(o); }
        }(t));
    }

    template<typename... T>
    pyobject to_pytuple(T&&... t) {
        pycontext{};
        auto tuple = pyobject::from(PyTuple_New(sizeof...(T)));
        const auto push = [&] <std::size_t... is> (std::index_sequence<is...>) {
            (..., PyTuple_SetItem(tuple.get(), is, to_pyobject(t).release()));
        };
        push(std::index_sequence_for<T...>{});
        return tuple;
    }

    template<typename F, typename... O, concepts::kwarg K, concepts::kwarg... Ks>
    constexpr decltype(auto) invoke_interleaved(F&& f, std::tuple<O...>&& interleaved, const K& kwarg, Ks&&... kwargs) {
        auto extended = std::tuple_cat(std::move(interleaved), std::tuple{kwarg.name.c_str(), to_pyobject(kwarg.value).release()});
        if constexpr (sizeof...(Ks) == 0)
            return std::apply(f, std::move(extended));
        else
            return invoke_interleaved(std::forward<F>(f), std::move(extended), std::forward<Ks>(kwargs)...);
    }

    template<concepts::kwarg... K> requires(sizeof...(K) == 0)
    pyobject to_pydict(K&&... kwargs) {
        return pyobject{nullptr};
    }

    template<concepts::kwarg... K> requires(sizeof...(K) > 0)
    pyobject to_pydict(K&&... kwargs) {
        std::string format = "{";
        for (std::size_t i = 0; i < sizeof...(K); ++i)
            format += "s:O,";
        format.back() = '}';

        pycontext{};
        auto dict = invoke_interleaved([&] (const auto&... interleaved) {
            return pyobject::from(Py_BuildValue(format.c_str(), interleaved...));
        }, std::tuple{}, kwargs...);

        if (!dict)
            throw exceptions::python_error("Conversion to python dictionary failed.");
        return dict;
    }

    template<typename... A, concepts::kwarg... K>
    pyobject pycall(const pyobject& obj,
                    const std::string& function,
                    const py_args<A...>& args = py_args<>{},
                    const py_kwargs<K...>& kwargs = py_kwargs<>{}) {
        auto f = pyobject::from(PyObject_GetAttrString(obj.get(), function.c_str()));
        auto pyargs = std::apply([&] (const auto&... arg) { return to_pytuple(arg...); }, args.values);
        auto pykwargs = std::apply([&] (const auto&... kwarg) { return to_pydict(kwarg...); }, kwargs.values);
        if (!f || !pyargs)
           return pyobject{nullptr};
        return pyobject::from(PyObject_Call(f.get(), pyargs.get(), pykwargs.get()));
    }

    struct plt {
        pyobject pyplot;

        plt() : pyplot{} {
            pyplot = pyobject::from(PyImport_ImportModule("matplotlib.pyplot"));
            if (!pyplot)
                throw exceptions::python_error("Could not import matplotlib.pyplot.");
        }
    };

}  // namespace detail
#endif  // DOXYGEN

//! Data structure to represent a keyword argument
template<typename V>
struct kwarg {
    std::string name;
    V value;

    template<typename _V> requires(std::is_same_v<std::remove_cvref_t<V>, std::remove_cvref_t<_V>>)
    explicit constexpr kwarg(std::string n, _V&& val) noexcept
    : name{std::move(n)}
    , value{std::forward<_V>(val)}
    {}
};

template<typename V>
kwarg(std::string, V&&) -> kwarg<std::conditional_t<std::is_lvalue_reference_v<V>, V, std::remove_cvref_t<V>>>;

//! A named keyword argument with no value specified yet
template<>
struct kwarg<none_t> {
    std::string name;

    template<typename V>
    constexpr auto operator=(V&& value) && noexcept {
        return cpplot::kwarg{std::move(name), std::forward<V>(value)};
    }
};

//! Factory function to create arguments passed to python functions
template<typename... T>
inline constexpr auto args(T&&... t) {
    return py_args{std::forward_as_tuple(std::forward<T>(t)...)};
}

//! Factory to create keyword arguments
template<concepts::kwarg... T>
inline constexpr auto kwargs(T&&... t) {
    return py_kwargs{std::forward_as_tuple(std::forward<T>(t)...)};
}

//! Helper function to create a keyword argument
inline kwarg<none_t> kw(std::string name) noexcept {
    return {std::move(name)};
}

namespace literals {

//! Create a keyword argument from a string literal
kwarg<none_t> operator ""_kw(const char* chars, size_t size) noexcept {
    std::string n;
    n.resize(size);
    std::copy_n(chars, size, n.begin());
    return {std::move(n)};
}

}  // namespace literals

//! Invoke a function on the given python object (may be used for non-exposed pyplot features)
template<typename... A, typename... K>
pyobject py_invoke(const pyobject& obj,
                   const std::string& function,
                   const py_args<A...>& args = no_args,
                   const py_kwargs<K...>& kwargs = no_kwargs) {
    auto result = detail::pycall(obj, function, args, kwargs);
    if (!result)
        throw exceptions::python_error("Python function invocation unsuccessful.");
    return result;
}

//! Return the `matplotlib.pyplot` module
pyobject pyplot() {
    return detail::plt{}.pyplot;
}

//! Show all currently active figures
void show() {
    detail::plt plt{};
    detail::pycall(plt.pyplot, "show");
}

//! Options for `axis.imshow`
struct imshow_options {
    bool add_colorbar = false;
};

//! Options for `axis.bar`
struct bar_options {
    bool add_bar_labels = false;
};

//! forward declaration
class figure;

//! Wrapper around a matplotlib.pyplot.Axes
class axis {
 public:
    //! Plot the given values against indices on the x-axis
    template<std::ranges::sized_range Y, typename... K>
    pyobject plot(const Y& y, const py_kwargs<K...>& kwargs = no_kwargs) {
        const auto x = std::views::iota(std::size_t{0}, std::ranges::size(y));
        return plot(x, y, kwargs);
    }

    //! Plot the given y-values against the given x-values
    template<std::ranges::range X, std::ranges::range Y, typename... K>
    pyobject plot(const X& x, const Y& y, const py_kwargs<K...>& kwargs = no_kwargs) {
        return detail::pycall(_ax, "plot", args(x, y), kwargs);
    }

    //! Plot a histogram on this axis
    template<std::ranges::range X, typename... K>
    pyobject hist(const X& x, const py_kwargs<K...>& kwargs = no_kwargs) {
        return detail::pycall(_ax, "hist", args(x), kwargs);
    }

    //! Show the given image on this axis
    template<concepts::image I, typename... K>
    pyobject imshow(const I& img,
                    const py_kwargs<K...>& kwargs = no_kwargs,
                    const imshow_options& opts = {}) {
        auto image = detail::pycall(_ax, "imshow", args(img), kwargs);
        if (image && opts.add_colorbar)
            detail::pycall(detail::plt{}.pyplot, "colorbar", no_args, cpplot::kwargs(
                kw("mappable") = image,
                kw("ax") = _ax
            ));
        return image;
    }

    //! Add a scatter plot to this axis
    template<std::ranges::range X, std::ranges::range Y, typename... K>
    pyobject scatter(const X& x, const Y& y, const py_kwargs<K...>& kwargs = no_kwargs) {
        return detail::pycall(_ax, "scatter", args(x, y), kwargs);
    }

    //! Add a bar plot to this axis using the data point indices on the x-axis
    template<std::ranges::sized_range Y, typename... K>
    pyobject bar(const Y& y,
                 const py_kwargs<K...>& kwargs = no_kwargs,
                 const bar_options& opts = {}) {
        const auto x = std::views::iota(std::size_t{0}, std::ranges::size(y));
        return bar(x, y, kwargs, opts);
    }

    //! Add a bar plot to this axis
    template<std::ranges::range X, std::ranges::range Y, typename... K>
    pyobject bar(const X& x, const Y& y,
                 const py_kwargs<K...>& kwargs = no_kwargs,
                 const bar_options& opts = {}) {
        auto rectangles = detail::pycall(_ax, "bar", args(x, y), kwargs);
        if (rectangles && opts.add_bar_labels)
            detail::pycall(_ax, "bar_label", args(rectangles));
        return rectangles;
    }

    //! Draw a polygon by connecting the points in the given range and fill its interior
    template<std::ranges::forward_range R, typename... K>
        requires(concepts::point_2d<std::ranges::range_value_t<R>>)
    pyobject fill(const R& corners, const py_kwargs<K...>& kwargs = no_kwargs) {
        return detail::pycall(_ax, "fill", args(
            corners | std::views::transform([] <typename P> (const P& point) { return traits::point_access<P, 0>::get(point); }),
            corners | std::views::transform([] <typename P> (const P& point) { return traits::point_access<P, 1>::get(point); })
        ), kwargs);
    }

    //! Add a title to this axis
    pyobject set_title(const std::string& title) {
       return detail::pycall(_ax, "set_title", args(title));
    }

    //! Set the x-axis ticks
    template<std::ranges::range X, typename... K>
    pyobject set_x_ticks(const X& ticks, const py_kwargs<K...>& kwargs = py_kwargs<>{}) {
        return detail::pycall(_ax, "set_xticks", args(ticks), kwargs);
    }

    //! Set the y-axis ticks
    template<std::ranges::range Y, typename... K>
    pyobject set_y_ticks(const Y& ticks, const py_kwargs<K...>& kwargs = py_kwargs<>{}) {
        return detail::pycall(_ax, "set_yticks", args(ticks), kwargs);
    }

    //! Set the x-axis label
    pyobject set_x_label(const std::string& label) {
         return detail::pycall(_ax, "set_xlabel", args(label), py_kwargs<>{});
    }

    //! Set the y-axis label
    pyobject set_y_label(const std::string& label) {
         return detail::pycall(_ax, "set_ylabel", args(label), py_kwargs<>{});
    }

    //! Add a legend to this axis (invokes pyplot.Axes.legend(kwargs))
    template<typename... K>
    pyobject add_legend(const py_kwargs<K...>& kwargs = py_kwargs<>{}) {
         return detail::pycall(_ax, "legend", py_args<>{}, kwargs);
    }

    //! Get the python representation of this axis
    pyobject get_pyobject() const {
        return _ax;
    }

    //! Invoke a python function on the underlying axis
    template<typename... A, typename... K>
    pyobject py_invoke(const std::string& function,
                       const py_args<A...>& args = no_args,
                       const py_kwargs<K...>& kwargs = no_kwargs) {
        return cpplot::py_invoke(_ax, function, args, kwargs);
    }

 private:
    friend class figure;
    axis(pyobject ax) : _ax{ax} {}
    pyobject _ax;
};

//! Represents a pyplot style to use for a figure
struct style {
    std::string_view name;

    constexpr bool operator==(const style& other) const noexcept {
        return name == other.name;
    }
};

//! default style
inline constexpr style default_style{.name = "default"};

//! Wrapper around a matplotlib.pyplot.Figure
class figure : private detail::plt {
 public:
    ~figure() { close(); }

    //! Create a figure with a single axis using the given style
    figure(const style& style = default_style)
    : _id{_get_unused_id()}
    , _grid{1, 1} {
        _set_style(style);
        auto [fig, axes] = _make_fig_and_axes(kwargs(kw("num") = _id));
        _fig = fig;
        _axes.push_back(cpplot::axis{axes});
        _set_style(default_style);
    }

    //! Create a figure with a grid of axes and one style for all axes
    figure(grid grid, const style& style = default_style)
    : _id{_get_unused_id()}
    , _grid{std::move(grid)} {
        _set_style(style);
        _fig = detail::pycall(this->pyplot, "figure", no_args, kwargs(kw("num") = _id));
        std::size_t flat_index = 1;
        for (std::size_t row = 0; row < _grid.rows; ++row) {
            for (std::size_t col = 0; col < _grid.cols; ++col)
                _axes.push_back(cpplot::axis{
                    detail::pycall(_fig, "add_subplot", args(_grid.rows, _grid.cols, flat_index++))
                });
        }
        _set_style(default_style);
    }

    //! Create a figure with a grid of axes and use an individual style on each axis
    template<std::invocable<const grid_location&> F>
        requires(std::convertible_to<std::invoke_result_t<F, const grid_location&>, style>)
    figure(grid grid, F&& style_callback)
    : _id{_get_unused_id()}
    , _grid{std::move(grid)} {
        _fig = detail::pycall(this->pyplot, "figure", no_args, kwargs(kw("num") = _id));
        std::size_t flat_index = 1;
        for (std::size_t row = 0; row < _grid.rows; ++row) {
            for (std::size_t col = 0; col < _grid.cols; ++col) {
                _set_style(style_callback(grid_location{.row = row, .col = col}));
                _axes.push_back(cpplot::axis{
                    detail::pycall(_fig, "add_subplot", args(_grid.rows, _grid.cols, flat_index++))
                });
            }
        }
        _set_style(default_style);
    }

    //! Return the underlying axis (for figures with a single axis)
    cpplot::axis axis() const {
        if (_axes.size() > 1)
            throw exceptions::size_error("Figure contains more than one axis. Call axis(const grid_location&) instead.");
        return _axes.at(0);
    }

    //! Return the axis at the specified position
    cpplot::axis axis_at(const grid_location& location) const {
        if (location.row >= _grid.rows) throw exceptions::size_error("Row index out of bounds");
        if (location.col >= _grid.cols) throw exceptions::size_error("Column index out of bounds");
        return _axes.at(location.row*_grid.cols + location.col);
    }

    //! Add a title to this figure
    pyobject set_title(const std::string& title) {
        return detail::pycall(_fig, "suptitle", args(title));
    }

    //! Save this figure to the file with the given name
    void save_to(const std::string& filename) const {
        detail::pycall(_fig, "savefig", args(filename), kwargs(kw("bbox_inches") = "tight"));
    }

    //! Close this figure
    void close() {
        detail::pycall(this->pyplot, "close", args(_id));
    }

    //! Return the number of axis rows in this figure
    std::size_t rows() const {
        return _grid.rows;
    }

    //! Return the number of axis columns in this figure
    std::size_t cols() const {
        return _grid.cols;
    }

    //! Get the python representation of this figure
    pyobject get_pyobject() const {
        return _fig;
    }

    //! Invoke a python function on the underlying figure
    template<typename... A, typename... K>
    pyobject py_invoke(const std::string& function,
                       const py_args<A...>& args = no_args,
                       const py_kwargs<K...>& kwargs = no_kwargs) {
        return cpplot::py_invoke(_fig, function, args, kwargs);
    }

 private:
    //! Set the style to use (calls pyplot.style.use(style))
    void _set_style(const style& style) {
        auto style_attr = pyobject::from(PyObject_GetAttrString(this->pyplot.get(), "style"));
        if (style_attr)
            detail::pycall(style_attr, "use", args(std::string{style.name}));
        else if (style != default_style)
            throw exceptions::python_error("Could not access pyplot.style attribute for setting the requested style.");
    }

    std::size_t _get_unused_id() const {
        std::size_t id = 0;
        while (detail::pycall(this->pyplot, "fignum_exists", args(id)).get() == Py_True)
            ++id;
        return id;
    }

    template<typename... K>
    std::pair<pyobject, pyobject> _make_fig_and_axes(const py_kwargs<K...>& kwargs) const {
        auto fig_ax_tuple = detail::pycall(this->pyplot, "subplots", no_args, kwargs);
        if (!fig_ax_tuple)
            throw exceptions::python_error("Could not create figure.");
        if (!PySequence_Check(fig_ax_tuple.get()))
            throw exceptions::python_error("Unexpected value returned from pyplot.subplots");
        if (PySequence_Size(fig_ax_tuple.get()) != 2)
            throw exceptions::python_error("Unexpected value returned from pyplot.subplots");
        return {
            pyobject{PySequence_GetItem(fig_ax_tuple.get(), 0)},
            pyobject{PySequence_GetItem(fig_ax_tuple.get(), 1)}
        };
    }

    std::size_t _id;
    grid _grid;
    pyobject _fig;
    std::vector<cpplot::axis> _axes;
};


// default trait implementations
namespace traits {

template<std::ranges::range R>
struct to_pyobject<R> {
    static PyObject* from(const R& range) {
        detail::pycontext{};
        auto list = PyList_New(std::ranges::size(range));
        std::ranges::for_each(range, [&, i=0] (const auto& value) mutable {
            PyList_SetItem(list, i++, detail::to_pyobject(value).release());
        });
        return list;
    }
};

template<concepts::as_image T>
    requires(!concepts::range_2d<T>)  // because in that case the range specialization is taken
struct to_pyobject<T> {
    static PyObject* from(const T& img) {
        detail::pycontext{};
        const auto grid = image_size<T>::get(img);
        auto py_image = PyList_New(grid.rows);
        for (std::size_t row = 0; row < grid.rows; ++row) {
            auto py_row = PyList_New(grid.cols);
            for (std::size_t col = 0; col < grid.cols; ++col)
                PyList_SetItem(py_row, col, detail::to_pyobject(image_access<T>::at(
                    grid_location{.row = row, .col = col},
                    img
                )).release());
            PyList_SetItem(py_image, row, py_row);
        }
        return py_image;
    }
};

// specialize the point trait for ranges with static size (e.g. std::array)
template<concepts::range_1d R, std::size_t dimension> requires(R{}.size() == 2)
struct point_access<R, dimension> {
    static_assert(dimension < 2);
    static decltype(auto) get(const R& range) {
        auto it = std::ranges::begin(range);
        std::ranges::advance(it, dimension);
        return *it;
    }
};

}  // namespace traits

}  // namespace cpplot
