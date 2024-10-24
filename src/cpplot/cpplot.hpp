#include <stdexcept>
#include <type_traits>
#include <algorithm>
#include <utility>
#include <optional>
#include <vector>
#include <concepts>


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

    template<typename... T>
    struct are_unique;
    template<typename T1, typename T2, typename... Ts>
    struct are_unique<T1, T2, Ts...> {
        static constexpr bool value =
            are_unique<T1, T2>::value &&
            are_unique<T1, Ts...>::value &&
            are_unique<T2, Ts...>::value;
    };
    template<typename T1, typename T2>
    struct are_unique<T1, T2> : std::bool_constant<!std::is_same_v<T1, T2>> {};
    template<typename T>
    struct are_unique<T> : std::true_type {};
    template<>
    struct are_unique<> : std::true_type {};
    template<typename... Ts>
    inline constexpr bool are_unique_v = are_unique<Ts...>::value;

    template<typename... Ts>
    struct OverloadSet : Ts... { using Ts::operator()...; };
    template<typename... Ts> OverloadSet(Ts...) -> OverloadSet<Ts...>;

    template<typename T>
    PyObjectWrapper value_to_pyobject(const T& t) {
        return PyObjectWrapper{pycall([&] () {
            return OverloadSet{
                [&] (const double& v) { return PyFloat_FromDouble(v); },
                [&] (const bool& b) { return b ? Py_True : Py_False; },
                [&] (const int& i) { return PyLong_FromLong(static_cast<long>(i)); },
                [&] (const unsigned int& i) { return PyLong_FromUnsignedLong(static_cast<unsigned long>(i)); },
                [&] (const std::size_t& i) { return PyLong_FromSize_t(i); }
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
                [] (const double&) { return "f"; },
                [] (const int&) { return "i"; },
                [] (const unsigned int&) { return "i"; },
                [] (const std::size_t&) { return "i"; },
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

    class Axis {
    public:
        explicit Axis(PyObjectWrapper mpl, PyObjectWrapper axis)
        : _mpl{std::move(mpl)}
        , _axis{std::move(axis)} {
            assert(_mpl);
            assert(_axis);
        }

        //! Add a line plot to this axis
        template<std::ranges::range X, std::ranges::range Y>
        bool plot(X&& x, Y&& y) {
            return plot(std::forward<X>(x), std::forward<Y>(y), Kwargs<>{});
        }

        //! Add a line plot to this axis with additional kwargs to be forwarded
        template<std::ranges::range X, std::ranges::range Y, typename... T>
        bool plot(X&& x, Y&& y, const Kwargs<T...>& kwargs) {
            return pycall([&] () {
                PyObjectWrapper function = check([&] () { return PyObject_GetAttrString(_axis, "plot"); });
                PyObjectWrapper args = check([&] () { return Py_BuildValue("OO", as_pylist(x).release(), as_pylist(y).release()); });
                if (function && args) {
                    PyObjectWrapper lines = check([&] () { return PyObject_Call(function, args, as_pyobject(kwargs)); });
                    if (kwargs.has_key("label"))
                        PyObjectWrapper{check([&] () { return PyObject_CallMethod(_axis, "legend", nullptr); })};
                    if (lines)
                        return true;
                }
                PyErr_Print();
                return false;
            });
        }

        template<typename Image>
        PyObjectWrapper set_image(const Image& image) {
            return pycall([&] () -> PyObject* {
                const auto size = Traits::ImageSize<Image>::get(image);
                PyObjectWrapper pydata = check([&] () { return PyList_New(size[0]); });
                if (!pydata)
                    return nullptr;

                // PyList_Set_Item "steals" a reference, so we don't have to decrement
                for (std::size_t y = 0; y < size[0]; ++y) {
                    PyObject* row = check([&] () { return PyList_New(size[1]); });
                    for (std::size_t x = 0; x < size[1]; ++x)
                        PyList_SET_ITEM(row, x, value_to_pyobject(
                            Traits::ImageAccess<Image>::at({y, x}, image)
                        ).release());
                    PyList_SET_ITEM(static_cast<PyObject*>(pydata), y, row);
                }
                return check([&] () {
                    return PyObject_CallMethod(_axis, "imshow", "O", static_cast<PyObject*>(pydata));
                });
            });
        }

    private:
        PyObjectWrapper _mpl;
        PyObjectWrapper _axis;
    };

    // forward declaration
    class MPLWrapper;

}  // namespace detail
#endif  // DOXYGEN


//! Class to represent a figure
class Figure {
 public:
    std::size_t id() const {
        return _id;
    }

    template<typename... Args>
    bool plot(Args&&... args) {
        return detail::Axis{_mpl, _axis}.plot(std::forward<Args>(args)...);
    }

    template<typename Image>
    bool set_image(const Image& image) {
        _image = detail::Axis{_mpl, _axis}.set_image(image);
        return static_cast<bool>(_image);
    }

    bool add_colorbar() {
        if (!_image.has_value())
            throw std::runtime_error("Cannot add colorbar; no image has been set");

        return detail::pycall([&] () -> bool {
            using detail::PyObjectWrapper;
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
    explicit Figure(std::size_t id,
                    detail::PyObjectWrapper mpl,
                    detail::PyObjectWrapper fig,
                    detail::PyObjectWrapper axis)
    : _id{id}
    , _mpl{mpl}
    , _fig{fig}
    , _axis{axis}
    {}

    std::size_t _id;
    detail::PyObjectWrapper _mpl;
    detail::PyObjectWrapper _fig;
    detail::PyObjectWrapper _axis;
    std::optional<detail::PyObjectWrapper> _image;
};


#ifndef DOXYGEN
namespace detail {

    class MPLWrapper {
     public:
        static MPLWrapper& instance() {
            static MPLWrapper wrapper{};
            return wrapper;
        }

        Figure figure(std::optional<std::size_t> id = {}) {
            if (id.has_value() && figure_exists(*id)) {
                PyObjectWrapper fig = check([&] () { return PyObject_CallMethod(_mpl, "figure", "i", id); });
                PyObjectWrapper axis = check([&] () { return PyObject_CallMethod(_mpl, "gca", nullptr); });
                return Figure{*id, _mpl, fig, axis};
            }

            const std::size_t fig_id = id.value_or(_get_unused_fig_id());
            PyObjectWrapper fig_axis_tuple = pycall([&] () -> PyObject* {
                PyObjectWrapper function = check([&] () { return PyObject_GetAttrString(_mpl, "subplots"); });
                if (!function) return nullptr;
                PyObjectWrapper args = PyTuple_New(0);
                PyObjectWrapper kwargs = Py_BuildValue("{s:i}", "num", fig_id);
                return check([&] () { return PyObject_Call(function, args, kwargs); });
            });

            auto [fig, axis] = pycall([&] () {
                assert(PyTuple_Check(fig_axis_tuple));
                PyObjectWrapper fig = check([&] () { return Py_NewRef(PyTuple_GetItem(fig_axis_tuple, 0)); });
                PyObjectWrapper axis = check([&] () { return Py_NewRef(PyTuple_GetItem(fig_axis_tuple, 1)); });
                if (!axis || !fig) {
                    throw std::runtime_error("Error creating figure.");
                }
                return std::make_pair(fig, axis);
            });
            return Figure{fig_id, _mpl, fig, axis};
        }

        bool figure_exists(std::size_t id) const {
            return pycall([&] () {
                PyObjectWrapper result = check([&] () { return PyObject_CallMethod(_mpl, "fignum_exists", "i", id); });
                return result.get() == Py_True;
            });
        }

        std::vector<std::size_t> get_fig_ids() const {
            return pycall([&] () {
                PyObjectWrapper result = check([&] () { return PyObject_CallMethod(_mpl, "get_fignums", nullptr); });
                assert(PyList_Check(result));

                const std::size_t size = PyList_Size(result);
                std::vector<std::size_t> ids; ids.reserve(size);
                for (std::size_t i = 0; i < size; ++i) {
                    PyObjectWrapper item = check([&] () { return PyList_GetItem(result, i); });
                    assert(PyLong_Check(item));
                    ids.push_back(PyLong_AsLong(item));
                }

                return ids;
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

        void close_all() const {
            pycall([&] () {
                PyObjectWrapper{check([&] () {
                    return PyObject_CallMethod(_mpl, "close", "s", "all"); }
                )};
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

//! Create a new figure
Figure figure(std::optional<std::size_t> id = {}) {
    return detail::MPLWrapper::instance().figure(id);
}

//! Return true if a figure with the given id exists
bool figure_exists(std::size_t id) {
    return detail::MPLWrapper::instance().figure_exists(id);
}

//! Show all figures
void show_all_figures(std::optional<bool> block = {}) {
    detail::MPLWrapper::instance().show_all(block);
}

//! Close all figures
void close_all_figures() {
    detail::MPLWrapper::instance().close_all();
}

//! Get the ids of all registered figures
std::vector<std::size_t> get_all_figure_ids() {
    return detail::MPLWrapper::instance().get_fig_ids();
}

//! Get all registered figures
std::vector<Figure> get_all_figures() {
    const auto ids = get_all_figure_ids();
    std::vector<Figure> figs; figs.reserve(ids.size());
    for (const auto id : ids)
        figs.push_back(figure(id));
    return figs;
}

//! Set a matplotlib style to be used in newly created figures
bool set_style(const std::string& name) {
    return detail::MPLWrapper::instance().use_style(name);
}

}  // namespace cpplot
