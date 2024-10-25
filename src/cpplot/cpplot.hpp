#include <stdexcept>
#include <type_traits>
#include <algorithm>
#include <utility>
#include <optional>
#include <vector>
#include <concepts>
#include <string_view>


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

#ifndef DOXYGEN
namespace detail {

    class PythonWrapper {
        explicit PythonWrapper() {
            Py_Initialize();
            if (!Py_IsInitialized())
                throw std::runtime_error("Could not initialize Python.");
        };

     public:
        ~PythonWrapper() {
            if (Py_IsInitialized())
                Py_Finalize();
        }

        static const PythonWrapper& instance() {
            static PythonWrapper wrapper{};
            return wrapper;
        }

        template<typename F>
        decltype(auto) operator()(F&& f) const {
            if (!Py_IsInitialized())
                throw std::runtime_error("Python is not initialized.");
            return f();
        }
    };

    template<std::invocable F>
    inline decltype(auto) pycall(const F& f) {
        return PythonWrapper::instance()(f);
    }

    template<std::invocable F> requires(std::is_same_v<std::invoke_result_t<F>, PyObject*>)
    inline PyObject* check(const F& f) {
        PyObject* result = f();
        if (!result)
            PyErr_Print();
        return result;
    }

    template<std::invocable F> requires(std::is_same_v<std::invoke_result_t<F>, int>)
    inline void check(const F& f) {
        int err_code = f();
        if (!err_code)
            PyErr_Print();
    }

    void swap_pyobjects(PyObject*& a, PyObject*& b) {
        PyObject* c = a;
        a = b;
        b = c;
    }

    struct WeakReference {};

    class PyObjectWrapper {
    public:
        ~PyObjectWrapper() { pycall([&] () { if (_p) Py_DECREF(_p); }); }

        PyObjectWrapper() = default;
        PyObjectWrapper(PyObject* p) : _p{p} {}
        PyObjectWrapper(PyObject* p, WeakReference) : _p{p} { pycall([&] () { if (_p) Py_INCREF(_p); }); }

        PyObjectWrapper(const PyObjectWrapper& other) : PyObjectWrapper{other._p, WeakReference{}} {}
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

        PyObject* get() {
            return _p;
        }

        operator PyObject*() const {
            return _p;
        }

        operator bool() const {
            return static_cast<bool>(_p);
        }

    private:
        PyObject* _p{nullptr};
    };

    template<typename... Ts>
    struct OverloadSet : Ts... { using Ts::operator()...; };
    template<typename... Ts> OverloadSet(Ts...) -> OverloadSet<Ts...>;

    template<typename T>
    PyObjectWrapper value_to_pyobject(const T& t) {
        return PyObjectWrapper{pycall([&] () {
            return OverloadSet{
                [&] (const std::floating_point auto& v) { return PyFloat_FromDouble(static_cast<double>(v)); },
                [&] (const bool& b) { return b ? Py_True : Py_False; },
                [&] (const std::integral auto& i) { return PyLong_FromLong(static_cast<long>(i)); },
                [&] (const std::unsigned_integral auto& i) { return PyLong_FromSize_t(static_cast<std::size_t>(i)); },
            }(t);
        })};
    }

    template<typename R>
    PyObjectWrapper as_pylist(const R& range) {
        return pycall([&] () {
            PyObject* list = PyList_New(std::size(range));
            std::size_t i = 0;
            for (const auto& value : range)
                PyList_SetItem(list, i++, value_to_pyobject(value).release());
            return list;
        });
    }

    template<typename T>
    std::string kwargs_format_for(const T& t) {
        return pycall([&] () {
            return OverloadSet{
                [] (const PyObject*) { return "O"; },
                [] (const std::floating_point auto&) { return "f"; },
                [] (const std::integral auto&) { return "i"; },
                [] (const std::string&) { return "s"; }
            }(t);
        });
    }

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
    Kwargs(T&&... kwargs)
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

//! Factory function to create a Kwargs object
template<typename... T>
constexpr auto with(T&&... args) {
    return Kwargs{std::forward<T>(args)...};
}

namespace Traits {

//! Customization point to get the size of an image type
template<typename T>
struct ImageSize;
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

//! Customization point to get a value in an image
template<typename T>
struct ImageAccess;
template<typename T>
struct ImageAccess<std::vector<std::vector<T>>> {
    static const T& at(const std::array<std::size_t, 2>& idx, const std::vector<std::vector<T>>& data) {
        return data.at(idx[0]).at(idx[1]);
    }
};

}  // namespace Traits


#ifndef DOXYGEN
namespace detail {

    template<typename... T>
    PyObjectWrapper as_pyobject(const Kwargs<T...>& kwargs) {
        if constexpr (sizeof...(T) == 0)
            return nullptr;

        const auto as_buildvalue_arg = OverloadSet{
            [] (const std::string& s) { return s.c_str(); },
            [] (const auto& arg) { return arg; }
        };

        PyObject* result = nullptr;
        kwargs.apply([&] <typename... _T> (_T&&... kwarg) {
            std::string format;
            (..., (format += ",s:" + kwargs_format_for(kwarg.value())));
            format = "{" + format.substr(1) + "}";
            result = std::apply(
                [&] (const auto&... args) {
                    return check([&] () {
                        return Py_BuildValue(format.c_str(), as_buildvalue_arg(args)...);
                    });
                },
                std::tuple_cat(
                    std::tuple{kwarg.key().c_str()...},
                    std::tuple{kwarg.value()...}
                )
            );
        });

        if (!result)
            throw std::runtime_error("Conversion to PyObject failed.");
        return result;
    }

    // forward declarations
    class MPLWrapper;

}  // namespace detail
#endif  // DOXYGEN

// forward declaration
class Figure;

//! Class to represent an axis for plotting lines, images, histograms, etc..
class Axis {
    using PyObjectWrapper = detail::PyObjectWrapper;
 public:
    //! Add a title to this axis
    bool set_title(std::string_view title) {
        return detail::check([&] () {
            return PyObject_CallMethod(_axis, "set_title", "s", title.data());
        });
    }

    //! Add a line plot to this axis
    template<std::ranges::range X, std::ranges::range Y>
    bool plot(X&& x, Y&& y) {
        return plot(std::forward<X>(x), std::forward<Y>(y), Kwargs<>{});
    }

    //! Add a line plot to this axis with additional kwargs to be forwarded
    template<std::ranges::range X, std::ranges::range Y, typename... T>
    bool plot(X&& x, Y&& y, const Kwargs<T...>& kwargs) {
        return detail::pycall([&] () {
            PyObjectWrapper function = detail::check([&] () {
                return PyObject_GetAttrString(_axis, "plot");
            });
            PyObjectWrapper args = detail::check([&] () {
                return Py_BuildValue("OO", detail::as_pylist(x).release(), detail::as_pylist(y).release());
            });
            if (function && args) {
                PyObjectWrapper lines = detail::check([&] () { return PyObject_Call(function, args, detail::as_pyobject(kwargs)); });
                if (kwargs.has_key("label"))
                    PyObjectWrapper{detail::check([&] () { return PyObject_CallMethod(_axis, "legend", nullptr); })};
                if (lines)
                    return true;
            }
            PyErr_Print();
            return false;
        });
    }

    //! Plot an image on this axie
    template<typename Image>  // constrain on image concept
    bool set_image(const Image& image) {
        _image = detail::pycall([&] () -> PyObject* {
            const auto size = Traits::ImageSize<Image>::get(image);
            PyObjectWrapper pydata = detail::check([&] () { return PyList_New(size[0]); });
            if (!pydata)
                return nullptr;

            // PyList_Set_Item "steals" a reference, so we don't have to decrement
            for (std::size_t y = 0; y < size[0]; ++y) {
                PyObject* row = detail::check([&] () { return PyList_New(size[1]); });
                for (std::size_t x = 0; x < size[1]; ++x)
                    PyList_SET_ITEM(row, x, detail::value_to_pyobject(
                        Traits::ImageAccess<Image>::at({y, x}, image)
                    ).release());
                PyList_SET_ITEM(static_cast<PyObject*>(pydata), y, row);
            }
            return detail::check([&] () {
                return PyObject_CallMethod(_axis, "imshow", "O", static_cast<PyObject*>(pydata));
            });
        });
        return static_cast<bool>(_image);
    }

    //! Add a colorbar to a previously added image (throws if no image had been set)
    bool add_colorbar() {
        if (!_image.has_value())
            throw std::runtime_error("Cannot add colorbar; no image has been set");

        return detail::pycall([&] () -> bool {
            using detail::check;
            PyObjectWrapper args = PyTuple_New(0);
            PyObjectWrapper kwargs = check([&] () {
                return Py_BuildValue(
                    "{s:O,s:O}",
                    "mappable", static_cast<PyObject*>(*_image),
                    "ax", static_cast<PyObject*>(_axis)
                );
            });
            PyObjectWrapper function = check([&] () {
                return PyObject_GetAttrString(_mpl, "colorbar");
            });
            if (!function || !kwargs)
                return false;
            return check([&] () { return PyObject_Call(function, args, kwargs); });
        });
    }

 private:
    friend class Figure;
    explicit Axis(PyObjectWrapper mpl, PyObjectWrapper axis)
    : _mpl{std::move(mpl)}
    , _axis{std::move(axis)} {
        assert(_mpl);
        assert(_axis);
    }

    PyObjectWrapper _mpl;
    PyObjectWrapper _axis;
    std::optional<detail::PyObjectWrapper> _image;
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
    bool set_title(std::string_view title) {
        return detail::check([&] () {
            return PyObject_CallMethod(_fig, "suptitle", "s", title.data());
        });
    }

    //! Return the axis at the given row and column
    Axis& at(std::size_t y, std::size_t x) {
        if (y >= _grid.ny) throw std::runtime_error("y-index out of bounds.");
        if (x >= _grid.nx) throw std::runtime_error("x-index out of bounds.");
        return _axes.at(y*_grid.nx + x);
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
    bool set_image(Args&&... args) {
        if (_grid.count() > 1)
            throw std::runtime_error("Figure has multiple axes, retrieve the desired axis and use its set_image function.");
        return _axes[0].set_image(std::forward<Args>(args)...);
    }

    //! Convenience function for figures with a single axis (throws if multiple axes are defined)
    template<typename... Args>
    bool add_colorbar(Args&&... args) {
        if (_grid.count() > 1)
            throw std::runtime_error("Figure has multiple axes, retrieve the desired axis and use its set_image function.");
        return _axes[0].add_colorbar(std::forward<Args>(args)...);
    }

    //! Close this figure
    bool close() {
        return detail::pycall([&] () {
            detail::PyObjectWrapper result = detail::check([&] () {
                return PyObject_CallMethod(_mpl, "close", "i", _id);
            });
            return result != nullptr;
        });
    }

 private:
    friend class detail::MPLWrapper;

    // constructor for figures with a single axis
    explicit Figure(std::size_t id,
                    detail::PyObjectWrapper mpl,
                    detail::PyObjectWrapper fig,
                    detail::PyObjectWrapper axis)
    : _id{id}
    , _grid{1, 1}
    , _mpl{mpl}
    , _fig{fig} {
        _axes.push_back(Axis{mpl, axis});
    }

    // constructor for figures with a grid of axes
    explicit Figure(std::size_t id,
                    detail::PyObjectWrapper mpl,
                    detail::PyObjectWrapper fig,
                    std::vector<detail::PyObjectWrapper> axes,
                    std::size_t ny,
                    std::size_t nx)
    : _id{id}
    , _grid{ny, nx}
    , _mpl{mpl}
    , _fig{fig} {
        if (axes.size() != _grid.count())
            throw std::runtime_error("Number of axes does not match the given grid layout.");
        _axes.reserve(axes.size());
        for (std::size_t i = 0; i < axes.size(); ++i)
            _axes.push_back(Axis{mpl, axes[i]});
    }

    std::size_t _id;
    Grid _grid;
    detail::PyObjectWrapper _mpl;
    detail::PyObjectWrapper _fig;
    std::vector<Axis> _axes;
    std::optional<detail::PyObjectWrapper> _image;
};


#ifndef DOXYGEN
namespace detail {

    long pyobject_to_long(PyObject* pyobj) {
        if (!PyLong_Check(pyobj))
            throw std::runtime_error("Given object does not represent an integer");
        return PyLong_AsLong(pyobj);
    }

    class MPLWrapper {
     public:
        static MPLWrapper& instance() {
            static MPLWrapper wrapper{};
            return wrapper;
        }

        Figure figure(std::size_t ny = 1, std::size_t nx = 1) {
            if (ny == 0 || nx == 0)
                throw std::runtime_error("Number of rows/cols must be non-zero.");

            const std::size_t fig_id = _get_unused_fig_id();
            PyObjectWrapper fig_axis_tuple = pycall([&] () -> PyObject* {
                PyObjectWrapper function = check([&] () { return PyObject_GetAttrString(_mpl, "subplots"); });
                if (!function) return nullptr;
                PyObjectWrapper args = PyTuple_New(0);
                PyObjectWrapper kwargs = Py_BuildValue("{s:i,s:i,s:i}", "num", fig_id, "nrows", ny, "ncols", nx);
                return check([&] () { return PyObject_Call(function, args, kwargs); });
            });

            auto [fig, axes] = pycall([&] () {
                assert(PyTuple_Check(fig_axis_tuple));
                PyObjectWrapper fig = check([&] () { return Py_NewRef(PyTuple_GetItem(fig_axis_tuple, 0)); });
                PyObjectWrapper axes = check([&] () { return Py_NewRef(PyTuple_GetItem(fig_axis_tuple, 1)); });
                if (!axes || !fig)
                    throw std::runtime_error("Error creating figure.");
                return std::make_pair(fig, axes);
            });

            if (nx*ny == 1)  // axes is a single axis
                return Figure{fig_id, _mpl, fig, axes};

            std::vector<PyObjectWrapper> ax_vec;
            ax_vec.reserve(ny*nx);
            pycall([&] () {
                assert(PySequence_Check(axes));
                if (ny > 1) {  // in this case, axes is a 2d numpy array
                    const auto seq_ny = PySequence_Size(axes);
                    for (std::size_t y = 0; y < seq_ny; ++y) {
                        PyObjectWrapper row = PySequence_GetItem(axes, y);
                        assert(PySequence_Check(row));
                        const auto seq_nx = PySequence_Size(row);
                        for (std::size_t x = 0; x < seq_nx; ++x)
                            ax_vec.push_back(check([&] () { return PySequence_GetItem(row, x); }));
                    }
                } else {  // axes is a 1d numpy array
                    const auto seq_nx = PySequence_Size(axes);
                    for (std::size_t x = 0; x < seq_nx; ++x)
                        ax_vec.push_back(check([&] () { return PySequence_GetItem(axes, x); }));
                }
            });
            if (ax_vec.size() != nx*ny)
                throw std::runtime_error("Could not create sub-figure axes");
            return Figure{fig_id, _mpl, fig, ax_vec, ny, nx};
        }

        bool figure_exists(std::size_t id) const {
            return pycall([&] () {
                PyObjectWrapper result = check([&] () { return PyObject_CallMethod(_mpl, "fignum_exists", "i", id); });
                return result.get() == Py_True;
            });
        }

        std::size_t number_of_active_figures() const {
            return pycall([&] () {
                PyObjectWrapper result = check([&] () { return PyObject_CallMethod(_mpl, "get_fignums", nullptr); });
                assert(PyList_Check(result));
                return PyList_Size(result);
            });
        }

        void show_all(std::optional<bool> block) const {
            pycall([&] () {
                PyObjectWrapper function = check([&] () { return PyObject_GetAttrString(_mpl, "show"); });
                if (!function)
                    return;

                PyObject* pyblock = block.has_value() ? (*block ? Py_True : Py_False) : Py_None;
                PyObjectWrapper args = PyTuple_New(0);
                PyObjectWrapper kwargs = Py_BuildValue("{s:O}", "block", pyblock);
                PyObjectWrapper result = check([&] () { return PyObject_Call(function, args, kwargs); });
            });
        }

        bool use_style(const std::string& name) {
            return pycall([&] () -> bool {
                if (!_mpl) return false;
                PyObjectWrapper style = check([&] () { return PyObject_GetAttrString(_mpl, "style"); });
                if (!style) return false;
                PyObjectWrapper result = check([&] () { return PyObject_CallMethod(style, "use", "s", name.c_str()); });
                return static_cast<bool>(result);
            });
        }

     private:
        explicit MPLWrapper() {
            _mpl = pycall([&] () {
                return PyImport_ImportModule("matplotlib.pyplot");
            });
            if (!_mpl)
                throw std::runtime_error("Could not import matplotlib.");
        }

        std::size_t _get_unused_fig_id() {
            std::size_t id = 0;
            while (figure_exists(id))
                id++;
            return id;
        }

        PyObjectWrapper _mpl{nullptr};
    };

}  // namespace detail
#endif  // DOXYGEN

//! Create a new figure containing a single axis
Figure figure() {
    return detail::MPLWrapper::instance().figure();
}

//! Create a new figure with multiple axes arranged in the given number of rows & columns
Figure figure(std::size_t nrows, std::size_t ncols) {
    return detail::MPLWrapper::instance().figure(nrows, ncols);
}

//! Show all figures
void show_all_figures(std::optional<bool> block = {}) {
    detail::MPLWrapper::instance().show_all(block);
}

//! Set a matplotlib style to be used in newly created figures
bool set_style(const std::string& name) {
    return detail::MPLWrapper::instance().use_style(name);
}

}  // namespace cpplot
