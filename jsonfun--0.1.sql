\echo Use "CREATE EXTENSION jsonfun" to load this file. \quit

create function json_extract_keys_array (from_json json, variadic keys text[])
returns text[]
as 'MODULE_PATHNAME', 'json_extract_keys_array'
language C strict;

create function json_extract_keys_with_delim_array (from_json json, delim text, variadic keys text[])
returns text[]
as 'MODULE_PATHNAME', 'json_extract_keys_with_delim_array'
language C strict;
