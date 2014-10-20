begin;

-- test data
create temporary table test_json (from_json json);
insert into test_json
	select ('{"": "tyhjä arpa", "vaihtuva": ' || g.i || ', "jotain": {"muuta": [1, 2, 3, null, 4, ["toinen", "array"], 123.123, "tekstiä"], "ihanaa": true, "woot": "heh", "must_ignore": null, "must_ignore_2": ["väärää", "dataa"]}}')::json
	from generate_series(1, 10000) g(i);

-- C versions
CREATE EXTENSION jsonfun;

-- Python version
CREATE OR REPLACE FUNCTION extract_json_strings(the_json json, VARIADIC keys text[])
 RETURNS text[]
 LANGUAGE plpython3u
AS $function$
import json
structure = json.loads(the_json)
rv = []
   
def extract_fields(structure, name):
    name, _, remaining = name.partition('.')
    if name in structure:
        value = structure[name]
        if remaining:
            if not isinstance(value, dict):
                return []
            return extract_fields(value, remaining)

        if isinstance(value, list):
            return value

        return [ value ]
    else:
        return []

for i in keys:
    rv.extend(extract_fields(structure, i))

return [ str(i) for i in rv if i is not None ]
$function$;

-- warm up caches (?)
select * from test_json;

-- do the timings
\timing
\echo json_extract_keys_array
select json_extract_keys_array(from_json, '', 'jotain.muuta', 'jotain.ihanaa', 'jotain.woot', 'jotain.must_ignore') from test_json;
select json_extract_keys_array(from_json, '', 'jotain.muuta', 'jotain.ihanaa', 'jotain.woot', 'jotain.must_ignore') from test_json;

\echo extract_json_strings
select extract_json_strings(from_json, '', 'jotain.muuta', 'jotain.ihanaa', 'jotain.woot', 'jotain.must_ignore') from test_json;
select extract_json_strings(from_json, '', 'jotain.muuta', 'jotain.ihanaa', 'jotain.woot', 'jotain.must_ignore') from test_json;
\timing

-- test some edge cases
\echo null with 'jotain'
select json_extract_keys_array(null::json, 'jotain');
-- crashes
-- select extract_json_strings(null::json, 'jotain');

\echo from_json with '.', '.a', 'a.'
select json_extract_keys_array(from_json, '.', '.a', 'a.') from test_json limit 1;
select extract_json_strings(from_json, '.', '.a', 'a.') from test_json limit 1;

\echo {"": {"": 1, "a": 2}, "a": {"": 3}} with '.', '.a', 'a.'
select json_extract_keys_array('{"": {"": 1, "a": 2}, "a": {"": 3}}'::json, '.', '.a', 'a.');
select extract_json_strings('{"": {"": 1, "a": 2}, "a": {"": 3}}'::json, '.', '.a', 'a.');

\echo {"": {"": {"": "success"}}} with '..'
select json_extract_keys_array('{"": {"": {"": "success"}}}'::json, '..');
select extract_json_strings('{"": {"": {"": "success"}}}'::json, '..');

\echo Test generic delim
select json_extract_delim_keys_array(from_json, '|', '', 'jotain|muuta', 'jotain|ihanaa', 'jotain|woot', 'jotain|must_ignore') from test_json limit 1;

rollback;
