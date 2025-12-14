-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_hybrid" to load this file. \quit

-- pg_hybrid extension SQL script
-- 版本 1.0
-- 
-- pg_hybrid - Columnar storage engine with IVFFlat index support
-- 
-- 包含完整的 hvector 类型实现，不依赖 pgvector 扩展

-- ============================================================================
-- hvector 类型定义
-- ============================================================================

-- 创建 vector 类型（先声明，后定义）
CREATE TYPE hvector;

-- 创建输入输出函数
CREATE FUNCTION hvector_in(cstring, oid, integer) RETURNS hvector
	AS 'MODULE_PATHNAME', 'hvector_in'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION hvector_out(hvector) RETURNS cstring
	AS 'MODULE_PATHNAME', 'hvector_out'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION hvector_typmod_in(cstring[]) RETURNS integer
	AS 'MODULE_PATHNAME', 'hvector_typmod_in'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION hvector_recv(internal, oid, integer) RETURNS hvector
	AS 'MODULE_PATHNAME', 'hvector_recv'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION hvector_send(hvector) RETURNS bytea
	AS 'MODULE_PATHNAME', 'hvector_send'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

-- 完成 vector 类型定义
CREATE TYPE hvector (
	INPUT     = hvector_in,
	OUTPUT    = hvector_out,
	TYPMOD_IN = hvector_typmod_in,
	RECEIVE   = hvector_recv,
	SEND      = hvector_send,
	STORAGE   = external
);

-- ============================================================================
-- hvector 工具函数
-- ============================================================================

CREATE FUNCTION hvector_dims(hvector) RETURNS integer
	AS 'MODULE_PATHNAME', 'hvector_dims'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_dims(hvector) IS 
	'Returns the number of dimensions of a vector';

CREATE FUNCTION hvector_norm(hvector) RETURNS float8
	AS 'MODULE_PATHNAME', 'hvector_norm'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_norm(hvector) IS 
	'Returns the L2 norm (Euclidean length) of a vector';

-- 创建向量 L2 归一化函数
CREATE OR REPLACE FUNCTION hvector_l2_normalize(hvector) RETURNS hvector
	AS 'MODULE_PATHNAME', 'hvector_l2_normalize'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_l2_normalize(hvector) IS 
	'Normalize a vector to unit length using L2 norm';

CREATE FUNCTION hvector_binary_quantize(hvector) RETURNS bit
	AS 'MODULE_PATHNAME', 'hvector_binary_quantize'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_binary_quantize(hvector) IS 
	'Binary quantize a vector';

CREATE FUNCTION hvector_subvector(hvector, integer, integer) RETURNS hvector
	AS 'MODULE_PATHNAME', 'hvector_subvector'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_subvector(hvector, integer, integer) IS 
	'Get a subvector of a vector';

-- ============================================================================
-- hvector 距离函数
-- ============================================================================

CREATE FUNCTION hvector_l2_distance(hvector, hvector) RETURNS float8
	AS 'MODULE_PATHNAME', 'hvector_l2_distance'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_l2_distance(hvector, hvector) IS 
	'Returns the L2 distance (Euclidean distance) between two vectors';

CREATE FUNCTION hvector_l2_squared_distance(hvector, hvector) RETURNS float8
	AS 'MODULE_PATHNAME', 'hvector_l2_squared_distance'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_l2_squared_distance(hvector, hvector) IS 
	'Returns the squared L2 distance between two vectors (faster than l2_distance)';

CREATE FUNCTION hvector_cosine_distance(hvector, hvector) RETURNS float8
	AS 'MODULE_PATHNAME', 'hvector_cosine_distance'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_cosine_distance(hvector, hvector) IS 
	'Returns the cosine distance between two vectors';

CREATE FUNCTION hvector_l1_distance(hvector, hvector) RETURNS float8
	AS 'MODULE_PATHNAME', 'hvector_l1_distance'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_l1_distance(hvector, hvector) IS 
	'Returns the L1 distance between two vectors';

-- ============================================================================
-- hvector 数值计算函数
-- ============================================================================

CREATE FUNCTION hvector_inner_product(hvector, hvector) RETURNS float8
	AS 'MODULE_PATHNAME', 'hvector_inner_product'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_inner_product(hvector, hvector) IS 
	'Returns the inner product of two vectors';



-- ============================================================================
-- hvector 操作符
-- ============================================================================

CREATE OPERATOR <-> (
	LEFTARG = hvector,
	RIGHTARG = hvector,
	PROCEDURE = hvector_l2_distance,
	COMMUTATOR = '<->'
);

COMMENT ON OPERATOR <->(hvector, hvector) IS 
	'Returns the L2 distance between two vectors';

-- ============================================================================
-- 访问方法定义
-- ============================================================================

-- 创建访问方法处理函数
CREATE FUNCTION pg_hybrid_ivfflat_handler(internal) RETURNS index_am_handler
	AS 'MODULE_PATHNAME', 'pg_hybrid_ivfflat_handler'
	LANGUAGE C STRICT;

-- 创建访问方法
CREATE ACCESS METHOD pg_hybrid_ivfflat 
	TYPE INDEX 
	HANDLER pg_hybrid_ivfflat_handler;

COMMENT ON ACCESS METHOD pg_hybrid_ivfflat IS 
	'IVFFlat (Inverted File with Flat compression) index access method for vector similarity search';



-- ============================================================================
-- 操作符类
-- ============================================================================

CREATE OPERATOR CLASS hvector_l2_ops
	DEFAULT FOR TYPE hvector USING pg_hybrid_ivfflat AS
	OPERATOR 1 <-> (hvector, hvector) FOR ORDER BY float_ops,
	FUNCTION 1 hvector_l2_squared_distance(hvector, hvector),
	FUNCTION 3 hvector_l2_distance(hvector, hvector);
