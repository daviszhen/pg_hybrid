#ifndef VECTOR_H
#define VECTOR_H

#include "c.h"
#include "postgres.h"
#include "fmgr.h"
#include "utils/relcache.h"

#define IVFFLAT_MAX_DIMENSIONS 2000

typedef struct VectorData
{
	int32		vl_len_;		/* varlena header (do not touch directly!) */
	int16		dim;			/* number of dimensions */
	float		data[FLEXIBLE_ARRAY_MEMBER];
}			VectorData;

typedef VectorData * Vector;

#define VECTOR_SIZE(dimensions) \
    (offsetof(VectorData, data) + sizeof(float) * (dimensions))

#define IVFFALT_VECTOR_DISTANCE_PROC 1
#define IVFFALT_VECTOR_NORMALIZATION_PROC 2
#define IVFFALT_KMEANS_DISTANCE_PROC 3
#define IVFFALT_KMEANS_NORMALIZATION_PROC 4
#define IVFFALT_VECTOR_TYPE_PROC 5

typedef struct IvfflatVectorTypeData{
    int max_dimensions;
    Datum (*normalize) (PG_FUNCTION_ARGS);
    Size (*item_size)(int dimensions);
    void (*update_center)(Pointer center, int dimensions, float *temp);
    void (*sum_center) (Pointer v, float *x);
} IvfflatVectorTypeData;

typedef IvfflatVectorTypeData * IvfflatVectorType;


Vector
vector_create(int dimensions);

Size
vector_size(int dimensions);

void
vector_update_center(Pointer center, int dimensions, float *temp);

void
vector_sum_center(Pointer v, float *x);

typedef struct ArrayData{
    int length;
    int max_length;
    int dimensions;
    Size item_size;
    char *data;
} ArrayData;

typedef ArrayData * Array;

Array
array_create(int max_length, int dimensions, Size item_size);

void
array_destroy(Array array);

void
array_append(Array array, void *data);

Pointer
array_get(Array array, int index);

FmgrInfo *
ivfflat_get_proc_info(Relation index, uint16 procnum);

const IvfflatVectorType
ivfflat_get_vector_type(Relation index);

bool
ivfflat_norm_non_zero(FmgrInfo *proc, Oid collation, Datum value);

Datum
ivfflat_normalize_value(const IvfflatVectorType vector_type, Oid collation, Datum value);

#endif