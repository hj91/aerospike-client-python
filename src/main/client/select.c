/*******************************************************************************
 * Copyright 2013-2014 Aerospike, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ******************************************************************************/

#include <Python.h>
#include <stdbool.h>

#include <aerospike/aerospike_key.h>
#include <aerospike/as_key.h>
#include <aerospike/as_error.h>
#include <aerospike/as_record.h>

#include "client.h"
#include "conversions.h"
#include "key.h"
#include "policy.h"

/**
 *******************************************************************************************************
 * This function projects bins on given namespace nd set.
 *
 * @param self                  AerospikeClient object
 * @param py_key                The key under which to store the record.
 * @param py_bins               The bins to project into the DB.
 * @param py_policy             The optional policy parameters
 *
 * Returns a tuple containing key, meta and bins.
 *******************************************************************************************************
 */
PyObject * AerospikeClient_Select_Invoke(
	AerospikeClient * self,
	PyObject * py_key, PyObject * py_bins, PyObject * py_policy)
{
	// Python Return Value
	PyObject * py_rec = NULL;
	PyObject * py_ustr = NULL;

	// Aerospike Client Arguments
	as_error err;
	as_policy_read read_policy;
	as_policy_read * read_policy_p = NULL;
	as_key key;
	as_record * rec = NULL;
	char ** bins = NULL;

	// Initialisation flags
	bool key_initialised = false;

	// Initialize error
	as_error_init(&err);

	if (!self || !self->as) {
		as_error_update(&err, AEROSPIKE_ERR_PARAM, "Invalid aerospike object");
		goto CLEANUP;
	}

	if (!self->is_conn_16) {
		as_error_update(&err, AEROSPIKE_ERR_CLUSTER, "No connection to aerospike cluster");
		goto CLEANUP;
	}

	// Convert python key object to as_key
	pyobject_to_key(&err, py_key, &key);
	if ( err.code != AEROSPIKE_OK ) {
		goto CLEANUP;
	}
	// key is initialised successfully
	key_initialised = true;

	// Convert python bins list to char ** bins
	if ( py_bins != NULL && PyList_Check(py_bins) ) {
		Py_ssize_t size = PyList_Size(py_bins);
		bins = (char **) alloca(sizeof(char *) * (size+1));
		for ( int i = 0; i < size; i++ ) {
			PyObject * py_val = PyList_GetItem(py_bins, i);
			bins[i] = (char *) alloca(sizeof(char) * AS_BIN_NAME_MAX_SIZE);
			if ( PyString_Check(py_val) ) {
				strncpy(bins[i], PyString_AsString(py_val), AS_BIN_NAME_MAX_LEN);
				bins[i][AS_BIN_NAME_MAX_LEN] = '\0';
			}
		}
		bins[size] = NULL;
	}
	else if ( py_bins != NULL && PyTuple_Check(py_bins) ) {
		Py_ssize_t size = PyTuple_Size(py_bins);
		bins = (char **) alloca(sizeof(char *) * (size+1));
		for ( int i = 0; i < size; i++ ) {
			PyObject * py_val = PyTuple_GetItem(py_bins, i);
			bins[i] = (char *) alloca(sizeof(char) * AS_BIN_NAME_MAX_SIZE);
			if (PyUnicode_Check(py_val)) {
				py_ustr = PyUnicode_AsUTF8String(py_val);
				strncpy(bins[i], PyString_AsString(py_ustr), AS_BIN_NAME_MAX_LEN);
				bins[i][AS_BIN_NAME_MAX_LEN] = '\0';
			} else if ( PyString_Check(py_val) ) {
				strncpy(bins[i], PyString_AsString(py_val), AS_BIN_NAME_MAX_LEN);
				bins[i][AS_BIN_NAME_MAX_LEN] = '\0';
			} else {
				as_error_update(&err, AEROSPIKE_ERR_PARAM, "not string type");
				goto CLEANUP;
			}
		}
		bins[size] = NULL;
	}
	else {
		as_error_update(&err, AEROSPIKE_ERR_PARAM, "not a list or tuple");
		goto CLEANUP;
	}

	// Convert python policy object to as_policy_exists
	pyobject_to_policy_read(&err, py_policy, &read_policy, &read_policy_p,
			&self->as->config.policies.read);
	if ( err.code != AEROSPIKE_OK ) {
		goto CLEANUP;
	}

	// Initialize record
	as_record_init(rec, 0);

	// Invoke operation
	aerospike_key_select(self->as, &err, read_policy_p, &key, (const char **) bins, &rec);

	if ( err.code == AEROSPIKE_OK ) {
		record_to_pyobject(&err, rec, &key, &py_rec);
	}
	else if ( err.code == AEROSPIKE_ERR_RECORD_NOT_FOUND ) {
		as_error_reset(&err);

		PyObject * py_rec_key = NULL;
		PyObject * py_rec_meta = Py_None;
		PyObject * py_rec_bins = Py_None;

		key_to_pyobject(&err, &key, &py_rec_key);

		py_rec = PyTuple_New(3);
		PyTuple_SetItem(py_rec, 0, py_rec_key);
		PyTuple_SetItem(py_rec, 1, py_rec_meta);
		PyTuple_SetItem(py_rec, 2, py_rec_bins);

		Py_INCREF(py_rec_meta);
		Py_INCREF(py_rec_bins);
	}

CLEANUP:

	if (py_ustr) {
		Py_DECREF(py_ustr);
	}

	if (key_initialised == true){
		// Destroy the key if it is initialised successfully.
		as_key_destroy(&key);
	}

	as_record_destroy(rec);

	if ( err.code != AEROSPIKE_OK ) {
		PyObject * py_err = NULL;
		error_to_pyobject(&err, &py_err);
		PyErr_SetObject(PyExc_Exception, py_err);
		Py_DECREF(py_err);
		return NULL;
	}

	return py_rec;
}

/**
 *******************************************************************************************************
 * Projects bins on given namespace and set.
 *
 * @param self                  AerospikeClient object
 * @param args                  The args is a tuple object containing an argument
 *                              list passed from Python to a C function
 * @param kwds                  Dictionary of keywords
 *
 * Returns a tuple containing key, meta and bins.
 * In case of error,appropriate exceptions will be raised.
 *******************************************************************************************************
 */
PyObject * AerospikeClient_Select(AerospikeClient * self, PyObject * args, PyObject * kwds)
{
	// Python Function Arguments
	PyObject * py_key = NULL;
	PyObject * py_bins = NULL;
	PyObject * py_policy = NULL;

	// Python Function Keyword Arguments
	static char * kwlist[] = {"key", "bins", "policy", NULL};

	// Python Function Argument Parsing
	if ( PyArg_ParseTupleAndKeywords(args, kwds, "OO|O:select", kwlist,
			&py_key, &py_bins, &py_policy) == false ) {
		return NULL;
	}

	// Invoke Operation
	return AerospikeClient_Select_Invoke(self, py_key, py_bins, py_policy);
}
