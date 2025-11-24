-- pg_hybrid extension SQL script
-- 版本 1.0

-- 创建扩展
CREATE EXTENSION IF NOT EXISTS pg_hybrid;

CREATE FUNCTION ivfflat_handler(internal) RETURNS index_am_handler
	AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE ACCESS METHOD pg_hybrid_ivfflat TYPE INDEX HANDLER ivfflat_handler;


