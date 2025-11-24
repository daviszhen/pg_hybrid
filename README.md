# pg_hybrid

PostgreSQL 16 extension for columnar storage engine.

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
/home/pengzhen/pg16/bin/psql -U postgres -d postgres
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

连接到 PostgreSQL 数据库后执行：

```sql
CREATE EXTENSION pg_hybrid;
```

### 4. 验证安装

```sql
SELECT pg_hybrid_version();
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
