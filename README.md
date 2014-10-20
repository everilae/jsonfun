What
====

A method for extracting variadic set of keys in dot format from JSON and
storing results in an array. For example keys like `path.to.some.value`
and `.leading.empty.key`.

"_array"?
---------

Just a reference to the return type of the function. This actually does not
handle extracting values from arrays with using the given key as an index.
Only objects are supported. What it does do is unpack JSON array values to
the results, if the given key points to such.

Examples
========

Basics.

```sql
select json_extract_keys_array('{"a": {"b": "hello"}, "c": {"d": "world"}}'::json, 'a.b', 'c.d');
```
```
 json_extract_keys_array 
-------------------------
 {hello,world}
(1 row)
```

A bit more contrived example with keys containing empty strings.

```sql
select json_extract_keys_array('{"": {"": 1, "a": 2}, "a": {"": 3}}'::json, '.', '.a', 'a.');
```
```
 json_extract_keys_array 
-------------------------
 {1,2,3}
(1 row)
```

Array unpacking.

```sql
select json_extract_keys_array('{"path": {"to": {"array": [1, 2, 3]}}}'::json, 'path.to.array');
```
```
 json_extract_keys_array 
-------------------------
 {1,2,3}
(1 row)
```

JSON null values will always be ignored. The same goes for keys that point to non existing paths.

```sql
select json_extract_keys_array('{"a": {"b": [1, null, "kaksi"], "c": {"d": 123.123}}}'::json, 'a.b', 'a.c.d', 'does.not.exist');
```
```
 json_extract_keys_array 
-------------------------
 {1,kaksi,123.123}
(1 row)
```

Compile
=======

```bash
% make
```

Install
=======

First

```bash
% make install
```

and then in your database

```sql
create extension jsonfun;
```

Contributors
============

- Ilja Everilä
- Antti Haapala, http://github.com/ztane (original  PL/Python version)
