/*
 * pg_hybrid.c
 * PostgreSQL extension for columnar storage engine
 *
 * Copyright (c) 2024
 * Licensed under the Apache License, Version 2.0
 */

#include "pg_hybrid.h"
#include "ivfflat_options.h"


PG_MODULE_MAGIC;

PGDLLEXPORT void _PG_init(void);
void
_PG_init(void)
{
    ivfflat_init_options();
}


