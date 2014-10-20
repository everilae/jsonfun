EXTENSION = jsonfun
MODULES = json_extract_keys_array
DATA = jsonfun--0.1.sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
