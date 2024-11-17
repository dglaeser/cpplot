#include <stdexcept>
#include <type_traits>
#include <algorithm>
#include <utility>
#include <optional>
#include <vector>
#include <string_view>
#include <concepts>
#include <ranges>


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
template<typename T>
struct ImageSize;

//! Customization point to get a value in an image
template<typename T>
struct ImageAccess;

template<typename T>
struct ImageSize<std::vector<std::vector<T>>> {
    static std::array<std::size_t, 2> get(const std::vector<std::vector<T>>& data) {
        const std::size_t y = data.size();
        if (y == 0)
            return {0, 0};
        const std::size_t x = data[0].size();
        assert(std::all_of(data.begin(), data.end(), [&] (const auto& row) { return row.size() == x; }));
        return {y, x};
    }
};

template<typename T>
struct ImageAccess<std::vector<std::vector<T>>> {
    static const T& at(const std::array<std::size_t, 2>& idx, const std::vector<std::vector<T>>& data) {
        return data.at(idx[0]).at(idx[1]);
    }
};

}  // namespace traits


#ifndef DOXYGEN
namespace detail {

    template<typename T, std::size_t s = sizeof(T)>
    std::false_type is_incomplete(T*);
    std::true_type is_incomplete(...);
    template<typename T>
    inline constexpr bool is_complete = !decltype(is_incomplete(std::declval<T*>()))::value;

    template<typename T>
    using ImageValueType = std::remove_cvref_t<decltype(traits::ImageAccess<T>::at(std::array<std::size_t, 2>{}, std::declval<const T>()))>;

}  // namespace detail
#endif  // DOXYGEN

template<typename T>
concept Image
= detail::is_complete<traits::ImageSize<T>>
and detail::is_complete<traits::ImageAccess<T>>
and requires(const T& t) {
    { traits::ImageSize<T>::get(t) } -> std::same_as<std::array<std::size_t, 2>>;
    { traits::ImageAccess<T>::at(std::array<std::size_t, 2>{}, t) };
    std::floating_point<detail::ImageValueType<T>> or std::integral<detail::ImageValueType<T>>;
};


#ifndef DOXYGEN
namespace detail {

    class Python {
        explicit Python() {
            Py_Initialize();
            if (!Py_IsInitialized())
                throw std::runtime_error("Could not initialize Python.");
        };

     public:
        ~Python() {
            if (Py_IsInitialized())
                Py_Finalize();
        }

        static const Python& instance() {
            static Python wrapper{};
            return wrapper;
        }

        template<typename F>
        decltype(auto) operator()(F&& f) const {
            if (!Py_IsInitialized())
                throw std::runtime_error("Python is not initialized.");
            return f();
        }
    };

    class PyObjectWrapper {
        void swap_pyobjects(PyObject*& a, PyObject*& b) {
            PyObject* c = a;
            a = b;
            b = c;
        }

    public:
        ~PyObjectWrapper() {
            if (_p)
                Python::instance()([&] () { Py_DECREF(_p); });
        }

        PyObjectWrapper() = default;
        PyObjectWrapper(PyObject* p) : _p{p} {}
        PyObjectWrapper(const PyObjectWrapper& other) : PyObjectWrapper{Py_XNewRef(other._p)} {}
        PyObjectWrapper(PyObjectWrapper&& other) : PyObjectWrapper{nullptr} { swap_pyobjects(_p, other._p); }

        PyObjectWrapper& operator=(const PyObjectWrapper& other) {
            *this = PyObjectWrapper{other};
            return *this;
        }

        PyObjectWrapper& operator=(PyObjectWrapper&& other) {
            swap_pyobjects(_p, other._p);
            return *this;
        }

        PyObject* release() {
            PyObject* tmp = _p;
            _p = nullptr;
            return tmp;
        }

        PyObject* get() { return _p; }
        PyObject* get() const { return _p; }

        explicit operator PyObject*() const { return _p; }
        operator bool() const { return static_cast<bool>(_p); }

    private:
        PyObject* _p{nullptr};
    };

    inline constexpr struct {
        template<std::invocable F> requires(std::is_same_v<std::invoke_result_t<F>, PyObject*>)
        PyObjectWrapper operator()(F&& f) const {
            PyObject* obj = Python::instance()(f);
            if (!obj)
                PyErr_Print();
            return {obj};
        }

        template<std::invocable F> requires(!std::is_same_v<std::invoke_result_t<F>, PyObject*>)
        decltype(auto) operator()(F&& f) const {
            return Python::instance()(f);
        }
    } pycall;

    template<typename T>
    struct AsPyObject;

    template<typename T>
    concept ConvertibleToPyObject = is_complete<AsPyObject<T>> and requires(const T& t) {
        { AsPyObject<T>::from(t) } -> std::same_as<PyObjectWrapper>;
    };

    template<typename... Ts>
    struct OverloadSet : Ts... { using Ts::operator()...; };
    template<typename... Ts> OverloadSet(Ts...) -> OverloadSet<Ts...>;

    template<typename T>
    PyObjectWrapper as_pyobject(const T& t) {
        return OverloadSet{
            [] (bool b) { return b ? Py_True : Py_False; },
            [] (std::integral auto i) { return PyLong_FromLong(static_cast<long>(i)); },
            [] (std::unsigned_integral auto i) { return PyLong_FromSize_t(static_cast<std::size_t>(i)); },
            [] (std::floating_point auto f) { return PyFloat_FromDouble(static_cast<double>(f)); },
            [] (const char* s) { return PyUnicode_FromString(s); },
            [] (const std::string& s) { return PyUnicode_FromString(s.c_str()); },
            [] (const std::wstring& s) { return PyUnicode_FromWideChar(s.data(), s.size()); },
            [] (const PyObjectWrapper& p) { return p; },
            [] <ConvertibleToPyObject O> (const O& o) { return AsPyObject<O>::from(o); }
        }(t);
    }

    template<typename... T>
    PyObjectWrapper as_pytuple(T&&... t) {
        auto tuple = pycall([&] () { return PyTuple_New(sizeof...(T)); });
        const auto push = [&] <std::size_t... is> (std::index_sequence<is...>) {
            (..., PyTuple_SetItem(tuple.get(), is, as_pyobject(t).release()));
        };
        push(std::index_sequence_for<T...>{});
        return tuple;
    }

    template<std::ranges::range R> requires(!Image<R>)
    struct AsPyObject<R> {
        static PyObjectWrapper from(const R& r) {
            return pycall([&] () {
                auto list = PyList_New(std::size(r));
                std::ranges::for_each(r, [&, i=0] <typename V> (const V& value) mutable {
                    PyList_SetItem(list, i++, as_pyobject(value).release());
                });
                return list;
            });
        }
    };

    template<Image T>
    struct AsPyObject<T> {
        static PyObjectWrapper from(const T& image) {
            const auto size = traits::ImageSize<T>::get(image);
            auto outer_list = pycall([&] () { return PyList_New(size[0]); });
            if (!outer_list)
                return nullptr;

            // PyList_Set_Item "steals" a reference, so we don't have to decrement
            for (std::size_t y = 0; y < size[0]; ++y) {
                auto row = pycall([&] () { return PyList_New(size[1]); });
                for (std::size_t x = 0; x < size[1]; ++x)
                    pycall([&] () { PyList_SET_ITEM(row, x, as_pyobject(traits::ImageAccess<T>::at({y, x}, image)).release()); });
                PyList_SET_ITEM(outer_list.get(), y, row.release());
            }

            return outer_list;
        }
    };

}  // namespace detail
#endif  // DOXYGEN


//! Data structure to represent a keyword argument
template<typename Value>
class Kwarg {
 public:
    using ValueType = std::remove_cvref_t<Value>;

    Kwarg(const Kwarg&) = delete;
    Kwarg(Kwarg&&) = default;

    template<typename V>
    explicit constexpr Kwarg(std::string name, V&& value) noexcept
    : _name{std::move(name)}
    , _value{std::forward<V>(value)} {
        static_assert(std::is_same_v<std::remove_cvref_t<Value>, std::remove_cvref_t<V>>);
    }

    const std::string& key() const { return _name; }
    const ValueType& value() const { return _value; }

 private:
    std::string _name;
    Value _value;
};

template<typename V>
Kwarg(std::string, V&&) -> Kwarg<std::conditional_t<std::is_lvalue_reference_v<V>, V, std::remove_cvref_t<V>>>;

//! Helper class to create keyword arguments
class KwargFactory {
 public:
    explicit KwargFactory(std::string name)
    : _name{std::move(name)}
    {}

    template<typename T>
    auto operator=(T&& value) && {
        return Kwarg<T>{std::move(_name), std::forward<T>(value)};
    }

 private:
    std::string _name;
};

//! Helper function to create keyword arguments
inline KwargFactory kw(std::string name) noexcept {
    return KwargFactory{std::move(name)};
}


namespace literals {

//! Create a keyword argument from a string literal
KwargFactory operator ""_kw(const char* chars, size_t size) {
    std::string n;
    n.resize(size);
    std::copy_n(chars, size, n.begin());
    return KwargFactory{n};
}

}  // namespace literals


#ifndef DOXYGEN
namespace detail {

    template<typename T>
    struct IsKwarg : std::false_type {};
    template<typename V>
    struct IsKwarg<Kwarg<V>> : std::true_type {};

}  // namespace detail
#endif  // DOXYGEN

//! Data structure to represent a set of keyword arguments
template<typename... T>
class Kwargs {
    static_assert(std::conjunction_v<detail::IsKwarg<T>...>);

 public:
    constexpr Kwargs(T&&... kwargs) noexcept
    : _kwargs{std::move(kwargs)...}
    {}

    bool has_key(const std::string& key) const {
        return std::apply([&] <typename... _T> (_T&&... kwarg) {
            return (... || (kwarg.key() == key));
        }, _kwargs);
    }

    template<typename Action>
    void apply(Action&& a) const {
        std::apply(a, _kwargs);
    }

 private:
    std::tuple<T...> _kwargs;
};

template<typename... T>
Kwargs(T&&...) -> Kwargs<std::remove_cvref_t<T>...>;

inline constexpr Kwargs<> no_kwargs;

//! Factory function to create a Kwargs object. Allows to write `f(arg, with("kwarg"_kw = 1.0))`
template<typename... T>
constexpr auto with(T&&... args) {
    return Kwargs{std::forward<T>(args)...};
}


#ifndef DOXYGEN
namespace detail {

    template<typename... T>
    struct AsPyObject<Kwargs<T...>> {
     public:
        static PyObjectWrapper from(const Kwargs<T...>&) requires(sizeof...(T) == 0) {
            if constexpr (sizeof...(T) == 0)
                return nullptr;
        }

        static PyObjectWrapper from(const Kwargs<T...>& kwargs) requires(sizeof...(T) > 0) {
            PyObject* result = nullptr;
            kwargs.apply([&] <typename... KW> (const KW&... kwarg) {
                std::string format = "{";
                for ([[maybe_unused]] std::size_t i = 0; i < sizeof...(T); ++i)
                    format += "s:O,";
                format.back() = '}';

                const auto as_buildvalue_arg = OverloadSet{
                    [] (const std::string& key) { return key.c_str(); },
                    [] (const PyObjectWrapper& o) { return o.get(); }
                };

                result = std::apply([&] (const auto&... interleaved) {
                    return pycall([&] () { return Py_BuildValue(format.c_str(), as_buildvalue_arg(interleaved)...); }).release();
                }, _make_interleaved(kwarg...));
            });

            if (!result)
                throw std::runtime_error("Conversion of Kwargs to PyObject failed.");
            return result;
        }

     private:
        template<typename... O, typename KW, typename... KWs>
        static constexpr auto _make_interleaved_impl(std::tuple<O...>&& out, const KW& kwarg, KWs&&... kwargs) {
            if constexpr (sizeof...(KWs) == 0)
                return std::tuple_cat(std::move(out), std::tuple{kwarg.key(), as_pyobject(kwarg.value())});
            else
                return _make_interleaved_impl(
                    std::tuple_cat(std::move(out), std::tuple{kwarg.key(), as_pyobject(kwarg.value())}),
                    std::forward<KWs>(kwargs)...
                );
        }

        template<typename... KWs>
        static constexpr auto _make_interleaved(KWs&&... kwargs) {
            return _make_interleaved_impl(std::tuple{}, std::forward<KWs>(kwargs)...);
        }
    };



    struct Args {
        template<typename... T>
        static auto from(T&&... t) { return std::forward_as_tuple(std::forward<T>(t)...); }
    };

    inline constexpr std::tuple<> no_args;

    template<typename... A, typename... KW>
    PyObjectWrapper invoke(const PyObjectWrapper obj,
                           const std::string& function_name,
                           const std::tuple<A...>& args = no_args,
                           const Kwargs<KW...>& kwargs = no_kwargs) {
        auto f = pycall([&] () { return PyObject_GetAttrString(obj.get(), std::string{function_name}.c_str()); });
        auto pyargs = std::apply([&] <typename... T> (const T&... arg) { return as_pytuple(arg...); }, args);
        auto pykwargs = as_pyobject(kwargs);
        if (!f || !pyargs)
           return nullptr;
        return pycall([&] () { return PyObject_Call(f.get(), pyargs.get(), pykwargs.get()); });
    }

    // forward declarations
    class PyPlot;

}  // namespace detail
#endif  // DOXYGEN

// forward declaration
class Figure;

//! Class to represent an axis for plotting lines, images, histograms, etc..
class Axis {
 public:
    //! Add a title to this axis
    bool set_title(const std::string& title) {
        return detail::invoke(_axis, "set_title", detail::Args::from(title));
    }

    //! Add a line plot to this axis using the data point indices as x-axis
    template<std::ranges::sized_range Y, typename... T>
    bool plot(Y&& y, const Kwargs<T...>& kwargs = no_kwargs) {
        return plot(
            std::views::iota(std::size_t{0}, std::ranges::size(y)),
            std::forward<Y>(y),
            kwargs
        );
    }

    //! Add a line plot to this axis
    template<std::ranges::range X, std::ranges::range Y, typename... T>
    bool plot(X&& x, Y&& y, const Kwargs<T...>& kwargs = no_kwargs) {
        return detail::invoke(_axis, "plot", detail::Args::from(x, y), kwargs);
    }

     //! Add a bar plot to this axis using the data point indices on the x-axis
    template<std::ranges::sized_range Y, typename... T>
    bool bar(Y&& y, const Kwargs<T...>& kwargs = no_kwargs) {
        return bar(
            std::views::iota(std::size_t{0}, std::ranges::size(y)),
            std::forward<Y>(y),
            kwargs
        );
    }

    //! Add a bar plot to this axis
    template<std::ranges::range X, std::ranges::range Y, typename... T>
    bool bar(X&& x, Y&& y, const Kwargs<T...>& kwargs = no_kwargs) {
        detail::PyObjectWrapper rectangles = detail::invoke(_axis, "bar", detail::Args::from(x, y), kwargs);
        if (!rectangles)
            return false;
        if (kwargs.has_key("label"))
            if (!detail::invoke(_axis, "bar_label", detail::Args::from(rectangles)))
                return false;
        return true;
    }

    //! Plot an image on this axis
    template<Image I>
    bool set_image(const I& image) {
        _image = detail::invoke(_axis, "imshow", detail::Args::from(image));
        return static_cast<bool>(_image);
    }

    //! Add a colorbar to a previously added image (throws if no image had been set)
    bool add_colorbar() {
        if (!_image.has_value())
            throw std::runtime_error("Cannot add colorbar; no image has been set");

        using namespace literals;
        return detail::invoke(_pyplot, "colorbar", detail::no_args, with(
            "mappable"_kw = *_image,
            "ax"_kw = _axis
        ));
    }

    //! Set the label to be displayed on the x axis
    bool set_x_label(const std::string& label) {
        return detail::invoke(_axis, "set_xlabel", detail::Args::from(label));
    }

    //! Set the label to be displayed on the y axis
    bool set_y_label(const std::string& label) {
        return detail::invoke(_axis, "set_ylabel", detail::Args::from(label));
    }

    //! Set the x-axis ticks
    template<std::ranges::range X, typename... T>
    bool set_x_ticks(X&& x, const Kwargs<T...>& kwargs = no_kwargs) {
        return detail::invoke(_axis, "set_xticks", detail::Args::from(x), kwargs);
    }

    //! Set the y-axis ticks
    template<std::ranges::range Y, typename... T>
    bool set_y_ticks(Y&& y, const Kwargs<T...>& kwargs = no_kwargs) {
        return detail::invoke(_axis, "set_yticks", detail::Args::from(y), kwargs);
    }

    //! Add a legend to this axis
    template<typename... T>
    bool add_legend(const Kwargs<T...>& kwargs = no_kwargs) {
        return detail::invoke(_axis, "legend", detail::no_args, kwargs);
    }

 private:
    friend class Figure;
    using PyObjectWrapper = detail::PyObjectWrapper;

    explicit Axis(PyObjectWrapper plt, PyObjectWrapper axis)
    : _pyplot{std::move(plt)}
    , _axis{std::move(axis)} {
        assert(_pyplot);
        assert(_axis);
    }

    PyObjectWrapper _pyplot;
    PyObjectWrapper _axis;
    std::optional<PyObjectWrapper> _image;
};


//! Class to represent a figure, i.e. a canvas containing one or more axes
class Figure {
    struct Grid {
        std::size_t ny;
        std::size_t nx;

        std::size_t count() const {
            return ny*nx;
        }
    };

 public:
    struct AxisLocation {
        std::size_t row;
        std::size_t col;
    };

    ~Figure() { close(); }

    //! Return the number of axis rows
    std::size_t ny() const {
        return _grid.ny;
    }

    //! Return the number of axis columns
    std::size_t nx() const {
        return _grid.nx;
    }

    //! Add a title to this figure
    bool set_title(const std::string& title) {
        return detail::invoke(_fig, "suptitle", detail::Args::from(title));
    }

    //! Return the axis at the given row and column
    Axis& at(const AxisLocation& loc) {
        if (loc.row >= _grid.ny) throw std::runtime_error("row index out of bounds.");
        if (loc.col >= _grid.nx) throw std::runtime_error("column index out of bounds.");
        return _axes.at(loc.row*_grid.nx + loc.col);
    }

    //! Convenience function for figures with a single axis (throws if multiple axes are defined)
    template<typename... Args>
    bool plot(Args&&... args) {
        if (_grid.count() > 1)
            throw std::runtime_error("Figure has multiple axes, retrieve the desired axis and use its plot function.");
        return _axes[0].plot(std::forward<Args>(args)...);
    }

    //! Convenience function for figures with a single axis (throws if multiple axes are defined)
    template<typename... Args>
    bool bar(Args&&... args) {
        if (_grid.count() > 1)
            throw std::runtime_error("Figure has multiple axes, retrieve the desired axis and use its plot function.");
        return _axes[0].bar(std::forward<Args>(args)...);
    }

    //! Convenience function for figures with a single axis (throws if multiple axes are defined)
    template<typename... Args>
    bool set_image(Args&&... args) {
        if (_grid.count() > 1)
            throw std::runtime_error("Figure has multiple axes, retrieve the desired axis and use its set_image function.");
        return _axes[0].set_image(std::forward<Args>(args)...);
    }

    //! Convenience function for figures with a single axis (throws if multiple axes are defined)
    template<typename... Args>
    bool set_x_label(Args&&... args) {
        if (_grid.count() > 1)
            throw std::runtime_error("Figure has multiple axes, retrieve the desired axis and use its set_image function.");
        return _axes[0].set_x_label(std::forward<Args>(args)...);
    }

    //! Convenience function for figures with a single axis (throws if multiple axes are defined)
    template<typename... Args>
    bool set_y_label(Args&&... args) {
        if (_grid.count() > 1)
            throw std::runtime_error("Figure has multiple axes, retrieve the desired axis and use its set_image function.");
        return _axes[0].set_y_label(std::forward<Args>(args)...);
    }

    //! Convenience function for figures with a single axis (throws if multiple axes are defined)
    template<typename... Args>
    bool set_x_ticks(Args&&... args) {
        if (_grid.count() > 1)
            throw std::runtime_error("Figure has multiple axes, retrieve the desired axis and use its set_image function.");
        return _axes[0].set_x_ticks(std::forward<Args>(args)...);
    }

    //! Convenience function for figures with a single axis (throws if multiple axes are defined)
    template<typename... Args>
    bool set_y_ticks(Args&&... args) {
        if (_grid.count() > 1)
            throw std::runtime_error("Figure has multiple axes, retrieve the desired axis and use its set_image function.");
        return _axes[0].set_y_ticks(std::forward<Args>(args)...);
    }

    //! Convenience function for figures with a single axis (throws if multiple axes are defined)
    template<typename... Args>
    bool add_colorbar(Args&&... args) {
        if (_grid.count() > 1)
            throw std::runtime_error("Figure has multiple axes, retrieve the desired axis and use its set_image function.");
        return _axes[0].add_colorbar(std::forward<Args>(args)...);
    }

    //! Convenience function for figures with a single axis (throws if multiple axes are defined)
    template<typename... T>
    bool add_legend(const Kwargs<T...>& kwargs = no_kwargs) {
        if (_grid.count() > 1)
            throw std::runtime_error("Figure has multiple axes, retrieve the desired axis and use its set_image function.");
        return _axes[0].add_legend(kwargs);
    }

    //! Adjust the layout of how the axes are arranged (see the [Matplotlib docu] (https://matplotlib.org/stable/api/_as_gen/matplotlib.figure.Figure.subplots_adjust.html) for the supported kwargs)
    template<typename... T>
    bool adjust_layout(const Kwargs<T...>& kwargs) {
        return detail::invoke(_fig, "subplots_adjust", detail::no_args, kwargs);
    }

    //! Save this figure to the given path
    bool save_to(const std::string& path) {
        using namespace literals;
        return detail::invoke(_fig, "savefig", detail::Args::from(path), with(
            "bbox_inches"_kw = "tight"
        ));
    }

    //! Close this figure
    bool close() {
        return detail::invoke(_pyplot, "close", detail::Args::from(_id));
    }

 private:
    using PyObjectWrapper = detail::PyObjectWrapper;
    friend class detail::PyPlot;

    // constructor for figures with a single axis
    explicit Figure(std::size_t id,
                    PyObjectWrapper plt,
                    PyObjectWrapper fig,
                    PyObjectWrapper axis)
    : _id{id}
    , _grid{1, 1}
    , _pyplot{plt}
    , _fig{fig} {
        _axes.push_back(Axis{plt, axis});
    }

    // constructor for figures with a grid of axes
    explicit Figure(std::size_t id,
                    PyObjectWrapper plt,
                    PyObjectWrapper fig,
                    std::vector<detail::PyObjectWrapper> axes,
                    std::size_t ny,
                    std::size_t nx)
    : _id{id}
    , _grid{ny, nx}
    , _pyplot{plt}
    , _fig{fig} {
        if (axes.size() != _grid.count())
            throw std::runtime_error("Number of axes does not match the given grid layout.");
        _axes.reserve(axes.size());
        for (std::size_t i = 0; i < axes.size(); ++i)
            _axes.push_back(Axis{plt, axes[i]});
    }

    std::size_t _id;
    Grid _grid;
    PyObjectWrapper _pyplot;
    PyObjectWrapper _fig;
    std::vector<Axis> _axes;
    std::optional<PyObjectWrapper> _image;
};


#ifndef DOXYGEN
namespace detail {

    long pyobject_to_long(PyObject* pyobj) {
        if (!PyLong_Check(pyobj))
            throw std::runtime_error("Given object does not represent an integer");
        return PyLong_AsLong(pyobj);
    }

    template<std::invocable<PyObject*> Visitor>
    void visit_py_sequence(PyObject* sequence, Visitor&& visitor) {
        if (!PySequence_Check(sequence))
            throw std::runtime_error("Given argument is not a sequence");
        const auto n = PySequence_Size(sequence);
        for (Py_ssize_t i = 0; i < n; ++i)
            visitor(PySequence_GetItem(sequence, i));
    }

    class PyPlot {
     public:
        static PyPlot& instance() {
            static PyPlot plt{};
            return plt;
        }

        Figure figure(std::size_t ny = 1, std::size_t nx = 1) {
            if (ny == 0 || nx == 0)
                throw std::runtime_error("Number of rows/cols must be non-zero.");

            using namespace literals;
            const std::size_t fig_id = _get_unused_fig_id();
            auto fig_axis_tuple = invoke(_pyplot, "subplots", no_args, with("num"_kw = fig_id, "nrows"_kw = ny, "ncols"_kw = nx));
            if (!PyTuple_Check(fig_axis_tuple))
                throw std::runtime_error("Unexpected value returned from pyplot.subplots");
            if (PyTuple_Size(fig_axis_tuple.get()) != 2)
                throw std::runtime_error("Unexpected value returned from pyplot.subplots");

            auto fig = pycall([&] () { return Py_NewRef(PyTuple_GetItem(fig_axis_tuple.get(), 0)); });
            auto axes = pycall([&] () { return Py_NewRef(PyTuple_GetItem(fig_axis_tuple.get(), 1)); });
            if (!axes || !fig)
                throw std::runtime_error("Error creating figure.");
            if (nx*ny == 1)  // axes is a single axis
                return Figure{fig_id, _pyplot, fig, axes};

            std::vector<PyObjectWrapper> ax_vec;
            ax_vec.reserve(ny*nx);
            if (ny == 1 || nx == 1)  // axes is a 1d numpy array
                visit_py_sequence(axes.get(), [&] (PyObject* entry) { ax_vec.emplace_back(entry); });
            else  // axes is a 2d numpy array
                visit_py_sequence(axes.get(), [&] (PyObject* row) {
                    visit_py_sequence(row, [&] (PyObject* entry) { ax_vec.emplace_back(entry); });
                });
            if (ax_vec.size() != nx*ny)
                throw std::runtime_error("Could not create sub-figure axes");

            return Figure{fig_id, _pyplot, fig, ax_vec, ny, nx};
        }

        bool figure_exists(std::size_t id) const {
            return invoke(_pyplot, "fignum_exists", Args::from(id)).get() == Py_True;
        }

        std::size_t number_of_active_figures() const {
            auto result = invoke(_pyplot, "get_fignums");
            if (!PyList_Check(result.get()))
                throw std::runtime_error("Could not determine the number of active figures.");
            return static_cast<std::size_t>(PyList_Size(result.get()));
        }

        void show_all(std::optional<bool> block) const {
            using namespace literals;
            if (block)
                invoke(_pyplot, "show", no_args, with("block"_kw = *block));
            else
                invoke(_pyplot, "show", no_args);
        }

        bool use_style(const std::string& name) {
            auto style = pycall([&] () { return PyObject_GetAttrString(_pyplot.get(), "style"); });
            return style ? static_cast<bool>(invoke(style, "use", detail::Args::from(name))) : false;
        }

     private:
        explicit PyPlot() {
            _pyplot = pycall([&] () { return PyImport_ImportModule("matplotlib.pyplot"); });
            if (!_pyplot)
                throw std::runtime_error("Could not import matplotlib.");
        }

        std::size_t _get_unused_fig_id() {
            std::size_t id = 0;
            while (figure_exists(id))
                id++;
            return id;
        }

        PyObjectWrapper _pyplot{nullptr};
    };

}  // namespace detail
#endif  // DOXYGEN

struct AxisLayout {
    std::size_t nrows;
    std::size_t ncols;
};

//! Create a new figure containing a single axis
Figure figure() {
    return detail::PyPlot::instance().figure();
}

//! Create a new figure with multiple axes arranged in the given number of rows & columns
Figure figure(const AxisLayout& layout) {
    return detail::PyPlot::instance().figure(layout.nrows, layout.ncols);
}

//! Show all figures
void show_all_figures(std::optional<bool> block = {}) {
    detail::PyPlot::instance().show_all(block);
}

//! Set a matplotlib style to be used in newly created figures
bool set_style(const std::string& name) {
    return detail::PyPlot::instance().use_style(name);
}

}  // namespace cpplot
