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

CREATE FUNCTION hvector_concat(hvector, hvector) RETURNS hvector
	AS 'MODULE_PATHNAME', 'hvector_concat'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_concat(hvector, hvector) IS 
	'Concatenate two vectors';

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

CREATE FUNCTION hvector_spherical_distance(hvector, hvector) RETURNS float8
	AS 'MODULE_PATHNAME', 'hvector_spherical_distance'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_spherical_distance(hvector, hvector) IS 
	'Returns the spherical distance between two vectors';
-- ============================================================================
-- hvector 数值计算函数
-- ============================================================================

CREATE FUNCTION hvector_inner_product(hvector, hvector) RETURNS float8
	AS 'MODULE_PATHNAME', 'hvector_inner_product'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_inner_product(hvector, hvector) IS 
	'Returns the inner product of two vectors';

CREATE FUNCTION hvector_add(hvector, hvector) RETURNS hvector
	AS 'MODULE_PATHNAME', 'hvector_add'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_add(hvector, hvector) IS 
	'Add two vectors';

CREATE FUNCTION hvector_sub(hvector, hvector) RETURNS hvector
	AS 'MODULE_PATHNAME', 'hvector_sub'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_sub(hvector, hvector) IS 
	'Subtract two vectors';

CREATE FUNCTION hvector_mul(hvector, hvector) RETURNS hvector
	AS 'MODULE_PATHNAME', 'hvector_mul'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_mul(hvector, hvector) IS 
	'Multiply two vectors';

CREATE FUNCTION hvector_lt(hvector, hvector) RETURNS bool
	AS 'MODULE_PATHNAME', 'hvector_lt'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_lt(hvector, hvector) IS 
	'Less than two vectors';

CREATE FUNCTION hvector_le(hvector, hvector) RETURNS bool
	AS 'MODULE_PATHNAME', 'hvector_le'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_le(hvector, hvector) IS 
	'Less than or equal to two vectors';

CREATE FUNCTION hvector_eq(hvector, hvector) RETURNS bool
	AS 'MODULE_PATHNAME', 'hvector_eq'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_eq(hvector, hvector) IS 
	'Equal to two vectors';

CREATE FUNCTION hvector_ne(hvector, hvector) RETURNS bool
	AS 'MODULE_PATHNAME', 'hvector_ne'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_ne(hvector, hvector) IS 
	'Not equal to two vectors';

CREATE FUNCTION hvector_ge(hvector, hvector) RETURNS bool
	AS 'MODULE_PATHNAME', 'hvector_ge'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_ge(hvector, hvector) IS 
	'Greater than or equal to two vectors';

CREATE FUNCTION hvector_gt(hvector, hvector) RETURNS bool
	AS 'MODULE_PATHNAME', 'hvector_gt'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_gt(hvector, hvector) IS 
	'Greater than two vectors';

CREATE FUNCTION hvector_cmp(hvector, hvector) RETURNS int4
	AS 'MODULE_PATHNAME', 'hvector_cmp'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_cmp(hvector, hvector) IS 
	'Compare two vectors';

CREATE FUNCTION hvector_negative_inner_product(hvector, hvector) RETURNS float8
	AS 'MODULE_PATHNAME', 'hvector_negative_inner_product'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_negative_inner_product(hvector, hvector) IS 
	'Returns the negative inner product of two vectors';
-- ============================================================================
-- hvector 聚合函数
-- ============================================================================

CREATE FUNCTION hvector_accum(double precision[], hvector) RETURNS double precision[]
	AS 'MODULE_PATHNAME', 'hvector_accum'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_accum(double precision[], hvector) IS 
	'Accumulate a vector into a state array';

CREATE FUNCTION hvector_combine(double precision[], double precision[]) RETURNS double precision[]
	AS 'MODULE_PATHNAME', 'hvector_combine'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_combine(double precision[], double precision[]) IS 
	'Combine two state arrays';

CREATE FUNCTION hvector_avg(double precision[]) RETURNS hvector
	AS 'MODULE_PATHNAME', 'hvector_avg'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_avg(double precision[]) IS 
	'Average a state array';

CREATE AGGREGATE avg(hvector) (
	SFUNC = hvector_accum,
	STYPE = double precision[],
	FINALFUNC = hvector_avg,
	COMBINEFUNC = hvector_combine,
	INITCOND = '{0}',
	PARALLEL = SAFE
);

CREATE AGGREGATE sum(hvector) (
	SFUNC = hvector_add,
	STYPE = hvector,
	COMBINEFUNC = hvector_add,
	PARALLEL = SAFE
);

-- ============================================================================
-- hvector 转换函数
-- ============================================================================

-- hvector -> hvector
CREATE FUNCTION hvector(hvector, integer, boolean) RETURNS hvector
	AS 'MODULE_PATHNAME', 'hvector'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector(hvector, integer, boolean) IS 
	'Convert a vector to a vector';

CREATE CAST (hvector AS hvector)
	WITH FUNCTION hvector(hvector, integer, boolean) AS IMPLICIT;

-- integer[] -> hvector
CREATE FUNCTION array_to_hvector(integer[], integer, boolean) RETURNS hvector
	AS 'MODULE_PATHNAME', 'array_to_hvector'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION array_to_hvector(integer[], integer, boolean) IS 
	'Convert a integer array to a vector';

CREATE CAST (integer[] AS hvector)
	WITH FUNCTION array_to_hvector(integer[], integer, boolean) AS ASSIGNMENT;

-- real[] -> hvector

CREATE FUNCTION array_to_hvector(real[], integer, boolean) RETURNS hvector
	AS 'MODULE_PATHNAME', 'array_to_hvector'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION array_to_hvector(real[], integer, boolean) IS 
	'Convert a real array to a vector';

CREATE CAST (real[] AS hvector)
	WITH FUNCTION array_to_hvector(real[], integer, boolean) AS ASSIGNMENT;

-- double precision[] -> hvector

CREATE FUNCTION array_to_hvector(double precision[], integer, boolean) RETURNS hvector
	AS 'MODULE_PATHNAME', 'array_to_hvector'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION array_to_hvector(double precision[], integer, boolean) IS 
	'Convert a double precision array to a vector';

CREATE CAST (double precision[] AS hvector)
	WITH FUNCTION array_to_hvector(double precision[], integer, boolean) AS ASSIGNMENT;

-- numeric[] -> hvector

CREATE FUNCTION array_to_hvector(numeric[], integer, boolean) RETURNS hvector
	AS 'MODULE_PATHNAME', 'array_to_hvector'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION array_to_hvector(numeric[], integer, boolean) IS 
	'Convert a numeric array to a vector';

CREATE CAST (numeric[] AS hvector)
	WITH FUNCTION array_to_hvector(numeric[], integer, boolean) AS ASSIGNMENT;

-- hvector -> float4[]
CREATE FUNCTION hvector_to_float4(hvector, integer, boolean) RETURNS real[]
	AS 'MODULE_PATHNAME', 'hvector_to_float4'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION hvector_to_float4(hvector, integer, boolean) IS 
	'Convert a vector to a float4 array';

CREATE CAST (hvector AS real[])
	WITH FUNCTION hvector_to_float4(hvector, integer, boolean) AS IMPLICIT;
-- ============================================================================
-- hvector 运算符
-- ============================================================================


CREATE OPERATOR + (
	LEFTARG = hvector, RIGHTARG = hvector, PROCEDURE = hvector_add,
	COMMUTATOR = +
);

CREATE OPERATOR - (
	LEFTARG = hvector, RIGHTARG = hvector, PROCEDURE = hvector_sub
);

CREATE OPERATOR * (
	LEFTARG = hvector, RIGHTARG = hvector, PROCEDURE = hvector_mul,
	COMMUTATOR = *
);

CREATE OPERATOR || (
	LEFTARG = hvector, RIGHTARG = hvector, PROCEDURE = hvector_concat
);

CREATE OPERATOR < (
	LEFTARG = hvector, RIGHTARG = hvector, PROCEDURE = hvector_lt,
	COMMUTATOR = > , NEGATOR = >= ,
	RESTRICT = scalarltsel, JOIN = scalarltjoinsel
);

CREATE OPERATOR <= (
	LEFTARG = hvector, RIGHTARG = hvector, PROCEDURE = hvector_le,
	COMMUTATOR = >= , NEGATOR = > ,
	RESTRICT = scalarlesel, JOIN = scalarlejoinsel
);

CREATE OPERATOR = (
	LEFTARG = hvector, RIGHTARG = hvector, PROCEDURE = hvector_eq,
	COMMUTATOR = = , NEGATOR = <> ,
	RESTRICT = eqsel, JOIN = eqjoinsel
);

CREATE OPERATOR <> (
	LEFTARG = hvector, RIGHTARG = hvector, PROCEDURE = hvector_ne,
	COMMUTATOR = <> , NEGATOR = = ,
	RESTRICT = eqsel, JOIN = eqjoinsel
);

CREATE OPERATOR >= (
	LEFTARG = hvector, RIGHTARG = hvector, PROCEDURE = hvector_ge,
	COMMUTATOR = <= , NEGATOR = < ,
	RESTRICT = scalargesel, JOIN = scalargejoinsel
);

CREATE OPERATOR > (
	LEFTARG = hvector, RIGHTARG = hvector, PROCEDURE = hvector_gt,
	COMMUTATOR = < , NEGATOR = <= ,
	RESTRICT = scalargtsel, JOIN = scalargtjoinsel
);

-- hvector 距离运算符

CREATE OPERATOR <-> (
	LEFTARG = hvector,
	RIGHTARG = hvector,
	PROCEDURE = hvector_l2_distance,
	COMMUTATOR = '<->'
);

COMMENT ON OPERATOR <->(hvector, hvector) IS 
	'Returns the L2 distance between two vectors';

CREATE OPERATOR <#> (
	LEFTARG = hvector, 
    RIGHTARG = hvector, 
    PROCEDURE = hvector_negative_inner_product,
	COMMUTATOR = '<#>'
);

CREATE OPERATOR <=> (
	LEFTARG = hvector, 
    RIGHTARG = hvector, 
    PROCEDURE = hvector_cosine_distance,
	COMMUTATOR = '<=>'
);

CREATE OPERATOR <+> (
	LEFTARG = hvector, 
    RIGHTARG = hvector, 
    PROCEDURE = hvector_l1_distance,
	COMMUTATOR = '<+>'
);

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

CREATE OPERATOR CLASS hvector_ops
	DEFAULT FOR TYPE hvector USING btree AS
	OPERATOR 1 < ,
	OPERATOR 2 <= ,
	OPERATOR 3 = ,
	OPERATOR 4 >= ,
	OPERATOR 5 > ,
	FUNCTION 1 hvector_cmp(hvector, hvector);

-- l2距离
CREATE OPERATOR CLASS hvector_l2_ops
	DEFAULT FOR TYPE hvector USING pg_hybrid_ivfflat AS
	OPERATOR 1 <-> (hvector, hvector) FOR ORDER BY float_ops,
	FUNCTION 1 hvector_l2_squared_distance(hvector, hvector),
	FUNCTION 3 hvector_l2_distance(hvector, hvector);

-- 内积. 考虑向量大小和方向
CREATE OPERATOR CLASS hvector_ip_ops
	FOR TYPE hvector USING pg_hybrid_ivfflat AS
	OPERATOR 1 <#> (hvector, hvector) FOR ORDER BY float_ops,
	FUNCTION 1 hvector_negative_inner_product(hvector, hvector),
	FUNCTION 3 hvector_spherical_distance(hvector, hvector),
	FUNCTION 4 hvector_norm(hvector);

-- 余弦. 考虑向量方向
CREATE OPERATOR CLASS hvector_cosine_ops
	FOR TYPE hvector USING pg_hybrid_ivfflat AS
	OPERATOR 1 <=> (hvector, hvector) FOR ORDER BY float_ops,
	FUNCTION 1 hvector_negative_inner_product(hvector, hvector),
	FUNCTION 2 hvector_norm(hvector),
	FUNCTION 3 hvector_spherical_distance(hvector, hvector),
	FUNCTION 4 hvector_norm(hvector);