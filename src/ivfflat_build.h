#ifndef IVFFLAT_BUILD_H
#define IVFFLAT_BUILD_H

#include "ivffat.h"
#include "common/relpath.h"
#include "utils/rel.h"
#include "vector.h"
#include "utils/memutils.h"

#define IVFFLAT_DEFAULT_LISTS_COUNT 100

typedef struct ListInfoData{
    BlockNumber blknum;
    OffsetNumber offnum;
} ListInfoData;

typedef ListInfoData * ListInfo;

typedef struct IvfflatBuildCtxData 
{
    Relation heap;
    Relation index;
    IndexInfo *index_info;
    TupleDesc tupdesc;
    int dimensions;
    int list_count;
    IvfflatVectorType vector_type;

    double rel_tuple_count;
    double index_tuple_count;

    FmgrInfo *vector_distance_proc;
    FmgrInfo *vector_normalize_proc;
    Oid collation;

    TupleDesc sort_desc;
    TupleTableSlot *sort_slot;
    Tuplesortstate *sort_state;

    Array centers;
    Array list_infos;

    MemoryContext tmp_ctx;

} IvfflatBuildCtxData;

typedef IvfflatBuildCtxData * IvfflatBuildCtx;


IvfflatBuildCtx
ivfflat_build_init_ctx(
    Relation heap,
    Relation index,
    IndexInfo *index_info,
    ForkNumber forkNum
);

void
ivfflat_build_destroy_ctx(IvfflatBuildCtx ctx);

void
ivfflat_build_index(IvfflatBuildCtx ctx,ForkNumber forkNum);

void
ivfflat_calculate_centers(IvfflatBuildCtx ctx);

void
ivfflat_random_centers(IvfflatBuildCtx ctx);

void 
ivfflat_create_meta_page(
    Relation index, 
    int dimensions,
    int list_count,
    ForkNumber forkNum
);

void
ivfflat_create_list_pages(
    Relation index,
    Array centers,
    int dimensions,
    int list_count,
    ForkNumber fork_num,
    Array list_infos
);

void
ivfflat_create_entry_pages(IvfflatBuildCtx ctx, ForkNumber fork_num);

void
ivfflat_scan_tuples(IvfflatBuildCtx ctx);

void
ivfflat_sort_tuples_callback(
    Relation index,
    ItemPointer tid,
    Datum *values,
    bool *isnull,
    bool tupleIsAlive,
    void *state);

void
ivfflat_sort_tuples(
    Relation index,
    ItemPointer tid,
    Datum *values,
    IvfflatBuildCtx ctx);

void
ivfflat_insert_tuples(IvfflatBuildCtx ctx, ForkNumber fork_num);

void
ivfflat_get_next_tuple(
    Tuplesortstate *sort_state,
    TupleDesc tupdesc,
    TupleTableSlot *slot,
    IndexTuple *itup,
    int *list_no
);

void
ivfflat_update_list(
    Relation index,
    ListInfo list_info,
    BlockNumber insert_page,
    BlockNumber original_insert_page,
    BlockNumber start_page,
    ForkNumber fork_num
);

Tuplesortstate *
ivfflat_init_sort_state(TupleDesc tupdesc,int memory, SortCoordinate coordinate);

#endif