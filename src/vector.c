#include "vector.h"
#include "varatt.h"
#include "fmgr.h"
#include "utils/float.h"

Vector
vector_create(int dimensions){
    int sz = VECTOR_SIZE(dimensions);
    Vector vec = (Vector) palloc0(sz);
    SET_VARSIZE(vec, sz);
    vec->dim = dimensions;
    return vec;
}

Size
vector_size(int dimensions){
    return VECTOR_SIZE(dimensions);
}

void
vector_update_center(Pointer center, int dimensions, float *temp){
    Vector vec = (Vector) center;
    SET_VARSIZE(vec, VECTOR_SIZE(dimensions));
    vec->dim = dimensions;
    for(int i = 0; i < dimensions; i++){
        vec->data[i] = temp[i];
    }
}

void
vector_sum_center(Pointer v, float *x){
    Vector vec = (Vector) v;
    int dim = vec->dim;
    for(int i = 0; i < dim; i++){
        x[i] += vec->data[i];
    }
}

Array
array_create(int max_length, int dimensions, Size item_size){
    Array array = palloc(sizeof(ArrayData));
    array->length = 0;
    array->max_length = max_length;
    array->dimensions = dimensions;
    array->item_size = MAXALIGN(item_size);
    array->data = palloc_extended(
        max_length * item_size, 
        MCXT_ALLOC_ZERO | MCXT_ALLOC_HUGE);
    return array;
}

void
array_destroy(Array array){
    pfree(array->data);
    pfree(array);
}

void
array_append(Array array, void *data){

}

Pointer
array_get(Array array, int index){
    return array->data + index * array->item_size;
}

PGDLLEXPORT PG_FUNCTION_INFO_V1(vector_l2_normalize);
Datum
vector_l2_normalize(PG_FUNCTION_ARGS){
    Vector a;
    double norm = 0.0;
    Vector res;

    a = (Vector)PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
    res = vector_create(a->dim);
    for(int i = 0; i < a->dim; i++){
        norm += a->data[i] * a->data[i];
    }

    norm = sqrt(norm);
    if(norm > 0){
        for(int i = 0; i < a->dim; i++){
            res->data[i] = a->data[i] / norm;
        }
        for(int i = 0; i < a->dim; i++){
            if(isinf(res->data[i])){
                float_overflow_error();
            }
        }
    }
    PG_RETURN_POINTER(res);
}

PGDLLEXPORT Datum vector_l2_normalize(PG_FUNCTION_ARGS);

static const IvfflatVectorTypeData ivfflat_default_vector_type_data = {
    .max_dimensions = IVFFLAT_MAX_DIMENSIONS,
    .normalize = vector_l2_normalize,
    .item_size = vector_size,
    .update_center = vector_update_center,
    .sum_center = vector_sum_center,
};

const IvfflatVectorType
ivfflat_get_vector_type(Relation index){
    FmgrInfo *proc = ivfflat_get_proc_info(index, IVFFALT_VECTOR_TYPE_PROC);
    if (proc == NULL){
        return (IvfflatVectorType) &ivfflat_default_vector_type_data;
    }
    ereport(ERROR,
        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
        errmsg("vector type function not supported for ivfflat index")));
}

bool
ivfflat_norm_non_zero(FmgrInfo *proc, Oid collation, Datum value)
{
	return DatumGetFloat8(FunctionCall1Coll(
    proc, 
    collation, 
    value)) > 0;
}

Datum
ivfflat_normalize_value(const IvfflatVectorType vector_type, Oid collation, Datum value){
    return DirectFunctionCall1Coll(vector_type->normalize, collation, value);
}