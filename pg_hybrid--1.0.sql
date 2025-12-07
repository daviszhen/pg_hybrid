-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_hybrid" to load this file. \quit

-- pg_hybrid extension SQL script
-- 版本 1.0
-- 
-- pg_hybrid - Columnar storage engine with IVFFlat index support
-- 
-- 包含完整的 vector 类型实现，不依赖 pgvector 扩展

-- ============================================================================
-- Vector 类型定义
-- ============================================================================

-- 创建 vector 类型（先声明，后定义）
CREATE TYPE pg_hybrid_vector;

-- 创建输入输出函数
CREATE FUNCTION pg_hybrid_vector_in(cstring, oid, integer) RETURNS pg_hybrid_vector
	AS 'MODULE_PATHNAME', 'pg_hybrid_vector_in'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION pg_hybrid_vector_out(pg_hybrid_vector) RETURNS cstring
	AS 'MODULE_PATHNAME', 'pg_hybrid_vector_out'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION pg_hybrid_vector_typmod_in(cstring[]) RETURNS integer
	AS 'MODULE_PATHNAME', 'pg_hybrid_vector_typmod_in'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION pg_hybrid_vector_recv(internal, oid, integer) RETURNS pg_hybrid_vector
	AS 'MODULE_PATHNAME', 'pg_hybrid_vector_recv'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION pg_hybrid_vector_send(pg_hybrid_vector) RETURNS bytea
	AS 'MODULE_PATHNAME', 'pg_hybrid_vector_send'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

-- 完成 vector 类型定义
CREATE TYPE pg_hybrid_vector (
	INPUT     = pg_hybrid_vector_in,
	OUTPUT    = pg_hybrid_vector_out,
	TYPMOD_IN = pg_hybrid_vector_typmod_in,
	RECEIVE   = pg_hybrid_vector_recv,
	SEND      = pg_hybrid_vector_send,
	STORAGE   = external
);

-- ============================================================================
-- Vector 工具函数
-- ============================================================================

CREATE FUNCTION pg_hybrid_vector_dims(pg_hybrid_vector) RETURNS integer
	AS 'MODULE_PATHNAME', 'pg_hybrid_vector_dims'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION pg_hybrid_vector_dims(pg_hybrid_vector) IS 
	'Returns the number of dimensions of a vector';

CREATE FUNCTION pg_hybrid_vector_norm(pg_hybrid_vector) RETURNS float8
	AS 'MODULE_PATHNAME', 'pg_hybrid_vector_norm'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION pg_hybrid_vector_norm(pg_hybrid_vector) IS 
	'Returns the L2 norm (Euclidean length) of a vector';

-- ============================================================================
-- Vector 距离函数
-- ============================================================================

CREATE FUNCTION pg_hybrid_l2_distance(pg_hybrid_vector, pg_hybrid_vector) RETURNS float8
	AS 'MODULE_PATHNAME', 'pg_hybrid_l2_distance'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION pg_hybrid_l2_distance(pg_hybrid_vector, pg_hybrid_vector) IS 
	'Returns the L2 distance (Euclidean distance) between two vectors';

CREATE FUNCTION pg_hybrid_vector_l2_squared_distance(pg_hybrid_vector, pg_hybrid_vector) RETURNS float8
	AS 'MODULE_PATHNAME', 'pg_hybrid_vector_l2_squared_distance'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION pg_hybrid_vector_l2_squared_distance(pg_hybrid_vector, pg_hybrid_vector) IS 
	'Returns the squared L2 distance between two vectors (faster than l2_distance)';

-- ============================================================================
-- Vector 操作符
-- ============================================================================

CREATE OPERATOR <-> (
	LEFTARG = pg_hybrid_vector,
	RIGHTARG = pg_hybrid_vector,
	PROCEDURE = pg_hybrid_l2_distance,
	COMMUTATOR = '<->'
);

COMMENT ON OPERATOR <->(pg_hybrid_vector, pg_hybrid_vector) IS 
	'Returns the L2 distance between two vectors';

-- ============================================================================
-- 访问方法定义
-- ============================================================================

-- 创建访问方法处理函数
CREATE FUNCTION ivfflat_handler(internal) RETURNS index_am_handler
	AS 'MODULE_PATHNAME', 'ivfflat_handler'
	LANGUAGE C STRICT;

-- 创建访问方法
CREATE ACCESS METHOD pg_hybrid_ivfflat 
	TYPE INDEX 
	HANDLER ivfflat_handler;

COMMENT ON ACCESS METHOD pg_hybrid_ivfflat IS 
	'IVFFlat (Inverted File with Flat compression) index access method for vector similarity search';

-- 创建向量 L2 归一化函数
-- 注意：如果 pgvector 扩展已安装，此函数可能已存在，可以使用 OR REPLACE
CREATE OR REPLACE FUNCTION pg_hybrid_vector_l2_normalize(pg_hybrid_vector) RETURNS pg_hybrid_vector
	AS 'MODULE_PATHNAME', 'pg_hybrid_vector_l2_normalize'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION pg_hybrid_vector_l2_normalize(pg_hybrid_vector) IS 
	'Normalize a vector to unit length using L2 norm';

-- ============================================================================
-- 操作符类定义
-- ============================================================================

-- 创建操作符类
-- 使用 pg_hybrid_ivfflat 访问方法
-- 注意：操作符类需要距离函数（FUNCTION 1）和可选的归一化函数（FUNCTION 2）
DO $$
DECLARE
	squared_distance_oid OID;
	l2_distance_oid OID;
	normalize_oid OID;
	squared_distance_name TEXT;
	l2_distance_name TEXT;
	normalize_name TEXT;
	opclass_exists BOOLEAN;
BEGIN
	-- 检查 pg_hybrid_vector 类型是否存在
	IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'pg_hybrid_vector') THEN
		RAISE EXCEPTION 'pg_hybrid_vector type does not exist';
	END IF;

	-- 检查操作符类是否已存在
	SELECT EXISTS (
		SELECT 1 FROM pg_opclass 
		WHERE opcname = 'pg_hybrid_vector_l2_ops' 
		AND opcmethod = (SELECT oid FROM pg_am WHERE amname = 'pg_hybrid_ivfflat')
	) INTO opclass_exists;

	IF opclass_exists THEN
		RAISE NOTICE 'Operator class pg_hybrid_vector_l2_ops already exists, skipping creation.';
		RETURN;
	END IF;

	-- 查找 vector_l2_squared_distance 函数（FUNCTION 1 - 距离函数）
	-- 注意：不检查返回类型，避免类型缓存问题
	SELECT p.oid, p.proname INTO squared_distance_oid, squared_distance_name
	FROM pg_proc p
	JOIN pg_namespace n ON n.oid = p.pronamespace
	WHERE p.proname = 'pg_hybrid_vector_l2_squared_distance'
	AND p.pronargs = 2
	AND n.nspname = 'public'
	LIMIT 1;

	-- 查找 l2_distance 函数（FUNCTION 3 - 备用距离函数）
	SELECT p.oid, p.proname INTO l2_distance_oid, l2_distance_name
	FROM pg_proc p
	JOIN pg_namespace n ON n.oid = p.pronamespace
	WHERE p.proname = 'pg_hybrid_l2_distance'
	AND p.pronargs = 2
	AND n.nspname = 'public'
	LIMIT 1;

	-- 查找归一化函数（FUNCTION 2）
	SELECT p.oid, p.proname INTO normalize_oid, normalize_name
	FROM pg_proc p
	JOIN pg_namespace n ON n.oid = p.pronamespace
	WHERE p.proname = 'pg_hybrid_vector_l2_normalize'
	AND p.pronargs = 1
	AND n.nspname = 'public'
	LIMIT 1;

	-- 创建操作符类
	IF squared_distance_oid IS NOT NULL THEN
		-- 使用 vector_l2_squared_distance 作为 FUNCTION 1
		IF l2_distance_oid IS NOT NULL AND normalize_oid IS NOT NULL THEN
			-- 完整版本：包含距离函数和归一化函数
			EXECUTE format('
				CREATE OPERATOR CLASS pg_hybrid_vector_l2_ops
					DEFAULT FOR TYPE pg_hybrid_vector USING pg_hybrid_ivfflat AS
					OPERATOR 1 <-> (pg_hybrid_vector, pg_hybrid_vector) FOR ORDER BY float_ops,
					FUNCTION 1 %I(pg_hybrid_vector, pg_hybrid_vector),
					FUNCTION 2 %I(pg_hybrid_vector),
					FUNCTION 3 %I(pg_hybrid_vector, pg_hybrid_vector)',
				squared_distance_name,
				normalize_name,
				l2_distance_name
			);
		ELSIF normalize_oid IS NOT NULL THEN
			-- 只有归一化函数
			EXECUTE format('
				CREATE OPERATOR CLASS pg_hybrid_vector_l2_ops
					DEFAULT FOR TYPE pg_hybrid_vector USING pg_hybrid_ivfflat AS
					OPERATOR 1 <-> (pg_hybrid_vector, pg_hybrid_vector) FOR ORDER BY float_ops,
					FUNCTION 1 %I(pg_hybrid_vector, pg_hybrid_vector),
					FUNCTION 2 %I(pg_hybrid_vector)',
				squared_distance_name,
				normalize_name
			);
		ELSE
			-- 只有距离函数
			EXECUTE format('
				CREATE OPERATOR CLASS pg_hybrid_vector_l2_ops
					DEFAULT FOR TYPE pg_hybrid_vector USING pg_hybrid_ivfflat AS
					OPERATOR 1 <-> (pg_hybrid_vector, pg_hybrid_vector) FOR ORDER BY float_ops,
					FUNCTION 1 %I(pg_hybrid_vector, pg_hybrid_vector)',
				squared_distance_name
			);
		END IF;

		COMMENT ON OPERATOR CLASS pg_hybrid_vector_l2_ops USING pg_hybrid_ivfflat IS
			'IVFFlat operator class for vector L2 distance';
	ELSIF l2_distance_oid IS NOT NULL THEN
		-- 回退：只使用 l2_distance
		RAISE WARNING 'pg_hybrid_vector_l2_squared_distance not found, using pg_hybrid_l2_distance instead. Performance may be suboptimal.';
		EXECUTE format('
			CREATE OPERATOR CLASS pg_hybrid_vector_l2_ops
				DEFAULT FOR TYPE pg_hybrid_vector USING pg_hybrid_ivfflat AS
				OPERATOR 1 <-> (pg_hybrid_vector, pg_hybrid_vector) FOR ORDER BY float_ops,
				FUNCTION 1 %I(pg_hybrid_vector, pg_hybrid_vector)',
			l2_distance_name
		);
	ELSE
		RAISE WARNING 'Neither pg_hybrid_vector_l2_squared_distance nor pg_hybrid_l2_distance found. Operator class not created.';
	END IF;
END $$;

