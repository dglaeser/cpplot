#include <type_traits>
#include <stdexcept>
#include <optional>
#include <vector>

#include <Python.h>

namespace cpplot {

struct PlotOptions {
    std::optional<std::string> label = {};
    std::optional<std::string> color = {};
};

class Plot {
 public:
    struct FigureHandle {
     private:
        explicit FigureHandle(std::size_t i) : id{i} {}
        friend class Plot;
        std::size_t id;
    };



    explicit Plot() {
        Py_Initialize();
        if (!_is_initialized())
            throw std::runtime_error("Initialization failed.");

        _mpl = PyImport_ImportModule("matplotlib.pyplot");
        if (_mpl == nullptr)
            throw std::runtime_error("Could not import matplotlib.");
    }

    std::optional<FigureHandle> figure() {
        std::size_t next_id = _figure_count;
        if (!_set_fig(next_id)) {
            PyErr_Print();
            return {};
        }

        _figure_count++;
        return FigureHandle{next_id};
    }

    bool plot_to(const FigureHandle& handle,
                 const std::vector<double>& x,
                 const std::vector<double>& y,
                 const PlotOptions& opts = {}) {
        PyObject* x_list = PyList_New(x.size());
        for (std::size_t i = 0; i < x.size(); ++i)
            if (PyList_SetItem(x_list, i, _value_to_pyobject(x[i])) != 0)
                throw std::runtime_error("SetItem failed");

        PyObject* y_list = PyList_New(y.size());
        for (std::size_t i = 0; i < y.size(); ++i)
            if (PyList_SetItem(y_list, i, _value_to_pyobject(y[i])) != 0)
                throw std::runtime_error("SetItem failed");

        if (!_set_fig(handle.id)) {
            PyErr_Print();
            return false;
        }

        PyObject* function = PyObject_GetAttrString(_mpl, "plot");
        PyObject* args = Py_BuildValue("OO", x_list, y_list);
        PyObject* kwargs = Py_BuildValue("{s:O,s:O}",
            "label", opts.label.has_value() ? PyUnicode_FromString(opts.label->c_str()) : Py_None,
            "color", opts.color.has_value() ? PyUnicode_FromString(opts.color->c_str()) : Py_None
        );
        PyObject* lines = PyObject_Call(function, args, kwargs);
        if (opts.label.has_value())
            PyObject_CallMethod(_mpl, "legend", nullptr);
        if (lines == nullptr)
            PyErr_Print();

        bool success = lines != nullptr;
        if (!success)
            PyErr_Print();

        Py_DECREF(lines);
        Py_DECREF(kwargs);
        Py_DECREF(args);
        Py_DECREF(function);
        Py_DECREF(y_list);
        Py_DECREF(x_list);
        return success;
    }

    void show() const {
        if (_is_initialized())
            PyObject_CallMethod(_mpl, "show", nullptr);
    }

    ~Plot() {
        if (_is_initialized()) {
            Py_DECREF(_mpl);
            Py_Finalize();
        }
    }

 private:
    bool _set_fig(std::size_t id) {
        if (!_is_initialized())
            return false;
        PyObject* fig = PyObject_CallMethod(_mpl, "figure", "i", id);
        if (PyObject_CallMethod(_mpl, "figure", "i", id) != nullptr) {
            return true;
        }
        return false;
    }

    template<typename T>
    PyObject* _value_to_pyobject(const T& t) const {
        if (!_is_initialized())
            return nullptr;
        static_assert(std::is_same_v<T, double>, "Only double implemented so far");
        PyObject* result = PyFloat_FromDouble(t);
        if (result == nullptr)
            throw std::runtime_error("Item conversion");
        return result;
    }

    bool _is_initialized() const {
        return Py_IsInitialized() != 0;
    }

    PyObject* _mpl{nullptr};
    std::size_t _figure_count = 0;
};

}  // namespace cpplot
