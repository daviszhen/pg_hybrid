# PostgreSQL extension Makefile
# 需要 PostgreSQL 16 开发头文件

MODULE_big = pg_hybrid
OBJS = src/pg_hybrid.o src/ivffat.o src/ivfflat_build.o src/ivfflat_page.o src/vector.o src/ivfflat_insert.o src/ivfflat_delete.o src/ivfflat_options.o src/ivfflat_scan.o
EXTENSION = pg_hybrid
DATA = pg_hybrid--1.0.sql
PGFILEDESC = "pg_hybrid - columnar storage engine"

# PostgreSQL 配置
# 使用 PostgreSQL 16 的 pg_config（如果存在），否则使用系统的
PG_CONFIG := $(shell test -x /home/pengzhen/pg16/bin/pg_config && echo /home/pengzhen/pg16/bin/pg_config || echo pg_config)
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# 编译选项
PG_CPPFLAGS = -std=c11 -Wall -Wextra -Wno-unused-parameter
SHLIB_LINK =


