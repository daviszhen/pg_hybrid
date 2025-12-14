#ifndef VECTOR_H
#define VECTOR_H

#include "c.h"
#include "postgres.h"
#include "fmgr.h"
#include "utils/relcache.h"
#include "utils/varbit.h"
#include "varatt.h"

#define IVFFLAT_MAX_DIMENSIONS 2000
#define VECTOR_MAX_DIM IVFFLAT_MAX_DIMENSIONS

/* Macros for accessing vector data */
#define PG_GETARG_VECTOR_P(n) ((Vector) PG_DETOAST_DATUM(PG_GETARG_DATUM(n)))
#define DatumGetVectorP(X) ((Vector) PG_DETOAST_DATUM(X))

typedef struct VectorData
{
	int32		vl_len_;		/* varlena header (do not touch directly!) */
	int16		dim;			/* number of dimensions */
	int16		unused;			/* reserved for future use, always zero */
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

VarBit* 
bitvector_create(int dim);

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

/* Vector type I/O functions */
PGDLLEXPORT Datum hvector_in(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hvector_out(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hvector_typmod_in(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hvector_recv(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hvector_send(PG_FUNCTION_ARGS);

/* Vector utility functions */
PGDLLEXPORT Datum hvector_dims(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hvector_norm(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hvector_l2_normalize(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hvector_binary_quantize(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hvector_subvector(PG_FUNCTION_ARGS);

/* Vector distance functions */
PGDLLEXPORT Datum hvector_l2_distance(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hvector_l2_squared_distance(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hvector_cosine_distance(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hvector_l1_distance(PG_FUNCTION_ARGS);

// 向量数值函数
PGDLLEXPORT Datum hvector_inner_product(PG_FUNCTION_ARGS);

/* Helper functions - defined in vector.c */
float
hvector_inner_product_float(int dim, float *a, float *b);
#endif