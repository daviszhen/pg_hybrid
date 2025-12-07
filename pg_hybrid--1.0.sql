-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_hybrid" to load this file. \quit

-- pg_hybrid extension SQL script
-- 版本 1.0
-- 
-- pg_hybrid - Columnar storage engine with IVFFlat index support
-- 
-- 注意：此扩展需要 vector 类型支持
-- 建议先安装 pgvector 扩展，或确保 vector 类型已存在

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
CREATE OR REPLACE FUNCTION vector_l2_normalize(vector) RETURNS vector
	AS 'MODULE_PATHNAME', 'vector_l2_normalize'
	LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION vector_l2_normalize(vector) IS 
	'Normalize a vector to unit length using L2 norm';

-- 操作符类定义
-- 注意：这些操作符类假设 vector 类型和相关操作符已存在
-- 如果使用 pgvector 扩展，操作符 <-> (l2_distance) 应该已经定义

-- 检查 vector 类型是否存在
DO $$
BEGIN
	IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'vector') THEN
		RAISE EXCEPTION 'vector type does not exist. Please install pgvector extension first or create the vector type.';
	END IF;
END $$;

-- 检查 l2_distance 函数是否存在（通常来自 pgvector）
DO $$
BEGIN
	IF NOT EXISTS (
		SELECT 1 FROM pg_proc p
		JOIN pg_namespace n ON n.oid = p.pronamespace
		WHERE p.proname = 'l2_distance'
		AND p.pronargs = 2
	) THEN
		RAISE WARNING 'l2_distance function not found. Operator class may not work correctly.';
	END IF;
END $$;

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
	-- 检查操作符类是否已存在
	SELECT EXISTS (
		SELECT 1 FROM pg_opclass 
		WHERE opcname = 'vector_l2_ops' 
		AND opcmethod = (SELECT oid FROM pg_am WHERE amname = 'pg_hybrid_ivfflat')
	) INTO opclass_exists;

	IF opclass_exists THEN
		RAISE NOTICE 'Operator class vector_l2_ops already exists, skipping creation.';
		RETURN;
	END IF;

	-- 查找 vector_l2_squared_distance 函数（FUNCTION 1 - 距离函数）
	SELECT p.oid, p.proname INTO squared_distance_oid, squared_distance_name
	FROM pg_proc p
	JOIN pg_namespace n ON n.oid = p.pronamespace
	WHERE p.proname = 'vector_l2_squared_distance'
	AND p.pronargs = 2
	AND p.prorettype = 'float8'::regtype
	LIMIT 1;

	-- 查找 l2_distance 函数（FUNCTION 3 - 备用距离函数）
	SELECT p.oid, p.proname INTO l2_distance_oid, l2_distance_name
	FROM pg_proc p
	JOIN pg_namespace n ON n.oid = p.pronamespace
	WHERE p.proname = 'l2_distance'
	AND p.pronargs = 2
	AND p.prorettype = 'float8'::regtype
	LIMIT 1;

	-- 查找归一化函数（FUNCTION 2）
	SELECT p.oid, p.proname INTO normalize_oid, normalize_name
	FROM pg_proc p
	JOIN pg_namespace n ON n.oid = p.pronamespace
	WHERE p.proname = 'vector_l2_normalize'
	AND p.pronargs = 1
	LIMIT 1;

	-- 创建操作符类
	IF squared_distance_oid IS NOT NULL THEN
		-- 使用 vector_l2_squared_distance 作为 FUNCTION 1
		IF l2_distance_oid IS NOT NULL AND normalize_oid IS NOT NULL THEN
			-- 完整版本：包含距离函数和归一化函数
			EXECUTE format('
				CREATE OPERATOR CLASS vector_l2_ops
					DEFAULT FOR TYPE vector USING pg_hybrid_ivfflat AS
					OPERATOR 1 <-> (vector, vector) FOR ORDER BY float_ops,
					FUNCTION 1 %I(vector, vector),
					FUNCTION 2 %I(vector),
					FUNCTION 3 %I(vector, vector)',
				squared_distance_name,
				normalize_name,
				l2_distance_name
			);
		ELSIF normalize_oid IS NOT NULL THEN
			-- 只有归一化函数
			EXECUTE format('
				CREATE OPERATOR CLASS vector_l2_ops
					DEFAULT FOR TYPE vector USING pg_hybrid_ivfflat AS
					OPERATOR 1 <-> (vector, vector) FOR ORDER BY float_ops,
					FUNCTION 1 %I(vector, vector),
					FUNCTION 2 %I(vector)',
				squared_distance_name,
				normalize_name
			);
		ELSE
			-- 只有距离函数
			EXECUTE format('
				CREATE OPERATOR CLASS vector_l2_ops
					DEFAULT FOR TYPE vector USING pg_hybrid_ivfflat AS
					OPERATOR 1 <-> (vector, vector) FOR ORDER BY float_ops,
					FUNCTION 1 %I(vector, vector)',
				squared_distance_name
			);
		END IF;

		COMMENT ON OPERATOR CLASS vector_l2_ops USING pg_hybrid_ivfflat IS
			'IVFFlat operator class for vector L2 distance';
	ELSIF l2_distance_oid IS NOT NULL THEN
		-- 回退：只使用 l2_distance
		RAISE WARNING 'vector_l2_squared_distance not found, using l2_distance instead. Performance may be suboptimal.';
		EXECUTE format('
			CREATE OPERATOR CLASS vector_l2_ops
				DEFAULT FOR TYPE vector USING pg_hybrid_ivfflat AS
				OPERATOR 1 <-> (vector, vector) FOR ORDER BY float_ops,
				FUNCTION 1 %I(vector, vector)',
			l2_distance_name
		);
	ELSE
		RAISE WARNING 'Neither vector_l2_squared_distance nor l2_distance found. Operator class not created.';
	END IF;
END $$;

