Python client Testing
=========

This testing includes unit-testing of all the API's provided by Python client.

Directory Structure
-------------------

```
test--
     |__ test_get.py
     |   
     |__ test_kv.py
     |
     |__ run
```

test_get.py :    
    It has all the test cases regarding aerospike.client.get() API.  
  
test_kv.py :  
    It is a general test suite which includes all API's single test cases.  
Execution
---------

You will find an executable bash script named _run_, which will kick start execution of all the test cases.  
You can execute individual test case by specifying individual test case name as command line parameter to _run_ script.  
e.g.
```
$: run test_get_with_key_digest
```
For more options check [Pytest usage] and run
```
$: py.test -v [options]
```

[Pytest usage]:http://pytest.org/latest/usage.html

To set the server details, modify the file config.conf
If a non-secure server is to be used specify the list of hosts in the [non-secure] section. All values of the [secure] section 
should be set to None.
If a secure server is to be used specify the list of hosts in the [secure] section along with the username and password.All 
values of the [non-secure] section should be set to None.
The list of hosts should be separated by a semi-colon. The last host should be terminated with a semi-colon. 

e.g.
```
[non-secure]
hosts: 127.0.0.1:3000;192.168.0.1:3000;
[secure]
hostssecure: None
user: None
password: None
```
