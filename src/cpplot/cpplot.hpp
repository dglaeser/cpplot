#include <stdexcept>
#include <type_traits>
#include <algorithm>
#include <utility>
#include <optional>
#include <vector>
#include <set>

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

    template<typename F>
    static decltype(auto) pycall(const F& f) {
        return PythonWrapper::instance()(f);
    }

    void swap_pyobjects(PyObject*& a, PyObject*& b) {
        PyObject* c = a;
        a = b;
        b = c;
    }

}  // namespace detail
#endif  // DOXYGEN


namespace ReferenceType {

//! Indicate that a PyObject pointer passed as argument does not "own" a reference
struct Weak {};

}  // namespace ReferenceType


//! RAII wrapper around a PyObject pointer
class PyObjectWrapper {
 public:
    ~PyObjectWrapper() { detail::pycall([&] () { if (_p) Py_DECREF(_p); }); }

    PyObjectWrapper() = default;
    PyObjectWrapper(PyObject* p) : _p{p} {}
    PyObjectWrapper(PyObject* p, ReferenceType::Weak) : _p{p} { detail::pycall([&] () { if (_p) Py_INCREF(_p); }); }

    PyObjectWrapper(const PyObjectWrapper& other) : PyObjectWrapper{other._p, ReferenceType::Weak{}} {}
    PyObjectWrapper(PyObjectWrapper&& other) : PyObjectWrapper{nullptr} { detail::swap_pyobjects(_p, other._p); }

    PyObjectWrapper& operator=(const PyObjectWrapper& other) {
        *this = PyObjectWrapper{other};
        return *this;
    }

    PyObjectWrapper& operator=(PyObjectWrapper&& other) {
        detail::swap_pyobjects(_p, other._p);
        return *this;
    }

    PyObject* release() {
        PyObject* tmp = _p;
        _p = nullptr;
        return tmp;
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


#ifndef DOXYGEN
namespace detail {

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


//! Data structure to represent a Key-Value pair
template<typename Key, typename Value>
class KeyValuePair {
    static_assert(std::is_same_v<Key, std::remove_cvref_t<Key>>);
    static_assert(std::is_default_constructible_v<Key>);

 public:
    using KeyType = Key;
    using ValueType = std::remove_cvref_t<Value>;

    KeyValuePair(const KeyValuePair&) = delete;
    KeyValuePair(KeyValuePair&&) = default;

    template<typename V>
    explicit constexpr KeyValuePair(const Key&, V&& value) noexcept
    : _value{std::forward<V>(value)} {
        static_assert(std::is_same_v<std::remove_cvref_t<Value>, std::remove_cvref_t<V>>);
    }

    //! Return the value associated with this key
    const auto& access_with(const Key&) const {
        return _value;
    }

 private:
    Value _value;
};

template<typename K, typename V>
KeyValuePair(const K&, V&&) -> KeyValuePair<K, std::conditional_t<
    std::is_lvalue_reference_v<V>, V, std::remove_cvref_t<V>
>>;

//! Data structure to represent a keyword argument (without a value)
template<typename Impl>
struct Key {
    //! bind a value to this keyword argument
    template<typename Value>
    constexpr auto operator=(Value&& v) const noexcept {
        static_assert(std::is_default_constructible_v<Impl>);
        return KeyValuePair{Impl{}, std::forward<Value>(v)};
    }
};

//! Data structure to represent the x-values keyword for creating a line plot
struct X : Key<X> { using Key<X>::operator=; };
//! Data structure to represent the y-values keyword for creating a line plot
struct Y : Key<Y> { using Key<Y>::operator=; };
//! Data structure to represent the label keyword for plots
struct Label : Key<Label> {
    using Key<Label>::operator=;
    static std::string name() { return "label"; }
};
//! Data structure to represent the color keyword for plots
struct Color : Key<Color> {
    using Key<Color>::operator=;
    static std::string name() { return "color"; }
};

inline constexpr X x;
inline constexpr Y y;
inline constexpr Label label;
inline constexpr Color color;


#ifndef DOXYGEN
namespace detail {

    template<typename T>
    struct IsKeyValuePair : std::false_type {};
    template<typename K, typename V>
    struct IsKeyValuePair<KeyValuePair<K, V>> : std::true_type {};

}  // namespace detail
#endif  // DOXYGEN

//! Data structure to represent a set of key-value pairs
template<typename... T>
class Kwargs : private T... {
    static_assert(std::conjunction_v<detail::IsKeyValuePair<T>...>);
    static_assert(detail::are_unique_v<T...>);

    using T::access_with...;

 public:
    template<typename K>
    static constexpr bool has_key = std::disjunction_v<std::is_same<typename T::KeyType, K>...>;

    Kwargs(T&&... kwargs)
    : T{std::move(kwargs)}...
    {}

    //! Return the value associated with the given key
    template<typename Key>
    const auto& get(const Key& key) const {
        return access_with(key);
    }

    //! Return a representation as PyObject
    PyObjectWrapper as_pyobject() const {
        if constexpr (sizeof...(T) == 0)
            return nullptr;

        std::string format;
        (..., (format += ",s:" + detail::kwargs_format_for(get(typename T::KeyType{}))));
        format = "{" + format.substr(1) + "}";
        PyObject* result = std::apply(
            [&] (const auto&... args) { return Py_BuildValue(format.c_str(), args...); },
            std::tuple_cat(std::tuple{T::KeyType::name().c_str(), get(typename T::KeyType{})}...)
        );
        if (!result)
            throw std::runtime_error("Conversion to PyObject failed.");
        return result;
    }
};

template<typename... T>
Kwargs(T&&...) -> Kwargs<std::remove_cvref_t<T>...>;

//! Factory function to create a Kwargs object
template<typename... T>
constexpr auto with(T&&... args) {
    return Kwargs{std::forward<T>(args)...};
}

//! Class that contains the data for a line plot
class LinePlot {
 public:
    template<typename A, typename VA, typename B, typename VB>
    static LinePlot from(KeyValuePair<A, VA>&& a, KeyValuePair<B, VB>&& b) {
        return _from(Kwargs{std::move(a), std::move(b)});
    }

    PyObjectWrapper x() const { return _x; }
    PyObjectWrapper y() const { return _y; }

 private:
    template<typename... T>
    static LinePlot _from(Kwargs<T...>&& kwargs) {
        static_assert(Kwargs<T...>::template has_key<X>);
        static_assert(Kwargs<T...>::template has_key<Y>);
        return LinePlot{kwargs.get(X{}), kwargs.get(Y{})};
    }

    template<typename X, typename Y>
    explicit LinePlot(const X& x, const Y& y)
    : _x{detail::as_pylist(x)}
    , _y{detail::as_pylist(y)}
    {}

    PyObjectWrapper _x;
    PyObjectWrapper _y;
};


namespace Traits {

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

template<typename T>
struct ImageAccess;
template<typename T>
struct ImageAccess<std::vector<std::vector<T>>> {
    static const T& at(const std::array<std::size_t, 2>& idx, const std::vector<std::vector<T>>& data) {
        return data.at(idx[0]).at(idx[1]);
    }
};

}  // namespace Traits


//! Class to represent an axis to which plots can be added
class Axis {
 public:
    explicit Axis(PyObjectWrapper mpl, PyObjectWrapper axis)
    : _mpl{std::move(mpl)}
    , _axis{std::move(axis)} {
        assert(_mpl);
        assert(_axis);
    }

    //! Add the given line plot to this axis
    bool add(const LinePlot& p) {
        return add(p, Kwargs<>{});
    }

    //! Add the given line plot to this axis with additional kwargs to be forwarded
    template<typename... T>
    bool add(const LinePlot& p, const Kwargs<T...>& kwargs) {
        return detail::pycall([&] () {
            PyObjectWrapper function = PyObject_GetAttrString(_axis, "plot");
            PyObjectWrapper args = Py_BuildValue("OO", p.x().release(), p.y().release());
            if (function && args) {
                PyObjectWrapper lines = PyObject_Call(function, args, kwargs.as_pyobject());
                if constexpr (Kwargs<T...>::template has_key<Label>)
                    PyObjectWrapper{PyObject_CallMethod(_axis, "legend", nullptr)};
                if (lines)
                    return true;
            }
            PyErr_Print();
            return false;
        });
    }

    template<typename Image>
    bool set_image(const Image& image) {
        return detail::pycall([&] () {
            const auto size = Traits::ImageSize<Image>::get(image);
            PyObject* pydata = PyList_New(size[0]);
            for (std::size_t y = 0; y < size[0]; ++y) {
                PyObject* row = PyList_New(size[1]);
                for (std::size_t x = 0; x < size[1]; ++x)
                    PyList_SET_ITEM(row, x, detail::value_to_pyobject(
                        Traits::ImageAccess<Image>::at({y, x}, image)
                    ).release());
                PyList_SET_ITEM(pydata, y, row);
            }
            PyObjectWrapper result = PyObject_CallMethod(_axis, "imshow", "O", pydata);
            return static_cast<bool>(result);
        });
    }

 private:
    PyObjectWrapper _mpl;
    PyObjectWrapper _axis;
};

//! Class to represent a figure
class Figure {
 public:
    explicit Figure(PyObjectWrapper mpl,
                    PyObjectWrapper fig,
                    PyObjectWrapper axis)
    : _mpl{mpl}
    , _fig{fig}
    , _axis{axis}
    {}

    template<typename... Args>
    bool plot(Args&&... args) {
        return Axis{_mpl, _axis}.add(std::forward<Args>(args)...);
    }

    template<typename Image>
    bool set_image(const Image& image) {
        return Axis{_mpl, _axis}.set_image(image);
    }

 private:
    PyObjectWrapper _mpl;
    PyObjectWrapper _fig;
    PyObjectWrapper _axis;
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
            if (id.has_value() && _figure_exists(*id)) {
                PyObjectWrapper fig = PyObject_CallMethod(_mpl, "figure", "i", id);
                PyObjectWrapper axis = PyObject_CallMethod(_mpl, "gca", nullptr);
                return Figure{_mpl, fig, axis};
            }

            const std::size_t fig_id = id.value_or(_get_unused_fig_id());
            PyObjectWrapper fig_axis_tuple = pycall([&] () -> PyObject* {
                PyObjectWrapper function = PyObject_GetAttrString(_mpl, "subplots");
                if (!function) return nullptr;
                PyObjectWrapper args = PyTuple_New(0);
                PyObjectWrapper kwargs = Py_BuildValue("{s:i}", "num", fig_id);
                return PyObject_Call(function, args, kwargs);
            });

            assert(pycall([&] () { return PyTuple_Check(fig_axis_tuple); }));
            PyObjectWrapper fig = pycall([&] () { return Py_NewRef(PyTuple_GetItem(fig_axis_tuple, 0)); });
            PyObjectWrapper axis = pycall([&] () { return Py_NewRef(PyTuple_GetItem(fig_axis_tuple, 1)); });
            if (!axis || !fig) {
                pycall([&] () { PyErr_Print(); });
                throw std::runtime_error("Error creating line plot.");
            }
            return Figure{_mpl, fig, axis};
        }

        void show_all(std::optional<bool> block) const {
            pycall([&] () {
                PyObjectWrapper function = PyObject_GetAttrString(_mpl, "show");
                if (!function) {
                    PyErr_Print();
                    return;
                }

                PyObject* pyblock = block.has_value() ? (*block ? Py_True : Py_False) : Py_None;
                PyObjectWrapper args = PyTuple_New(0);
                PyObjectWrapper kwargs = Py_BuildValue("{s:O}", "block", pyblock);
                if (!kwargs) {
                    PyErr_Print();
                    return;
                }

                PyObjectWrapper result = PyObject_Call(function, args, kwargs);
                if (!result)
                    PyErr_Print();
            });
        }

        bool use_style(const std::string& name) {
            return pycall([&] () -> bool {
                if (!_mpl) return false;
                PyObjectWrapper style = PyObject_GetAttrString(_mpl, "style");
                if (!style) return false;
                PyObjectWrapper result = PyObject_CallMethod(style, "use", "s", name.c_str());
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
            while (_figure_exists(id))
                id++;
            _figure_ids.insert(id);
            return id;
        }

        bool _figure_exists(std::size_t id) const {
            return _figure_ids.count(id);
        }

        PyObjectWrapper _mpl{nullptr};
        std::set<std::size_t> _figure_ids;
    };

}  // namespace detail
#endif  // DOXYGEN

//! Create a new figure
Figure figure(std::optional<std::size_t> id = {}) {
    return detail::MPLWrapper::instance().figure(id);
}

//! Show all figures
void show_all(std::optional<bool> block = {}) {
    detail::MPLWrapper::instance().show_all(block);
}

//! Set a matplotlib style to be used in newly created figures
bool set_style(const std::string& name) {
    return detail::MPLWrapper::instance().use_style(name);
}

}  // namespace cpplot
