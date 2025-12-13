#include "vector.h"
#include "varatt.h"
#include "fmgr.h"
#include "utils/float.h"
#include "utils/builtins.h"
#include "utils/array.h"
#include "lib/stringinfo.h"
#include "libpq/pqformat.h"
#include "catalog/pg_type.h"
#include <math.h>
#include <errno.h>
#include <ctype.h>

Vector
vector_create(int dimensions){
    int sz = VECTOR_SIZE(dimensions);
    Vector vec = (Vector) palloc0(sz);
    SET_VARSIZE(vec, sz);
    vec->dim = dimensions;
    vec->unused = 0;
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
    vec->unused = 0;
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
    item_size = MAXALIGN(item_size);
    array->length = 0;
    array->max_length = max_length;
    array->dimensions = dimensions;
    array->item_size = item_size;
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

Pointer
array_get(Array array, int index){
    return array->data + (index * array->item_size);
}

PGDLLEXPORT PG_FUNCTION_INFO_V1(hvector_l2_normalize);
Datum
hvector_l2_normalize(PG_FUNCTION_ARGS){
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

PGDLLEXPORT Datum hvector_l2_normalize(PG_FUNCTION_ARGS);

static const IvfflatVectorTypeData ivfflat_default_vector_type_data = {
    .max_dimensions = IVFFLAT_MAX_DIMENSIONS,
    .normalize = hvector_l2_normalize,
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

/* Helper functions */
static inline void CheckDim(int dim)
{
    if (dim < 1)
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("vector must have at least 1 dimension")));
    if (dim > VECTOR_MAX_DIM)
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("vector cannot have more than %d dimensions", VECTOR_MAX_DIM)));
}

static inline void CheckExpectedDim(int32 typmod, int dim)
{
    if (typmod != -1 && typmod != dim)
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("expected %d dimensions, not %d", typmod, dim)));
}

static inline void is_valid_float(float value)
{
    if (isnan(value))
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("NaN not allowed in vector")));
    if (isinf(value))
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("infinite value not allowed in vector")));
}

static inline bool char_isspace(char ch)
{
    return (ch == ' ' || ch == '\t' || ch == '\n' || 
            ch == '\r' || ch == '\v' || ch == '\f');
}

/* Vector input function */
PGDLLEXPORT PG_FUNCTION_INFO_V1(hvector_in);
Datum
hvector_in(PG_FUNCTION_ARGS)
{
    char *lit = PG_GETARG_CSTRING(0);
    int32 typmod = PG_GETARG_INT32(2);
    float data[VECTOR_MAX_DIM];
    int dim = 0;
    char *pt = lit;
    Vector result;

    /* Skip leading whitespace */
    while (char_isspace(*pt))
        pt++;

    /* Must start with '[' */
    if (*pt != '[')
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                 errmsg("invalid input syntax for type vector: \"%s\"", lit),
                 errdetail("Vector contents must start with \"[\".")));

    pt++;

    /* Skip whitespace after '[' */
    while (char_isspace(*pt))
        pt++;

    /* Empty vector not allowed */
    if (*pt == ']')
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("vector must have at least 1 dimension")));

    /* Parse elements */
    while(1)
    {
        float val;
        char *stringEnd;

        if (dim == VECTOR_MAX_DIM)
            ereport(ERROR,
                    (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                     errmsg("vector cannot have more than %d dimensions", VECTOR_MAX_DIM)));

        /* Skip whitespace */
        while (char_isspace(*pt))
            pt++;

        /* Check for empty string */
        if (*pt == '\0')
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                     errmsg("invalid input syntax for type vector: \"%s\"", lit)));

        errno = 0;
        val = strtof(pt, &stringEnd);

        if (stringEnd == pt)
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                     errmsg("invalid input syntax for type vector: \"%s\"", lit)));

        if (errno == ERANGE && isinf(val))
            ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("\"%s\" is out of range for type vector", 
                            pnstrdup(pt, stringEnd - pt))));

        is_valid_float(val);
        data[dim++] = val;
        pt = stringEnd;

        /* Skip whitespace */
        while (char_isspace(*pt))
            pt++;

        if (*pt == ',')
            pt++;
        else if (*pt == ']')
        {
            pt++;
            break;
        }
        else
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                     errmsg("invalid input syntax for type vector: \"%s\"", lit)));
    }

    /* Only whitespace allowed after closing bracket */
    while (char_isspace(*pt))
        pt++;
    if (*pt != '\0')
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                 errmsg("invalid input syntax for type vector: \"%s\"", lit)));

    CheckDim(dim);
    CheckExpectedDim(typmod, dim);

    /* Create result vector */
    result = vector_create(dim);
    result->unused = 0;
    for (int i = 0; i < dim; i++)
        result->data[i] = data[i];

    PG_RETURN_POINTER(result);
}

/* Vector output function */
PGDLLEXPORT PG_FUNCTION_INFO_V1(hvector_out);
Datum
hvector_out(PG_FUNCTION_ARGS)
{
    Vector vec = PG_GETARG_VECTOR_P(0);
    StringInfoData buf;
    int i;
    char *result;

    initStringInfo(&buf);
    appendStringInfoChar(&buf, '[');

    for (i = 0; i < vec->dim; i++)
    {
        if (i > 0)
            appendStringInfoString(&buf, ", ");
        appendStringInfo(&buf, "%g", vec->data[i]);
    }

    appendStringInfoChar(&buf, ']');
    appendStringInfoChar(&buf, '\0');
    result = pstrdup(buf.data);
    pfree(buf.data);
    PG_RETURN_CSTRING(result);
}

/* Vector typmod input function */
PGDLLEXPORT PG_FUNCTION_INFO_V1(hvector_typmod_in);
Datum
hvector_typmod_in(PG_FUNCTION_ARGS)
{
    ArrayType *ta = PG_GETARG_ARRAYTYPE_P(0);
    int32 *tl;
    int n;

    tl = ArrayGetIntegerTypmods(ta, &n);

    if (n != 1)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("invalid type modifier")));

    if (*tl < 1)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("dimensions for type vector must be at least 1")));

    if (*tl > VECTOR_MAX_DIM)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("dimensions for type vector cannot exceed %d", VECTOR_MAX_DIM)));

    PG_RETURN_INT32(*tl);
}

/* Vector receive function (binary input) */
PGDLLEXPORT PG_FUNCTION_INFO_V1(hvector_recv);
Datum
hvector_recv(PG_FUNCTION_ARGS)
{
    StringInfo buf = (StringInfo) PG_GETARG_POINTER(0);
    int32 typmod = PG_GETARG_INT32(2);
    Vector result;
    int16 dim;
    int16 unused;

    dim = pq_getmsgint(buf, sizeof(int16));
    unused = pq_getmsgint(buf, sizeof(int16));

    CheckDim(dim);
    CheckExpectedDim(typmod, dim);

    if (unused != 0)
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("expected unused to be 0, not %d", unused)));

    result = vector_create(dim);
    result->unused = 0;
    for (int i = 0; i < dim; i++)
    {
        result->data[i] = pq_getmsgfloat4(buf);
        is_valid_float(result->data[i]);
    }

    PG_RETURN_POINTER(result);
}

/* Vector send function (binary output) */
PGDLLEXPORT PG_FUNCTION_INFO_V1(hvector_send);
Datum
hvector_send(PG_FUNCTION_ARGS)
{
    Vector vec = PG_GETARG_VECTOR_P(0);
    StringInfoData buf;

    pq_begintypsend(&buf);
    pq_sendint(&buf, vec->dim, sizeof(int16));
    pq_sendint(&buf, 0, sizeof(int16)); /* unused */
    for (int i = 0; i < vec->dim; i++)
        pq_sendfloat4(&buf, vec->data[i]);

    PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/* Vector dimensions function */
PGDLLEXPORT PG_FUNCTION_INFO_V1(hvector_dims);
Datum
hvector_dims(PG_FUNCTION_ARGS)
{
    Vector vec = PG_GETARG_VECTOR_P(0);
    PG_RETURN_INT32(vec->dim);
}

/* Vector norm function */
PGDLLEXPORT PG_FUNCTION_INFO_V1(hvector_norm);
Datum
hvector_norm(PG_FUNCTION_ARGS)
{
    Vector vec = PG_GETARG_VECTOR_P(0);
    double sum = 0.0;

    for (int i = 0; i < vec->dim; i++)
        sum += vec->data[i] * vec->data[i];

    PG_RETURN_FLOAT8(sqrt(sum));
}

/* L2 distance function */
PGDLLEXPORT PG_FUNCTION_INFO_V1(hvector_l2_distance);
Datum
hvector_l2_distance(PG_FUNCTION_ARGS)
{
    Vector a = PG_GETARG_VECTOR_P(0);
    Vector b = PG_GETARG_VECTOR_P(1);
    double sum = 0.0;
    double diff;

    if (a->dim != b->dim)
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("different vector dimensions %d and %d", a->dim, b->dim)));

    for (int i = 0; i < a->dim; i++)
    {
        diff = a->data[i] - b->data[i];
        sum += diff * diff;
    }

    PG_RETURN_FLOAT8(sqrt(sum));
}

/* L2 squared distance function */
PGDLLEXPORT PG_FUNCTION_INFO_V1(hvector_l2_squared_distance);
Datum
hvector_l2_squared_distance(PG_FUNCTION_ARGS)
{
    Vector a = PG_GETARG_VECTOR_P(0);
    Vector b = PG_GETARG_VECTOR_P(1);
    double sum = 0.0;
    double diff;

    if (a->dim != b->dim)
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("different vector dimensions %d and %d", a->dim, b->dim)));

    for (int i = 0; i < a->dim; i++)
    {
        diff = a->data[i] - b->data[i];
        sum += diff * diff;
    }

    PG_RETURN_FLOAT8(sum);
}