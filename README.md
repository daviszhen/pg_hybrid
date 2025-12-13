# pg_hybrid

PostgreSQL 16 extension providing IVFFlat index access method for vector similarity search.

## 功能特性

- **IVFFlat 索引访问方法** (`pg_hybrid_ivfflat`): 
- 单向量函数: 

`hvector_in`
`hvector_out`
`hvector_typmod_in`
`hvector_recv`
`hvector_send`
`hvector_dims`
`hvector_norm`
`hvector_l2_normalize`

- 双向量函数: 

`hvector_l2_distance`
`hvector_l2_squared_distance`

- 向量操作符: `<->`
- 向量索引: `pg_hybrid_ivfflat`
- 向量索引选项: `lists`
- 向量索引配置参数: `ivfflat.probes`

## 编译和安装

- PostgreSQL 16 开发包（包含头文件和 pg_config）
- GCC 或 Clang 编译器
- Make

### 1. 编译 Extension

```bash
make
```

### 2. 安装到 PostgreSQL

```bash
make install
```

**注意**：Makefile 默认使用 `pg_config`。如果您的 PostgreSQL 16 安装在不同的路径，请修改 `Makefile` 中的 `PG_CONFIG` 变量。


### 3. 在数据库中创建 Extension

```sql
DROP EXTENSION IF EXISTS pg_hybrid CASCADE;

-- 然后安装 pg_hybrid 扩展
CREATE EXTENSION pg_hybrid;
```

### 4. 验证安装

检查访问方法是否创建成功：

```sql
select * from pg_am;
select * from pg_extension;

-- 查看访问方法
SELECT amname, amhandler FROM pg_am WHERE amname = 'pg_hybrid_ivfflat';

```

### 5. 卸载

```sql
DROP EXTENSION IF EXISTS pg_hybrid CASCADE;
```

然后删除安装的文件：

```bash
sudo make uninstall
```

## PostgreSQL 16 数据库管理

### 初始化数据库

如果还没有初始化 PostgreSQL 16 数据目录，需要先初始化：

```bash
mkdir -p ~/pg16_data
initdb -D ~/pg16_data -U postgres --locale=C --encoding=UTF8
```

### 启动数据库

```bash
pg_ctl -D ~/pg16_data -l ~/pg16_data/logfile start
```

### 停止数据库

```bash
pg_ctl -D ~/pg16_data stop
```

### 重启数据库

```bash
pg_ctl -D ~/pg16_data restart
```

### 查看数据库状态

```bash
pg_ctl -D ~/pg16_data status
```

### 连接数据库

```bash
psql -d postgres -U postgres
```

### 查看日志

```bash
tail -f ~/pg16_data/logfile
```

**注意**：默认数据目录为 `~/pg16_data`，默认端口为 `5432`。如果您的配置不同，请相应调整命令中的路径和端口。


## 使用示例

### 创建向量表和索引

```sql
DROP TABLE IF EXISTS items;

-- 创建包含向量列的表
CREATE TABLE items (
    id bigserial PRIMARY KEY,
    embedding hvector(5)
);

-- 生成 1 万行，每行一个 5 维随机向量
INSERT INTO items (embedding) 
SELECT 
    ('[' || 
     string_agg((random() * 10)::text, ',') || 
     ']')::hvector(5) AS embedding
FROM generate_series(1, 10000) AS row_num
CROSS JOIN generate_series(1, 5) AS dim
GROUP BY row_num;

select * from items;

-- 向量相似度搜索（使用 L2 距离）
SELECT * FROM items 
ORDER BY embedding <-> '[1,2,3,4,5]'::hvector 
LIMIT 10;

-- 使用 pg_hybrid_ivfflat 访问方法创建 IVFFlat 索引
CREATE INDEX ON items USING pg_hybrid_ivfflat (embedding hvector_l2_ops)
WITH (lists = 100);

SELECT * FROM items 
ORDER BY embedding <-> '[1,2,3,4,5]'::hvector 
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
  SELECT * FROM items ORDER BY embedding <-> '[1,2,3]'::hvector LIMIT 5;
  ```


## 许可证

Apache License 2.0
