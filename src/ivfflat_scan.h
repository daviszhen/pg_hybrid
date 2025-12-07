#ifndef IVFFLAT_SCAN_H
#define IVFFLAT_SCAN_H

#include "ivffat.h"
#include "vector.h"

typedef struct IvfflatScanListData {
    pairingheap_node ph_node;
	BlockNumber start_page;
	double		distance;
} IvfflatScanListData;

typedef IvfflatScanListData * IvfflatScanList;

typedef struct IvfflatScanOpaqueData{
    IvfflatVectorType vector_type;
    int probes,max_probes,dimensions;
    bool is_first_scan;
    Datum value;
    MemoryContext tmp_ctx;

    //
    Tuplesortstate *sort_state;
    TupleDesc tup_desc;
    TupleTableSlot *v_slot;
    TupleTableSlot *m_slot;
    BufferAccessStrategy strategy;

    //
    FmgrInfo *vector_distance_proc,*vector_normalize_proc;
    Oid collation;
    Datum (*dist_func)(FmgrInfo *flinfo, Oid collation, Datum arg1, Datum arg2);

    //
    pairingheap *list_queue;
    BlockNumber *list_pages;
    int list_index;
    IvfflatScanList lists;
} IvfflatScanOpaqueData;

typedef IvfflatScanOpaqueData * IvfflatScanOpaque;

Tuplesortstate *
ivfflat_init_scan_sort_state(TupleDesc tup_desc);

int
ivfflat_compare_lists(
    const pairingheap_node *a,
    const pairingheap_node *b,
    void *arg
);

Datum
ivfflat_zero_distance(FmgrInfo *flinfo, Oid collation, Datum arg1, Datum arg2);

Datum
ivfflat_get_scan_value(IndexScanDesc scan_desc);

void
ivfflat_get_scan_lists(IndexScanDesc scan_desc,Datum value);

void
ivfflat_get_scan_items(IndexScanDesc scan_desc, Datum value);
#endif