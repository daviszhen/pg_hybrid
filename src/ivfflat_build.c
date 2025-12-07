#include "ivfflat_build.h"
#include "access/generic_xlog.h"
#include "c.h"
#include "fmgr.h"
#include "ivfflat_page.h"
#include "postgres.h"
#include "src/vector.h"
#include "catalog/pg_operator_d.h"
#include "miscadmin.h"
#include "access/tableam.h"
#include "storage/block.h"
#include "storage/bufmgr.h"
#include "storage/off.h"
#include <float.h>
#include "catalog/index.h"

IndexBuildResult *
ivfflat_build(Relation heap, Relation index, IndexInfo *indexInfo)
{
    IndexBuildResult *result = NULL;
    IvfflatBuildCtx ctx = ivfflat_build_init_ctx(
        heap,
        index,
        indexInfo,
        MAIN_FORKNUM);

    elog(INFO, "==pengzhen==ivfflat_build: %d", ctx->centers->dimensions);
    ivfflat_build_index(ctx, MAIN_FORKNUM);

    result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));
    result->heap_tuples = ctx->rel_tuple_count;
    result->index_tuples = ctx->index_tuple_count;

    ivfflat_build_destroy_ctx(ctx);
    return result;
}

void
ivfflat_buildempty(Relation index)
{
    IndexInfo *index_info;
    IvfflatBuildCtx ctx;
    index_info = BuildIndexInfo(index);

     ctx = ivfflat_build_init_ctx(
        NULL,
        index,
        index_info,
        INIT_FORKNUM);

    ivfflat_build_index(ctx, INIT_FORKNUM);

    ivfflat_build_destroy_ctx(ctx);
}

IvfflatBuildCtx
ivfflat_build_init_ctx(
    Relation heap,
    Relation index,
    IndexInfo *index_info,
    ForkNumber forkNum
)
{
    IvfflatBuildCtx ctx = (IvfflatBuildCtx) palloc(sizeof(IvfflatBuildCtxData));
    ctx->heap = heap;
    ctx->index = index;
    ctx->index_info = index_info;
    ctx->tupdesc = RelationGetDescr(index);
    ctx->vector_type = ivfflat_get_vector_type(index);

    ctx->list_count = IVFFLAT_DEFAULT_LISTS_COUNT;
    ctx->dimensions = TupleDescAttr(index->rd_att, 0)->atttypmod;
    if (ctx->dimensions > IVFFLAT_MAX_DIMENSIONS) {
        ereport(ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("dimensions must be <= %d for ivfflat index", IVFFLAT_MAX_DIMENSIONS)));
    }

    ctx->rel_tuple_count = 0;
    ctx->index_tuple_count = 0;

    ctx->vector_distance_proc = index_getprocinfo(index, 1, IVFFALT_VECTOR_DISTANCE_PROC);
    ctx->vector_normalize_proc = ivfflat_get_proc_info(index, IVFFALT_VECTOR_NORMALIZATION_PROC);
    ctx->collation = index->rd_indcollation[0];

    ctx->sort_desc = CreateTemplateTupleDesc(3);
    TupleDescInitEntry(ctx->sort_desc, (AttrNumber) 1, "list", INT4OID, -1, 0);
    TupleDescInitEntry(ctx->sort_desc, (AttrNumber) 2, "tid", TIDOID, -1, 0);
    TupleDescInitEntry(ctx->sort_desc, (AttrNumber) 3, "vector", TupleDescAttr(ctx->tupdesc, 0)->atttypid, -1, 0);

    ctx->sort_slot = MakeSingleTupleTableSlot(ctx->sort_desc, &TTSOpsVirtual);
    ctx->centers = array_create(
        ctx->list_count,
         ctx->dimensions, 
         ctx->vector_type->item_size(ctx->dimensions));
    ctx->list_infos = array_create(
        ctx->list_count,
        0,
        sizeof(ListInfoData));
    ctx->tmp_ctx = AllocSetContextCreate(
        CurrentMemoryContext,
        "ivfflat temp ctx",
        ALLOCSET_DEFAULT_SIZES);
    return ctx;
}

void
ivfflat_build_destroy_ctx(IvfflatBuildCtx ctx)
{
    array_destroy(ctx->centers);
    array_destroy(ctx->list_infos);
    MemoryContextDelete(ctx->tmp_ctx);
    pfree(ctx);
}

void
ivfflat_build_index(IvfflatBuildCtx ctx,ForkNumber fork_num){
    elog(INFO, "==pengzhen==111:");
    //step 1. calculate the centers
    ivfflat_calculate_centers(ctx);
    elog(INFO, "==pengzhen==222:");
    //step 2. create the meta page
    ivfflat_create_meta_page(
        ctx->index,
         ctx->dimensions,
          ctx->list_count,
           fork_num);
    elog(INFO, "==pengzhen==333:");
    //step 3. create the list pages
    ivfflat_create_list_pages(
        ctx->index,
        ctx->centers,
        ctx->dimensions,
        ctx->list_count,
        fork_num,
        ctx->list_infos);
    elog(INFO, "==pengzhen==444:");
    //step 4. create the entry pages
    ivfflat_create_entry_pages(ctx,fork_num);
    elog(INFO, "==pengzhen==555:");
    if(fork_num == INIT_FORKNUM){
        log_newpage_range(
            ctx->index,
            fork_num,
            0,
            RelationGetNumberOfBlocksInFork(ctx->index, fork_num),
            true
        );
        elog(INFO, "==pengzhen==666:");
    }
    elog(INFO, "==pengzhen==777:");
}

FmgrInfo *
ivfflat_get_proc_info(Relation index, uint16 procnum){
    if(!OidIsValid(index_getprocid(index,1,procnum))){
        return NULL;
    }
    return index_getprocinfo(index, 1, procnum);
}

void
ivfflat_calculate_centers(IvfflatBuildCtx ctx){
    ivfflat_random_centers(ctx);
}

void
ivfflat_random_centers(IvfflatBuildCtx ctx){
    float *temp = (float *) palloc(
        sizeof(float)*ctx->centers->dimensions);

    for(; ctx->centers->length < ctx->centers->dimensions;){
        Pointer center = array_get(ctx->centers, ctx->centers->length);
        for(int i = 0; i < ctx->centers->dimensions; i++){
            temp[i] = (float) 0.0;//FIXME: use random double
        }
        ctx->vector_type->update_center(center, ctx->centers->dimensions, temp);
        ctx->centers->length++;
    }
}

void 
ivfflat_create_meta_page(
    Relation index, 
    int dimensions,
    int list_count,
    ForkNumber forkNum
){
    Buffer buf ;
    Page page;
    GenericXLogState *state;
    IvfflatMetaPage meta;
    elog(INFO, "==pengzhen==create_meta_page111:");
    buf= ivfflat_new_buffer(index, forkNum);
    elog(INFO, "==pengzhen==create_meta_page222:");
    ivfflat_start_xlog(index, &buf, &page, &state);
    elog(INFO, "==pengzhen==create_meta_page333:");
    meta = IvfflatPageGetMeta(page);
    meta->version = IVFFLAT_VERSION;
    meta->dimensions = dimensions;
    meta->list_count = list_count;
    ((PageHeader) page)->pd_lower =
        ((char *) meta + sizeof(IvfflatMetaPageData)) - (char *) page;
    elog(INFO, "==pengzhen==create_meta_page444:");
    ivfflat_commit_xlog(buf, state);
}

void
ivfflat_create_list_pages(
    Relation index,
    Array centers,
    int dimensions,
    int list_count,
    ForkNumber fork_num,
    Array list_infos
){
    Size list_size;
    IvfflatList list_entry;
    Buffer buf;
    Page page;
    GenericXLogState *state;
    Pointer center;
    ListInfo list_info;

    list_size = MAXALIGN(IVFFLAT_LIST_SIZE(centers->item_size));
    list_entry = (IvfflatList) palloc0(list_size);

    buf = ivfflat_new_buffer(index, fork_num);
    ivfflat_start_xlog(index, &buf, &page, &state);

    for(int i = 0; i < list_count; i++){
        OffsetNumber offno;
        MemSet(list_entry,0, list_size);

        list_entry->start_page = InvalidBlockNumber;
        list_entry->insert_page = InvalidBlockNumber;
        center = array_get(centers, i);
        memcpy(
            &list_entry->center, 
            center, 
            VARSIZE_ANY(center));

        if(PageGetFreeSpace(page) < list_size){
            ivfflat_append_page(index, &buf, &page, &state, fork_num);
        }

        offno = PageAddItem(page, (Item) list_entry, list_size, InvalidOffsetNumber, false, false);
        if (offno == InvalidOffsetNumber){
            elog(ERROR, "failed to add list entry to page");
        }
        list_info = (ListInfo) array_get(list_infos, i);
        list_info->blknum = BufferGetBlockNumber(buf);
        list_info->offnum = offno;
    }

    ivfflat_commit_xlog(buf, state);
    pfree(list_entry);
}

void
ivfflat_create_entry_pages(IvfflatBuildCtx ctx, ForkNumber fork_num){
    ivfflat_scan_tuples(ctx);
    tuplesort_performsort(ctx->sort_state);
    ivfflat_insert_tuples(ctx,fork_num);
    tuplesort_end(ctx->sort_state);
}

void
ivfflat_scan_tuples(IvfflatBuildCtx ctx){
    ctx->sort_state = ivfflat_init_sort_state(
        ctx->sort_desc,
        maintenance_work_mem,
        NULL
    );
    if(ctx->heap != NULL){
        ctx->rel_tuple_count = table_index_build_scan(
            ctx->heap,
            ctx->index,
            ctx->index_info,
            true,
            true,
            ivfflat_sort_tuples,
            (void *) ctx,
            NULL
        );
    }
}

Tuplesortstate *
ivfflat_init_sort_state(TupleDesc tupdesc,int memory, SortCoordinate coordinate){
    AttrNumber	attNums[] = {1};
	Oid			sortOperators[] = {Int4LessOperator};
	Oid			sortCollations[] = {InvalidOid};
	bool		nullsFirstFlags[] = {false};

	return tuplesort_begin_heap(
        tupdesc,
        1,
        attNums,
        sortOperators,
        sortCollations,
        nullsFirstFlags,
        memory,
        coordinate,
        false);
}

void
ivfflat_sort_tuples(
    Relation index,
    ItemPointer tid,
    Datum *values,
    bool *isnull,
    bool tupleIsAlive,
    void *state){
    double distance;
    double min_distance = DBL_MAX;
    int closest_center = 0;
    Pointer center;
    Datum   value;
    IvfflatBuildCtx ctx = (IvfflatBuildCtx) state;
    if(isnull[0]){
        return;
    }

    value = PointerGetDatum(PG_DETOAST_DATUM(values[0]));

    for(int i = 0; i < ctx->centers->length; i++){
        center = array_get(ctx->centers, i);
        distance = DatumGetFloat8(FunctionCall2Coll(
            ctx->vector_distance_proc,
            ctx->collation,
            value,
            PointerGetDatum(center)
        ));
        if(distance < min_distance){
            min_distance = distance;
            closest_center = i;
        }
    }

    //fill tuple
    ExecClearTuple(ctx->sort_slot);
    ctx->sort_slot->tts_values[0] = Int32GetDatum(closest_center);
    ctx->sort_slot->tts_isnull[0] = false;
    ctx->sort_slot->tts_values[1] = PointerGetDatum(tid);
    ctx->sort_slot->tts_isnull[1] = false;
    ctx->sort_slot->tts_values[2] = value;
    ctx->sort_slot->tts_isnull[2] = false;
    ExecStoreVirtualTuple(ctx->sort_slot);
    tuplesort_puttupleslot(ctx->sort_state, ctx->sort_slot);

    ctx->index_tuple_count++;
}

void
ivfflat_insert_tuples(IvfflatBuildCtx ctx, ForkNumber fork_num){
    TupleTableSlot *slot;
    TupleDesc tupdesc;
    IndexTuple itup;
    int list_no;
    ListInfo list_info;

    slot = MakeSingleTupleTableSlot(
        ctx->sort_desc,
        &TTSOpsMinimalTuple
    );
    tupdesc = ctx->sort_desc;

    ivfflat_get_next_tuple(
        ctx->sort_state, 
        tupdesc,
        slot,
        &itup,
        &list_no);

    for(int i = 0; i < ctx->centers->length; i++){
        Buffer buf;
        Page page;
        GenericXLogState *state;
        BlockNumber start_page;
        BlockNumber insert_page;
        OffsetNumber offno;

        CHECK_FOR_INTERRUPTS();

        buf = ivfflat_new_buffer(ctx->index, fork_num);
        ivfflat_start_xlog(ctx->index, &buf, &page, &state);

        start_page = BufferGetBlockNumber(buf);
        while(list_no == i){
            Size    itemsz = MAXALIGN(IndexTupleSize(itup));
            if(PageGetFreeSpace(page) < itemsz){
                ivfflat_append_page(
                    ctx->index,
                    &buf,
                    &page,
                    &state,
                    fork_num);
            }
            offno = PageAddItem(
                page,
                (Item) itup,
                itemsz,
                InvalidOffsetNumber,
                false,
                false);
            if(offno == InvalidOffsetNumber){
                elog(ERROR, "failed to add list entry to page");
            }

            pfree(itup);

            ivfflat_get_next_tuple(
                ctx->sort_state,
                tupdesc,
                slot,
                &itup,
                &list_no);
        }
        insert_page = BufferGetBlockNumber(buf);
        ivfflat_commit_xlog(buf, state);
        list_info = (ListInfo) array_get(ctx->list_infos, i);
        ivfflat_update_list(
            ctx->index,
            list_info,
            insert_page,
            InvalidBlockNumber,
            start_page,
            fork_num
        );
    }
}

void
ivfflat_get_next_tuple(
    Tuplesortstate *sort_state,
    TupleDesc tupdesc,
    TupleTableSlot *slot,
    IndexTuple *itup,
    int *list_no
){
    if(tuplesort_gettupleslot(sort_state, true, false, slot, NULL)){
        Datum value;
        bool isnull;

        *list_no = DatumGetInt32(slot_getattr(slot, 1, &isnull));
        value = slot_getattr(slot, 3, &isnull);

        *itup = index_form_tuple(tupdesc, &value, &isnull);
        (*itup)->t_tid = *((ItemPointer) DatumGetPointer(
            slot_getattr(slot, 2, &isnull)));
    }else{
        *list_no = -1;
    }
}

void
ivfflat_update_list(
    Relation index,
    ListInfo list_info,
    BlockNumber insert_page,
    BlockNumber original_insert_page,
    BlockNumber start_page,
    ForkNumber fork_num
){
    Buffer buf;
    Page page;
    GenericXLogState *state;
    IvfflatList list;
    bool changed = false;

    buf = ReadBufferExtended(
    index,
    fork_num,
    list_info->blknum,
    RBM_NORMAL,
    NULL);
    LockBuffer(buf, BUFFER_LOCK_EXCLUSIVE);
    state = GenericXLogStart(index);
    page = GenericXLogRegisterBuffer(state, buf, 0);
    list = (IvfflatList) PageGetItem(page, 
        PageGetItemId(page, 
            list_info->offnum));
    
    if(BlockNumberIsValid(insert_page) && 
        insert_page != list->insert_page){
        if(!BlockNumberIsValid(original_insert_page) || 
            insert_page >= original_insert_page){
            list->insert_page = insert_page;
            changed = true;
        }
    }

    if(BlockNumberIsValid(start_page) && start_page != list->start_page){
        list->start_page = start_page;
        changed = true;
    }

    if(changed){
        ivfflat_commit_xlog(buf, state);
    }else{
        ivfflat_abort_xlog(buf, state);
    }
}