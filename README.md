# pg_hybrid

PostgreSQL 16 extension providing IVFFlat index access method for vector similarity search.

## 功能特性

- **IVFFlat 索引访问方法** (`pg_hybrid_ivfflat`): 支持向量相似度搜索的倒排文件索引
- **向量归一化函数**: `vector_l2_normalize()` - L2 归一化函数
- **操作符类支持**: 与 pgvector 扩展兼容，支持 L2 距离操作符 `<->`

## 依赖要求

- PostgreSQL 16+
- **vector 类型**: 需要先安装 [pgvector](https://github.com/pgvector/pgvector) 扩展，或确保 vector 类型已存在

## 项目结构

```
pg_hybrid/
├── Makefile              # 编译配置文件
├── pg_hybrid.control     # Extension 控制文件
├── pg_hybrid--1.0.sql    # SQL 安装脚本
├── src/                  # C 源代码目录
│   └── pg_hybrid.c       # 主源代码文件
├── LICENSE               # Apache License 2.0
└── README.md             # 本文件
```

## 编译要求

- PostgreSQL 16 开发包（包含头文件和 pg_config）
- GCC 或 Clang 编译器
- Make

**注意**：Makefile 默认使用 `/home/pengzhen/pg16/bin/pg_config`。如果您的 PostgreSQL 16 安装在不同的路径，请修改 `Makefile` 中的 `PG_CONFIG` 变量。

## PostgreSQL 16 数据库管理

### 初始化数据库

如果还没有初始化 PostgreSQL 16 数据目录，需要先初始化：

```bash
mkdir -p ~/pg16_data
/home/pengzhen/pg16/bin/initdb -D ~/pg16_data -U postgres --locale=C --encoding=UTF8
```

### 启动数据库

```bash
/home/pengzhen/pg16/bin/pg_ctl -D ~/pg16_data -l ~/pg16_data/logfile start
```

### 停止数据库

```bash
/home/pengzhen/pg16/bin/pg_ctl -D ~/pg16_data stop
```

### 重启数据库

```bash
/home/pengzhen/pg16/bin/pg_ctl -D ~/pg16_data restart
```

### 查看数据库状态

```bash
/home/pengzhen/pg16/bin/pg_ctl -D ~/pg16_data status
```

### 连接数据库

```bash
/home/pengzhen/pg16/bin/psql -d postgres
```

### 查看日志

```bash
tail -f ~/pg16_data/logfile
```

**注意**：默认数据目录为 `~/pg16_data`，默认端口为 `5432`。如果您的配置不同，请相应调整命令中的路径和端口。

## 编译和安装

### 1. 编译 Extension

```bash
make
```

### 2. 安装到 PostgreSQL

```bash
sudo make install
```

### 3. 在数据库中创建 Extension

**重要**: 在创建 pg_hybrid 扩展之前，需要先安装 pgvector 扩展：

```sql
-- 然后安装 pg_hybrid 扩展
CREATE EXTENSION pg_hybrid;
```

### 4. 验证安装

检查访问方法是否创建成功：

```sql
-- 查看访问方法
SELECT amname, amhandler FROM pg_am WHERE amname = 'pg_hybrid_ivfflat';

-- 查看操作符类
SELECT opcname, opcmethod::regclass 
FROM pg_opclass 
WHERE opcmethod = (SELECT oid FROM pg_am WHERE amname = 'pg_hybrid_ivfflat');
```

## 使用示例

### 创建向量表和索引

```sql
-- 创建包含向量列的表
CREATE TABLE items (
    id bigserial PRIMARY KEY,
    embedding pg_hybrid_vector(128)
);

-- 使用 pg_hybrid_ivfflat 访问方法创建 IVFFlat 索引
CREATE INDEX ON items USING pg_hybrid_ivfflat (embedding pg_hybrid_vector_l2_ops)
WITH (lists = 100);

-- 插入向量数据
INSERT INTO items (embedding) VALUES 
    ('[1,2,3,4,5]'::pg_hybrid_vector),
    ('[6,7,8,9,10]'::pg_hybrid_vector);

-- 向量相似度搜索（使用 L2 距离）
SELECT * FROM items 
ORDER BY embedding <-> '[1,2,3,4,5]'::pg_hybrid_vector 
LIMIT 10;
```

### 索引选项

- `lists`: 倒排列表的数量（默认: 100，范围: 1-32768）
  ```sql
  CREATE INDEX idx_embedding ON items USING pg_hybrid_ivfflat (embedding)
  WITH (lists = 200);
  ```

### 配置参数

- `ivfflat.probes`: 设置查询时探测的列表数量（默认: 1）
  ```sql
  SET ivfflat.probes = 10;
  SELECT * FROM items ORDER BY embedding <-> '[1,2,3]'::pg_hybrid_vector LIMIT 5;
  ```

## Cursor/VSCode配置

```
/*
 * PostgreSQL 头文件包含路径说明：
 * - postgres.h: PostgreSQL 核心头文件
 *   路径: /home/pengzhen/pg16/include/postgresql/server/postgres.h
 * - fmgr.h: 函数管理器头文件
 *   路径: /home/pengzhen/pg16/include/postgresql/server/fmgr.h
 * - utils/builtins.h: 内置函数工具头文件
 *   路径: /home/pengzhen/pg16/include/postgresql/server/utils/builtins.h
 *
 * 编译时通过 Makefile 中的 -I 选项指定包含路径：
 * -I/home/pengzhen/pg16/include/postgresql/server
 * -I/home/pengzhen/pg16/include/postgresql/internal
 */
```

## 开发

### 清理编译文件

```bash
make clean
```

### 完全清理

```bash
make distclean
```

## 测试用例

我们提供了两套测试用例来验证 pg_hybrid 的功能：

**注意**：测试脚本只验证 pg_hybrid 扩展自身实现的功能，不依赖 pgvector 或其他扩展。

### 快速测试（推荐首次使用）

快速测试脚本 (`quick_test.sql`) 用于快速验证扩展基本功能：

```bash
# 运行快速测试
psql -d postgres -f quick_test.sql
```

或者在 psql 中：

```sql
\i quick_test.sql
```

快速测试包括：
- ✅ 扩展安装验证
- ✅ 访问方法检查
- ✅ 处理函数验证
- ✅ 向量归一化函数检查
- ✅ 配置参数测试
- ✅ 功能缺失检查

### 完整测试套件

完整测试脚本 (`test_pg_hybrid.sql`) 包含全面的测试场景：

```bash
# 运行完整测试
psql -d postgres -f test_pg_hybrid.sql
```

或者在 psql 中：

```sql
\i test_pg_hybrid.sql
```

### 测试覆盖范围

测试套件包含以下测试场景：

#### 1. 扩展安装验证
- 检查 pg_hybrid 扩展是否正确安装
- 验证扩展版本信息

```sql
SELECT extname, extversion FROM pg_extension WHERE extname = 'pg_hybrid';
```

#### 2. 访问方法验证
- 检查 `pg_hybrid_ivfflat` 访问方法是否存在
- 验证访问方法处理函数

```sql
SELECT amname, amhandler::regproc 
FROM pg_am 
WHERE amname = 'pg_hybrid_ivfflat';
```

#### 3. 函数验证
- 检查 `ivfflat_handler` 处理函数
- 检查 `vector_l2_normalize` 归一化函数

```sql
SELECT proname, pg_get_function_identity_arguments(oid) 
FROM pg_proc 
WHERE proname IN ('ivfflat_handler', 'vector_l2_normalize');
```

#### 4. 配置参数测试
- 检查 `ivfflat.probes` 配置参数
- 测试参数设置功能

```sql
SHOW ivfflat.probes;
SET ivfflat.probes = 10;
```

#### 5. 索引接口验证
- 验证索引操作接口是否注册
- 检查索引选项支持

#### 6. 操作符类检查（如果存在）
- 检查操作符类是否创建
- 验证操作符类关联的函数

#### 7. 功能缺失检查
- 检查 vector 类型是否存在
- 检查距离函数是否存在
- 检查距离操作符是否存在
- 检查操作符类是否存在

### pg_hybrid 已实现功能

测试会验证以下已实现的功能：

- ✅ **访问方法**: `pg_hybrid_ivfflat`
- ✅ **处理函数**: `ivfflat_handler`
- ✅ **归一化函数**: `vector_l2_normalize`
- ✅ **索引接口**: build/insert/delete/scan/vacuum
- ✅ **索引选项**: `lists` 参数
- ✅ **配置参数**: `ivfflat.probes`

### 需要补充的功能

测试会检查并提示以下缺失的功能：

- ⚠ **vector 类型**: 需要定义向量数据类型
- ⚠ **距离函数**: 需要实现 `l2_distance` 或 `vector_l2_squared_distance`
- ⚠ **距离操作符**: 需要创建操作符 `<-> (vector, vector)`
- ⚠ **操作符类**: 需要创建 `vector_l2_ops` 操作符类

### 预期测试结果

所有测试应该：
- ✅ 扩展正确安装
- ✅ 访问方法存在
- ✅ 函数正确注册
- ✅ 配置参数可用
- ⚠ 缺失功能会被明确标注

### 故障排查

如果测试失败，请检查：

1. **扩展未安装**
   ```sql
   CREATE EXTENSION pg_hybrid;
   ```

2. **功能缺失**
   - 查看测试输出的"功能缺失检查"部分
   - 根据提示补充缺失的功能

3. **配置参数问题**
   ```sql
   -- 检查参数是否存在
   SELECT * FROM pg_settings WHERE name = 'ivfflat.probes';
   ```

## 卸载

```sql
DROP EXTENSION pg_hybrid CASCADE;
```

然后删除安装的文件：

```bash
sudo make uninstall
```

## 许可证

Apache License 2.0
