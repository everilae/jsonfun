#include "postgres.h"
#include "fmgr.h"

#include "catalog/pg_type.h"

#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/jsonapi.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

PG_FUNCTION_INFO_V1(json_extract_keys_array);
Datum json_extract_keys_array(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(json_extract_keys_with_delim_array);
Datum json_extract_keys_with_delim_array(PG_FUNCTION_ARGS);

typedef struct JsonResults
{
	text **t;
	int n;
	Size size;
} JsonResults;

typedef struct GetArrayState
{
	JsonLexContext *lex;
	JsonResults *results;
	char	*result_start;
	bool	normalize_results;
	bool	next_scalar;
	bool	next_array;
	int	array_lex_level;
	int	npath;		/* length of each path-related array */
	char	**path_names;	/* field name(s) being sought */
	bool	*pathok;	/* is path matched to current depth? */
} GetArrayState;

static Datum get_array(FunctionCallInfo fcinfo, const int keys_arg, const char delim);

static void get_object_field_start(void *state, char *fname, bool isnull);
static void get_object_field_end(void *state, char *fname, bool isnull);
static void get_scalar(void *state, char *token, JsonTokenType tokentype);
static void get_array_element_start(void *state, bool isnull);
static void get_array_element_end(void *state, bool isnull);

#define JSON_RESULTS_STORE(results, value) \
	do \
	{ \
		if ((results)->n >= (results)->size) \
		{ \
			(results)->size *= 2; \
			(results)->t = repalloc((results)->t, (results)->size * sizeof(text *)); \
		} \
		(results)->t[(results)->n++] = (value); \
	} while (0)

Datum
json_extract_keys_array(PG_FUNCTION_ARGS)
{
	return get_array(fcinfo, 1, '.');
}

Datum
json_extract_keys_with_delim_array(PG_FUNCTION_ARGS)
{
	text *delim = PG_GETARG_TEXT_P(1);
	char *delim_cptr = text_to_cstring(delim);
	/*
	 * This will do all kinds of nasty things with odd multibyte encodings...
	 */
	if (delim_cptr[0] == '\0')
		ereport(ERROR,
			(errmsg("empty delimiter not supported")));

	return get_array(fcinfo, 2, delim_cptr[0]);
}

static Datum
get_array(FunctionCallInfo fcinfo, const int keys_arg, const char delim)
{
	text *json;
	ArrayType *keys;
	ArrayType *result;
	Datum *keytext;
	bool *keynulls;
	int nkeys;
	JsonResults results = { 0 };
	/* return text[] */
	const Oid element_type = TEXTOID;
	const int16 typlen = -1;
	const bool typbyval = false;
	const char typalign = 'i';
	int i;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	json = PG_GETARG_TEXT_P(0);
	keys = PG_GETARG_ARRAYTYPE_P(keys_arg);

	deconstruct_array(keys, element_type,
			  typlen, typbyval, typalign,
			  &keytext, &keynulls, &nkeys);

	results.size = nkeys * 2;
	results.t = palloc(results.size * sizeof(text *));

	for (i = 0; i < nkeys; i++)
	{
		char *dotpath, **tpath;
		int npath, j, k;
		JsonLexContext *lex;
		JsonSemAction *sem;
		GetArrayState *state;

		if (keynulls[i])
			continue;

		dotpath = TextDatumGetCString(keytext[i]);
		npath = 1;
		for (j = 0; dotpath[j] != '\0'; j++)
			if (dotpath[j] == delim)
				npath++;

		tpath = palloc(npath * sizeof(char *));
		k = 0;

		tpath[k++] = dotpath;
		for (j = 0; dotpath[j] != '\0' && k < npath; j++)
		{
			if (dotpath[j] == delim)
			{
				/*
				 * this turns dotpath in to a list of char arrays pointed
				 * to by tpath
				 */
				dotpath[j] = '\0';
				tpath[k++] = &dotpath[j + 1];
			}
		}

		lex = makeJsonLexContext(json, true);
		sem = palloc0(sizeof(JsonSemAction));
		state = palloc0(sizeof(GetArrayState));

		state->lex = lex;
		/* _as_text */
		state->results = &results;
		state->normalize_results = true;
		state->path_names = tpath;
		state->npath = npath;
		state->pathok = palloc0(sizeof(bool) * npath);
		state->pathok[0] = true;

		sem->semstate = (void *) state;
		sem->scalar = get_scalar;
		sem->object_field_start = get_object_field_start;
		sem->object_field_end = get_object_field_end;
		sem->array_element_start = get_array_element_start;
		sem->array_element_end = get_array_element_end;

		pg_parse_json(lex, sem);
	}

	result = construct_array((DatumPtr) results.t, results.n,
			element_type, typlen, typbyval, typalign);

	if (result != NULL)
		PG_RETURN_ARRAYTYPE_P(result);
	else
		PG_RETURN_NULL();
}

/*
 * Copied from src/backend/utils/adt/jsonfuncs.c
 */
static void
get_object_field_start(void *state, char *fname, bool isnull)
{
	GetArrayState *_state = (GetArrayState *) state;
	bool get_next = false;
	int lex_level = _state->lex->lex_level;
	if (lex_level <= _state->npath &&
			_state->pathok[lex_level - 1] &&
			_state->path_names != NULL &&
			_state->path_names[lex_level - 1] != NULL &&
			strcmp(fname, _state->path_names[lex_level - 1]) == 0)
	{
		if (lex_level < _state->npath)
		{
			/* if not at end of path just mark path ok */
			_state->pathok[lex_level] = true;
		}
		else
		{
			/* end of path, so we want this value */
			get_next = true;
		}
	}
	if (get_next)
	{
		/* this object overrides any previous matching object */
		_state->result_start = NULL;

		if (_state->normalize_results &&
				_state->lex->token_type == JSON_TOKEN_STRING)
		{
			/* for as_text variants, tell get_scalar to set it for us */
			_state->next_scalar = true;
		}
		else if (_state->lex->token_type != JSON_TOKEN_ARRAY_START)
		{
			/* for non-as_text variants, just note the json starting point */
			_state->result_start = _state->lex->token_start;
		}
		else
		{
			/* signal array methods that the next field value should extend results */
			_state->next_array = true;
			/* in order to avoid flattening the array store current lex_level + array level (1) */
			_state->array_lex_level = lex_level + 1;
		}
	}
}

static void
get_object_field_end(void *state, char *fname, bool isnull)
{
	GetArrayState *_state = (GetArrayState *) state;
	bool get_last = false;
	int lex_level = _state->lex->lex_level;
	/* same tests as in get_object_field_start */
	if (lex_level <= _state->npath &&
			_state->pathok[lex_level - 1] &&
			_state->path_names != NULL &&
			_state->path_names[lex_level - 1] != NULL &&
			strcmp(fname, _state->path_names[lex_level - 1]) == 0)
	{
		if (lex_level < _state->npath)
		{
			/* done with this field so reset pathok */
			_state->pathok[lex_level] = false;
		}
		else
		{
			/* end of path, so we want this value */
			get_last = true;
		}
	}
	/* for as_text scalar case, our work is already done */
	if (get_last)
	{
		if (_state->result_start != NULL)
		{
			/*
			 * make a text object from the string from the prevously noted json
			 * start up to the end of the previous token (the lexer is by now
			 * ahead of us on whatever came after what we're interested in).
			 */
			if (!isnull)
			{
				char *start = _state->result_start;
				int len = _state->lex->prev_token_terminator - start;
				JSON_RESULTS_STORE(_state->results, cstring_to_text_with_len(start, len));
			}
			/* this should be unnecessary but let's do it for cleanliness: */
			_state->result_start = NULL;
		}
		else if (_state->next_array)
		{
			_state->next_array = false;
		}
	}
}

static void
get_scalar(void *state, char *token, JsonTokenType tokentype)
{
	GetArrayState *_state = (GetArrayState *) state;
	int lex_level = _state->lex->lex_level;
	/* Check for whole-object match */
	if (lex_level == 0 && _state->npath == 0)
	{
		if (_state->normalize_results && tokentype == JSON_TOKEN_STRING)
		{
			/* we want the de-escaped string */
			_state->next_scalar = true;
		}
		else if (tokentype != JSON_TOKEN_NULL)
		{
			/*
			 * This is a bit hokey: we will suppress whitespace after the
			 * scalar token, but not whitespace before it. Probably not worth
			 * doing our own space-skipping to avoid that.
			 */
			char *start = _state->lex->input;
			int len = _state->lex->prev_token_terminator - start;
			JSON_RESULTS_STORE(_state->results, cstring_to_text_with_len(start, len));
		}
	}
	if (_state->next_scalar)
	{
		/* a de-escaped text value is wanted, so supply it */
		JSON_RESULTS_STORE(_state->results, cstring_to_text(token));
		/* make sure the next call to get_scalar add extras */
		_state->next_scalar = false;
	}
}

static void
get_array_element_start(void *state, bool isnull)
{
	GetArrayState *_state = (GetArrayState *) state;
	int lex_level = _state->lex->lex_level;
	/*
	 * Path points to an array. Extract elements.
	 */
	if (_state->next_array && _state->array_lex_level == lex_level) {
		_state->result_start = NULL;

		if (_state->normalize_results &&
				_state->lex->token_type == JSON_TOKEN_STRING)
		{
			_state->next_scalar = true;
		}
		else
		{
			_state->result_start = _state->lex->token_start;
		}
	}
}

static void
get_array_element_end(void *state, bool isnull)
{
	GetArrayState *_state = (GetArrayState *) state;
	int lex_level = _state->lex->lex_level;
	/*
	 * Same tests as in get_array_element_start.
	 */
	if (_state->next_array &&
			_state->array_lex_level == lex_level &&
			_state->result_start != NULL)
	{
		/* skip nulls entirely */
		if (!isnull)
		{
			char *start = _state->result_start;
			int len = _state->lex->prev_token_terminator - start;
			JSON_RESULTS_STORE(_state->results, cstring_to_text_with_len(start, len));
		}
		_state->result_start = NULL;
	}
}
