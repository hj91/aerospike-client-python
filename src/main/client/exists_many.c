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
#include <aerospike/aerospike_batch.h>
#include <aerospike/as_key.h>
#include <aerospike/as_error.h>
#include <aerospike/as_record.h>
#include <aerospike/as_batch.h>

#include "client.h"
#include "conversions.h"
#include "key.h"
#include "policy.h"

/**
 *******************************************************************************************************
 * This callback will be called with the results with aerospike_batch_exists().
 *
 * @param results               An array of n as_batch_read entries
 * @param n                     The number of results from the batch request
 * @param udata                 The return value to be filled with result of
 *                              exists_many()
 *
 * Returns boolean value(true or false).
 *******************************************************************************************************
 */
static
bool batch_exists_cb(const as_batch_read* results, uint32_t n, void* udata)
{
	// Typecast udata back to PyObject
	PyObject * py_recs = (PyObject *) udata;

	// Loop over results array
	for ( uint32_t i =0; i < n; i++ ){

		PyObject * rec = PyDict_New();
		PyObject * p_key = NULL;

		if ( results[i].key->valuep ) {
			switch(((as_val*)(results[i].key->valuep))->type){
				case AS_INTEGER:
					p_key = PyInt_FromLong((long)results[i].key->value.integer.value);
					break;

				case AS_STRING:
					p_key = PyString_FromString((const char *)results[i].key->value.string.value);
					break;
				default:
					break;
			}
		} else {
			Py_INCREF(Py_None);
			p_key = Py_None;
		}

		if ( results[i].result == AEROSPIKE_OK ){

			PyDict_SetItemString( rec, "gen", PyInt_FromLong((long)results[i].record.gen) );
			PyDict_SetItemString( rec, "ttl", PyInt_FromLong((long)results[i].record.ttl) );

			if ( PyDict_SetItem( py_recs, p_key, rec ) ){
				return false;
			}
		} else if (results[i].result == AEROSPIKE_ERR_RECORD_NOT_FOUND){
			
			Py_INCREF(Py_None);

			if( PyDict_SetItem( py_recs, p_key, Py_None)){
				return false;
			}
		}
		Py_DECREF(rec);
		Py_DECREF(p_key);
	}
	return true;
}

/**
 *******************************************************************************************************
 * This function checks if a batch of records are present in DB or not.
 *
 * @param self                  AerospikeClient object
 * @param py_keys               The list of keys
 * @param py_policy             The dictionary of policies
 *
 * Returns the metadata of a record if key exists otherwise NULL.
 *******************************************************************************************************
 */
static
PyObject * AerospikeClient_Exists_Many_Invoke(
	AerospikeClient * self,
	PyObject * py_keys, PyObject * py_policy)
{
	// Python Return Value
	PyObject * py_recs = PyDict_New();

	// Aerospike Client Arguments
	as_error err;
	as_batch batch;
	as_policy_batch policy;
	as_policy_batch * batch_policy_p = NULL;

	// Initialisation flags
	bool batch_initialised = false;

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

	// Convert python keys list to as_key ** and set it in batch.keys
	// keys can be specified in PyList and PyTuple
	if ( py_keys != NULL && PyList_Check(py_keys) ) {
		Py_ssize_t size = PyList_Size(py_keys);

		as_batch_init(&batch, size);
		// batch is initialised
		batch_initialised = true;

		for ( int i = 0; i < size; i++ ) {

			PyObject * py_key = PyList_GetItem(py_keys, i);

			if ( !PyTuple_Check(py_key) ){
				as_error_update(&err, AEROSPIKE_ERR_CLIENT, "Key should be a tuple.");
				goto CLEANUP;
			}

			pyobject_to_key(&err, py_key, as_batch_keyat(&batch, i));

			if ( err.code != AEROSPIKE_OK ) {
				goto CLEANUP;
			}
		}
	}
	else if ( py_keys != NULL && PyTuple_Check(py_keys) ) {
		Py_ssize_t size = PyTuple_Size(py_keys);

		as_batch_init(&batch, size);
		// batch is initialised
		batch_initialised = true;

		for ( int i = 0; i < size; i++ ) {
			PyObject * py_key = PyTuple_GetItem(py_keys, i);

			if ( !PyTuple_Check(py_key) ){
				as_error_update(&err, AEROSPIKE_ERR_CLIENT, "Key should be a tuple.");
				goto CLEANUP;
			}

			pyobject_to_key(&err, py_key, as_batch_keyat(&batch, i));

			if ( err.code != AEROSPIKE_OK ) {
				goto CLEANUP;
			}
		}
	}
	else {
		as_error_update(&err, AEROSPIKE_ERR_CLIENT, "Keys should be specified as a list or tuple.");
		goto CLEANUP;
	}

	// Convert python policy object to as_policy_batch
	pyobject_to_policy_batch(&err, py_policy, &policy, &batch_policy_p,
			&self->as->config.policies.batch);
	if ( err.code != AEROSPIKE_OK ) {
		goto CLEANUP;
	}

	// Invoke C-client API
	aerospike_batch_exists(self->as, &err, batch_policy_p,
		&batch, (aerospike_batch_read_callback) batch_exists_cb,
		py_recs);

CLEANUP:

	if (batch_initialised == true){
		// We should destroy batch object as we are using 'as_batch_init' for initialisation
		// Also, pyobject_to_key is soing strdup() in case of Unicode. So, object destruction
		// is necessary.
		as_batch_destroy(&batch);
	}

	if ( err.code != AEROSPIKE_OK ) {
		PyObject * py_err = NULL;
		error_to_pyobject(&err, &py_err);
		PyErr_SetObject(PyExc_Exception, py_err);
		Py_DECREF(py_err);
		return NULL;
	}

	return py_recs;
}

/**
 *******************************************************************************************************
 * Read the meta-data of records from the database in batch.
 *
 * @param self                  AerospikeClient object
 * @param args                  The args is a tuple object containing an argument
 *                              list passed from Python to a C function
 * @param kwds                  Dictionary of keywords
 *
 * Returns a dictionary of record with key to be primary key and value
 * to be meatadata of a record.
 * In case of error,appropriate exceptions will be raised.
 *******************************************************************************************************
 */
PyObject * AerospikeClient_Exists_Many(AerospikeClient * self, PyObject * args, PyObject * kwds)
{
	// Python Function Arguments
	PyObject * py_keys = NULL;
	PyObject * py_policy = NULL;

	// Python Function Keyword Arguments
	static char * kwlist[] = {"keys", "policy", NULL};

	// Python Function Argument Parsing
	if ( PyArg_ParseTupleAndKeywords(args, kwds, "O|O:exists_many", kwlist,
			&py_keys, &py_policy) == false ) {
		return NULL;
	}

	// Invoke Operation
	return AerospikeClient_Exists_Many_Invoke(self, py_keys, py_policy);
}
