#include "ivfflat_scan.h"
#include "access/genam.h"
#include "access/tupdesc.h"
#include "access/relscan.h"
#include "postgres.h"
#include "src/ivfflat_page.h"
#include "storage/block.h"
#include "storage/bufmgr.h"
#include "storage/itemptr.h"
#include "utils/palloc.h"
#include "utils/selfuncs.h"
#include "utils/float.h"
#include "ivfflat_options.h"
#include "utils/spccache.h"
#include "utils/tuplesort.h"
#include "vector.h"
#include "catalog/pg_operator_d.h"
#include "miscadmin.h"
#include <float.h>
extern int ivfflat_probes;
void
ivfflat_costestimate(PlannerInfo *root, IndexPath *path, double loop_count,
    Cost *indexStartupCost, Cost *indexTotalCost,
    Selectivity *indexSelectivity, double *indexCorrelation,
    double *indexPages){
    GenericCosts costs;
    Relation index;
    int list_count;
    double ratio;
    double spc_seq_page_cost;
    double		sequentialRatio = 0.5;
    double		startupPages;

    if(path->indexorderbys == NIL){//no order by
        *indexStartupCost = get_float8_infinity();
        *indexTotalCost = get_float8_infinity();
        *indexSelectivity = 0;
        *indexCorrelation = 0;
        *indexPages = 0;
        return;
    }
    MemSet(&costs, 0, sizeof(costs));
    genericcostestimate(root,path,loop_count,&costs);

    index = index_open(path->indexinfo->indexoid,NoLock);
    ivfflat_get_meta_page(index,&list_count,NULL);
    index_close(index,NoLock);

    ratio = ((double) ivfflat_probes) / list_count;
    if(ratio > 1.0){
        ratio = 1.0;
    }

    get_tablespace_page_costs(
        path->indexinfo->reltablespace,
        NULL,
        &spc_seq_page_cost);

    costs.indexTotalCost -= 
        sequentialRatio * 
        costs.numIndexPages *
        (costs.spc_random_page_cost - spc_seq_page_cost);

    costs.indexStartupCost = costs.indexTotalCost * ratio;

    startupPages = costs.numIndexPages * ratio;
    if(startupPages > path->indexinfo->rel->pages && ratio < 0.5){
        costs.indexStartupCost -= 
            (1 - sequentialRatio) * 
            startupPages * 
            (costs.spc_random_page_cost - spc_seq_page_cost);

        costs.indexStartupCost -= 
            (startupPages - path->indexinfo->rel->pages) * spc_seq_page_cost;
    }
    *indexStartupCost = costs.indexStartupCost;
    *indexTotalCost = costs.indexTotalCost;
    *indexSelectivity = costs.indexSelectivity;
    *indexCorrelation = costs.indexCorrelation;
    *indexPages = costs.numIndexPages;
}

Tuplesortstate *
ivfflat_init_scan_sort_state(TupleDesc tup_desc){
    AttrNumber	attNums[] = {1};
	Oid			sortOperators[] = {Float8LessOperator};
	Oid			sortCollations[] = {InvalidOid};
	bool		nullsFirstFlags[] = {false};

	return tuplesort_begin_heap(
        tup_desc,
        1, 
        attNums,
        sortOperators,
        sortCollations,
        nullsFirstFlags,
        work_mem,
        NULL,
        false);
}

#define GET_SCAN_LIST(ptr) pairingheap_container(IvfflatScanListData, ph_node, ptr)
#define GET_SCAN_LIST_CONST(ptr) pairingheap_const_container(IvfflatScanListData, ph_node, ptr)

int
ivfflat_compare_lists(
    const pairingheap_node *a,
    const pairingheap_node *b,
    void *arg
){
    if(GET_SCAN_LIST_CONST(a)->distance > GET_SCAN_LIST_CONST(b)->distance){
        return 1;
    }
    if(GET_SCAN_LIST_CONST(a)->distance < GET_SCAN_LIST_CONST(b)->distance){
        return -1;
    }
    return 0;
}

IndexScanDesc
ivfflat_beginscan(Relation index, int nkeys, int norderbys){
    IndexScanDesc scan_desc;
    IvfflatScanOpaque scan_opaque;
    int list_count,dimensions;
    int max_probes,probes = ivfflat_probes;
    MemoryContext old_ctx;

    scan_desc = RelationGetIndexScan(index, nkeys, norderbys);
    ivfflat_get_meta_page(index,&list_count,&dimensions);

    max_probes = probes;
    if(probes > list_count){
        probes = list_count;
    }
    if(max_probes > list_count){
        max_probes = list_count;
    }

    scan_opaque = (IvfflatScanOpaque) palloc(sizeof(IvfflatScanOpaqueData));
    scan_opaque->vector_type = ivfflat_get_vector_type(index);
    scan_opaque->is_first_scan = true;
    scan_opaque->probes = probes;
    scan_opaque->max_probes = max_probes;
    scan_opaque->dimensions = dimensions;

    scan_opaque->vector_distance_proc = index_getprocinfo(index, 1,IVFFALT_VECTOR_DISTANCE_PROC);
    scan_opaque->vector_normalize_proc = ivfflat_get_proc_info(index, IVFFALT_VECTOR_NORMALIZATION_PROC);
    scan_opaque->collation = index->rd_indcollation[0];

    scan_opaque->tmp_ctx = AllocSetContextCreate(CurrentMemoryContext,
        "Ivfflat scan temporary context",
        ALLOCSET_DEFAULT_SIZES);

    old_ctx = MemoryContextSwitchTo(scan_opaque->tmp_ctx);

    scan_opaque->tup_desc = CreateTemplateTupleDesc(2);
    TupleDescInitEntry(
        scan_opaque->tup_desc,
        (AttrNumber) 1,
        "distance",
        FLOAT8OID,
        -1,
        0
    );
    TupleDescInitEntry(
        scan_opaque->tup_desc,
        (AttrNumber) 2,
        "heaptid",
        TIDOID,
        -1,
        0
    );

    scan_opaque->sort_state =ivfflat_init_scan_sort_state(scan_opaque->tup_desc);

    scan_opaque->v_slot = MakeSingleTupleTableSlot(scan_opaque->tup_desc, &TTSOpsVirtual);
    scan_opaque->m_slot = MakeSingleTupleTableSlot(scan_opaque->tup_desc, &TTSOpsMinimalTuple);
    scan_opaque->strategy = GetAccessStrategy(BAS_BULKREAD);

    scan_opaque->list_queue = pairingheap_allocate(ivfflat_compare_lists, scan_desc);
    scan_opaque->list_pages = palloc(max_probes * sizeof(BlockNumber));
    scan_opaque->list_index = 0;
    scan_opaque->lists = palloc(max_probes * sizeof(IvfflatScanListData));

    MemoryContextSwitchTo(old_ctx);
    scan_desc->opaque = scan_opaque;
    return scan_desc;
}


void
ivfflat_rescan(IndexScanDesc scan, ScanKey keys, int nkeys, ScanKey orderbys, int norderbys){
    IvfflatScanOpaque scan_opaque = (IvfflatScanOpaque) scan->opaque;
    scan_opaque->is_first_scan = true;
    pairingheap_reset(scan_opaque->list_queue);
    scan_opaque->list_index = 0;

    if (keys && scan->numberOfKeys > 0){
        memmove(scan->keyData, keys, scan->numberOfKeys * sizeof(ScanKeyData));
    }

	if (orderbys && scan->numberOfOrderBys > 0){
        memmove(scan->orderByData, orderbys, scan->numberOfOrderBys * sizeof(ScanKeyData));
    }
}

Datum
ivfflat_zero_distance(FmgrInfo *flinfo, Oid collation, Datum arg1, Datum arg2){
    return Float8GetDatum(0.0);
}

Datum
ivfflat_get_scan_value(IndexScanDesc scan_desc){
    IvfflatScanOpaque scan_opaque = (IvfflatScanOpaque) scan_desc->opaque;
    Datum value;
    if(scan_desc->orderByData->sk_flags & SK_ISNULL){
        value = PointerGetDatum(NULL);
        scan_opaque->dist_func = ivfflat_zero_distance;
    }else{
        value = scan_desc->orderByData->sk_argument;
        scan_opaque->dist_func = FunctionCall2Coll;
        if(scan_opaque->vector_normalize_proc != NULL){
            MemoryContext old_ctx = MemoryContextSwitchTo(scan_opaque->tmp_ctx);
            value = ivfflat_normalize_value(scan_opaque->vector_type, scan_opaque->collation, value);
            MemoryContextSwitchTo(old_ctx);
        }
    }
    return value;
}

void
ivfflat_get_scan_lists(IndexScanDesc scan_desc,Datum value){
    IvfflatScanOpaque scan_opaque = (IvfflatScanOpaque) scan_desc->opaque;
    BlockNumber start_blkno = IVFFLAT_HEAD_BLKNO;
    int list_count = 0;
    double max_distance = DBL_MAX,distance;
    Buffer center_buf;
    Page center_page;
    OffsetNumber max_offset;
    IvfflatList list;
    IvfflatScanList scan_list;

    //scan list pages
    while(BlockNumberIsValid(start_blkno)){
        center_buf = ReadBuffer(scan_desc->indexRelation,start_blkno);
        LockBuffer(center_buf,BUFFER_LOCK_SHARE);
        center_page = BufferGetPage(center_buf);

        max_offset = PageGetMaxOffsetNumber(center_page);
        for(OffsetNumber offset = FirstOffsetNumber;
            offset <= max_offset;
            offset = OffsetNumberNext(offset)){
            list = (IvfflatList) PageGetItem(
                center_page,
                PageGetItemId(center_page,offset));
            distance = DatumGetFloat8(
                scan_opaque->dist_func(
                scan_opaque->vector_distance_proc,
                scan_opaque->collation,
                PointerGetDatum(&list->center),
                value
            ));
            if(list_count < scan_opaque->max_probes){
                scan_list = &scan_opaque->lists[list_count];
                scan_list->start_page = list->start_page;
                scan_list->distance = distance;
                list_count++;
                //add to heap
                pairingheap_add(scan_opaque->list_queue,&scan_list->ph_node);

                if(list_count == scan_opaque->max_probes){
                    max_distance = GET_SCAN_LIST(pairingheap_first(scan_opaque->list_queue))->distance;
                }
            }else if(distance < max_distance){
                scan_list = GET_SCAN_LIST(pairingheap_remove_first(scan_opaque->list_queue));
                scan_list->start_page = list->start_page;
                scan_list->distance = distance;
                pairingheap_add(scan_opaque->list_queue,&scan_list->ph_node);

                max_distance = GET_SCAN_LIST(pairingheap_first(scan_opaque->list_queue))->distance;
            }
        }
        start_blkno = IvfflatPageGetOpaque(center_page)->nextblkno;
        UnlockReleaseBuffer(center_buf);
    }

    for(int i = list_count - 1; i >= 0; i--){
        scan_opaque->list_pages[i] = GET_SCAN_LIST(pairingheap_remove_first(scan_opaque->list_queue))->start_page;
    }
}

void
ivfflat_get_scan_items(IndexScanDesc scan_desc, Datum value){
    IvfflatScanOpaque scan_opaque = (IvfflatScanOpaque) scan_desc->opaque;
    TupleDesc tup_desc = RelationGetDescr(scan_desc->indexRelation);
    TupleTableSlot *slot = scan_opaque->v_slot;
    int batch_probes = 0;
    Buffer buf;
    Page page;
    OffsetNumber max_offset;
    IndexTuple itup;
    Datum datum;
    bool isnull;
    ItemId itemid;

    tuplesort_reset(scan_opaque->sort_state);

    while(scan_opaque->list_index < scan_opaque->max_probes && 
        (++batch_probes) <= scan_opaque->probes){
        BlockNumber search_page = scan_opaque->list_pages[scan_opaque->list_index++];
        while(BlockNumberIsValid(search_page)){
            buf = ReadBufferExtended(scan_desc->indexRelation,MAIN_FORKNUM,search_page,RBM_NORMAL,scan_opaque->strategy);
            LockBuffer(buf,BUFFER_LOCK_SHARE);
            page = BufferGetPage(buf);
            max_offset = PageGetMaxOffsetNumber(page);
            for(OffsetNumber offset = FirstOffsetNumber;
                offset <= max_offset;
                offset = OffsetNumberNext(offset)){
                itemid = PageGetItemId(page,offset);
                itup = (IndexTuple) PageGetItem(page,itemid);
                datum = index_getattr(itup,1,tup_desc,&isnull);
                ExecClearTuple(slot);
                slot->tts_values[0] = scan_opaque->dist_func(
                    scan_opaque->vector_distance_proc,
                    scan_opaque->collation,
                    datum,
                    value
                );
                slot->tts_isnull[0] = false;
                slot->tts_values[1] = PointerGetDatum(&itup->t_tid);
                slot->tts_isnull[1] = false;
                ExecStoreVirtualTuple(slot);

                tuplesort_puttupleslot(scan_opaque->sort_state,slot);
            }
            search_page = IvfflatPageGetOpaque(page)->nextblkno;
            UnlockReleaseBuffer(buf);
        }
    }

    tuplesort_performsort(scan_opaque->sort_state);
}

bool
ivfflat_gettuple(IndexScanDesc scan, ScanDirection dir){
    IvfflatScanOpaque scan_opaque = (IvfflatScanOpaque) scan->opaque;
    ItemPointer heap_tid;
    bool is_null;
    Datum value;

    if(scan_opaque->is_first_scan){
        if(scan->orderByData == NULL){
            elog(ERROR, "cannot scan ivfflat index without order");
        }
        if(!IsMVCCSnapshot(scan->xs_snapshot)){
            elog(ERROR, "non-MVCC snapshots are not supported with ivfflat");
        }
        value = ivfflat_get_scan_value(scan);
        ivfflat_get_scan_lists(scan, value);
        ivfflat_get_scan_items(scan, value);
        scan_opaque->is_first_scan = false;
        scan_opaque->value = value;
    }
    while(!tuplesort_gettupleslot(scan_opaque->sort_state,true,false,scan_opaque->m_slot,NULL)){
        if(scan_opaque->list_index == scan_opaque->max_probes){
            return false;
        }
        ivfflat_get_scan_items(scan, scan_opaque->value);
    }
    heap_tid = (ItemPointer) DatumGetPointer(slot_getattr(scan_opaque->m_slot,2,&is_null));
    scan->xs_heaptid = *heap_tid;
    scan->xs_recheck = false;
    scan->xs_recheckorderby = false;
    return true;
}

void
ivfflat_endscan(IndexScanDesc scan){
    IvfflatScanOpaque scan_opaque = (IvfflatScanOpaque) scan->opaque;
    tuplesort_end(scan_opaque->sort_state);
    MemoryContextDelete(scan_opaque->tmp_ctx);
    pfree(scan_opaque);
    scan->opaque = NULL;
}