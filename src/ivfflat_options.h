#ifndef IVFFLAT_OPTIONS_H
#define IVFFLAT_OPTIONS_H

#include "c.h"

#define IVFFLAT_DEFAULT_PROBES 1
#define IVFFLAT_DEFAULT_LIST_COUNT 100
#define IVFFLAT_MIN_LIST_COUNT 1
#define IVFFLAT_MAX_LIST_COUNT 32768

typedef struct IvfflatOptions {
    int32 vl_len_;
    int list_count;
} IvfflatOptions;

typedef enum IvfflatIterativeScanMode
{
	IVFFLAT_ITERATIVE_SCAN_OFF,
	IVFFLAT_ITERATIVE_SCAN_RELAXED
}	IvfflatIterativeScanMode;

void ivfflat_init_options(void);
#endif