/*
 * This source file is part of libRocket, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://www.librocket.com
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "precompiled.h"
#include "DataSourceWrapper.h"
#include "../../../Include/Rocket/Core/Log.h"
#include "../../../Include/Rocket/Core/Python/Utilities.h"

namespace Rocket {
namespace Controls {
namespace Python {

// Returns the string representation of a python class name
static Core::String GetPythonClassName(PyObject* object)
{
	Core::String full_name;

	// Check if the object is a class; either a standard Python class or a Boost Python class metatype. If so, we can
	// query the name of the class object directly; otherwise, it is an object that we have to query the class type
	// from.
	bool is_class = PyObject_IsInstance(object, (PyObject *)&PyType_Type) ||
					object->ob_type == python::objects::class_metatype().get();
	PyObject* py_class = NULL;
	if (!is_class)
	{
		py_class = PyObject_GetAttrString(object, "__class__");
		object = py_class;
		if (!object)
		{
			PyErr_Clear();
			return full_name;
		}
	}

	PyObject* py_class_name = PyObject_GetAttrString(object, "__name__");
	if (py_class_name != NULL)
	{
		const char* class_name = PyUnicode_AsUTF8(py_class_name);

		PyObject* py_module_name = PyObject_GetAttrString(object, "__module__");
		const char* module_name = PyUnicode_AsUTF8(py_module_name);

		full_name.FormatString(128, "%s.%s", module_name, class_name);

		Py_DECREF(py_module_name);
		Py_DECREF(py_class_name);
	}
	else
	{
		PyErr_Clear();
	}

	if (py_class != NULL)
		Py_DECREF(py_class);

	return full_name;
}


DataSourceWrapper::DataSourceWrapper(PyObject* _self, const char* name) : DataSource(name)
{
	self = _self;
}

DataSourceWrapper::~DataSourceWrapper()
{
}

void DataSourceWrapper::InitialisePythonInterface()
{
	void (DataSourceWrapper::*NotifyRowChange)(const Core::String&) = &DataSourceWrapper::NotifyRowChange;
	void (DataSourceWrapper::*NotifyRowChangeParams)(const Core::String&, int, int) = &DataSourceWrapper::NotifyRowChange;

	python::scope datasource_scope = python::class_<DataSource, DataSourceWrapper, boost::noncopyable>("DataSource", python::init< const char* >())
		.def("NotifyRowAdd", &DataSourceWrapper::NotifyRowAdd)
		.def("NotifyRowRemove", &DataSourceWrapper::NotifyRowRemove)
		.def("NotifyRowChange", NotifyRowChange)
		.def("NotifyRowChange", NotifyRowChangeParams)		
		;

	// Register the column names for use in python
	python::scope().attr("COLUMN_CHILD_SOURCE") = DataSource::CHILD_SOURCE;
	python::scope().attr("COLUMN_DEPTH") = DataSource::DEPTH;
	python::scope().attr("COLUMN_NUM_CHILDREN") = DataSource::NUM_CHILDREN;
}

void DataSourceWrapper::GetRow(Core::StringList& row, const Core::String& table, int row_index, const Core::StringList& columns)
{
	PyObject* callable = PyObject_GetAttrString(self, "GetRow");
	if (!callable)
	{
		Core::String error_message(128, "Function \"GetRow\" not found on python data source %s.", GetPythonClassName(self).CString());
		Core::Log::Message(Core::Log::LT_WARNING, "%s", error_message.CString());
		PyErr_SetString(PyExc_RuntimeError, error_message.CString());
		python::throw_error_already_set();
		return;
	}

	python::tuple t = python::make_tuple(table.CString(), row_index, columns);
	PyObject* result = PyObject_CallObject(callable, t.ptr());
	Py_DECREF(callable);	

	// If it's a list, then just get the entries out of it
	if (result && PyList_Check(result))
	{
		int num_entries = PyList_Size(result);
		for (int i = 0; i < num_entries; i++)
		{
			Core::String entry;

			PyObject* entry_object = PyList_GetItem(result, i);
			if (PyUnicode_Check(entry_object))
			{
				entry = PyUnicode_AsUTF8(entry_object);
			}
			else if (PyLong_Check(entry_object))
			{
				int entry_int = (int)PyLong_AsLong(entry_object);
				Core::TypeConverter< int, Core::String >::Convert(entry_int, entry);
			}
			else if (PyFloat_Check(entry_object))
			{
				float entry_float = (float)PyFloat_AS_DOUBLE(entry_object);
				Core::TypeConverter< float, Core::String >::Convert(entry_float, entry);
			}
			else
			{
				Core::String error_message(128, "Failed to convert row %d entry %d on data source %s.", row_index, i, GetPythonClassName(self).CString());
				Core::Log::Message(Core::Log::LT_WARNING, "%s", error_message.CString());
				PyErr_SetString(PyExc_RuntimeError, error_message.CString());
				python::throw_error_already_set();
			}

			row.push_back(entry);
		}
	}
	else
	{
		// Print the error and restore it to the caller
		PyObject *type, *value, *traceback;
		PyErr_Fetch(&type, &value, &traceback);
		Py_XINCREF(type);
		Py_XINCREF(value);
		Py_XINCREF(traceback);

		Core::String error_message(128, "Failed to get entries for table %s row %d from python data source %s.", table.CString(), row_index, GetPythonClassName(self).CString());
		Core::Log::Message(Core::Log::LT_WARNING, "%s", error_message.CString());
		if (type == NULL)
			PyErr_SetString(PyExc_RuntimeError, error_message.CString());
		else
			PyErr_Restore(type, value, traceback);

		python::throw_error_already_set();
	}

	if (result)
		Py_DECREF(result);
}

int DataSourceWrapper::GetNumRows(const Core::String& table)
{
	int num_rows = 0;

	PyObject* callable = PyObject_GetAttrString(self, "GetNumRows");
	if (!callable)
	{
		Core::String error_message(128, "Function \"GetNumRows\" not found on python data source %s.", GetPythonClassName(self).CString());
		Core::Log::Message(Core::Log::LT_WARNING, "%s", error_message.CString());
		PyErr_SetString(PyExc_RuntimeError, error_message.CString());
		python::throw_error_already_set();

		return 0;
	}

	PyObject* result = PyObject_CallObject(callable, python::make_tuple(table.CString()).ptr());
	Py_DECREF(callable);

	if (result && PyLong_Check(result))
	{
		num_rows = PyLong_AsLong(result);
	}
	else
	{
		// Print the error and restore it to the caller
		PyObject *type, *value, *traceback;
		PyErr_Fetch(&type, &value, &traceback);
		Py_XINCREF(type);
		Py_XINCREF(value);
		Py_XINCREF(traceback);

		Core::String error_message(128, "Failed to get number of rows from python data source %s.", GetPythonClassName(self).CString());
		Core::Log::Message(Core::Log::LT_WARNING, "%s", error_message.CString());
		if (type == NULL)
			PyErr_SetString(PyExc_RuntimeError, error_message.CString());
		else
			PyErr_Restore(type, value, traceback);

		python::throw_error_already_set();
	}
	
	if (result)
		Py_DECREF(result);

	return num_rows;	
}

Core::ScriptObject DataSourceWrapper::GetScriptObject() const
{
	return self;
}

}
}
}
