#include "ivfflat_insert.h"
#include "access/generic_xlog.h"
#include "access/itup.h"
#include "c.h"
#include "postgres.h"
#include "storage/block.h"
#include "storage/bufpage.h"
#include "storage/lockdefs.h"
#include "utils/memutils.h"
#include "vector.h"
#include "utils/relcache.h"
#include "utils/rel.h"
#include "ivfflat_page.h"
#include "storage/lmgr.h"

bool
ivfflat_insert(
    Relation index, 
    Datum *values, 
    bool *isnull, 
    ItemPointer heap_tid,
    Relation heap, 
    IndexUniqueCheck check_unique,
    bool index_unchanged,
    IndexInfo *index_info
){
    MemoryContext insert_ctx;
    MemoryContext old_ctx;
    if(isnull[0]){
        return false;
    }
    insert_ctx = AllocSetContextCreate(
        CurrentMemoryContext,
        "insert temporary context",
        ALLOCSET_DEFAULT_SIZES);
    old_ctx = MemoryContextSwitchTo(insert_ctx);

    ivfflat_insert_tuple(index, values, isnull, heap_tid, heap);

    MemoryContextSwitchTo(old_ctx);
    MemoryContextDelete(insert_ctx);
    return true;
}

void
ivfflat_insert_tuple(
    Relation index, 
    Datum *values, 
    bool *isnull, 
    ItemPointer heap_tid, 
    Relation heap_rel){
    Datum value;
    FmgrInfo *normalize_proc;
    Oid collation;
    BlockNumber insert_page = InvalidBlockNumber,original_insert_page;
    ListInfoData list_info;
    IndexTuple itup;
    Size sz;
    Buffer buf,new_buf;
    Page page,new_page;
    GenericXLogState *state;
    OffsetNumber offno;
    const IvfflatVectorType vector_type = ivfflat_get_vector_type(index);

    value = PointerGetDatum(PG_DETOAST_DATUM(values[0]));
    normalize_proc = ivfflat_get_proc_info(index, IVFFALT_VECTOR_NORMALIZATION_PROC);
    if(normalize_proc != NULL){
        collation = index->rd_indcollation[0];
        if(!ivfflat_norm_non_zero(normalize_proc, collation, value)){
            return;//zero vector
        }
        //normalize non-zero vector
        value = ivfflat_normalize_value(vector_type, collation, value);
    }

    ivfflat_get_meta_page(index,NULL,NULL);
    
    //find the nearest center and the list belong to it
    ivfflat_find_insert_page(
        index,
        &value,
        &insert_page,
        &list_info);
    original_insert_page = insert_page;

    //build index tuple from input
    itup = index_form_tuple(
        RelationGetDescr(index),
        &value,
        isnull
    );
    itup->t_tid = *heap_tid;

    sz = MAXALIGN(IndexTupleSize(itup));
    Assert(sz <= IvfflatPageMaxSpace);

    while(1){
        buf = ReadBuffer(index, insert_page);
        LockBuffer(buf, BUFFER_LOCK_EXCLUSIVE);

        state = GenericXLogStart(index);
        page = GenericXLogRegisterBuffer(state, buf,0);
        if(PageGetFreeSpace(page) >= sz){
            break;
        }
        insert_page = IvfflatPageGetOpaque(page)->nextblkno;
        if(BlockNumberIsValid(insert_page)){
            ivfflat_abort_xlog(buf, state);
        }else{
            LockRelationForExtension(index,ExclusiveLock);
            new_buf = ivfflat_new_buffer(index,MAIN_FORKNUM);
            UnlockRelationForExtension(index,ExclusiveLock);

            ivfflat_append_xlog(&new_buf, &new_page, state);

            insert_page = BufferGetBlockNumber(new_buf);
            IvfflatPageGetOpaque(page)->nextblkno = insert_page;

            ivfflat_commit_xlog(buf,state);

            state = GenericXLogStart(index);
            buf = new_buf;
            page = GenericXLogRegisterBuffer(state,buf,0);
            break;
        }
    }

    offno = PageAddItem(page, (Item) itup, sz, InvalidOffsetNumber, false, false);
    if(offno == InvalidOffsetNumber){
        elog(ERROR, "failed to add index item to \"%s\"", RelationGetRelationName(index));
    }

    ivfflat_commit_xlog(buf,state);

    if(insert_page != original_insert_page){
        ivfflat_update_list(
            index, 
            &list_info, 
            insert_page, 
            original_insert_page, 
            InvalidBlockNumber, 
            MAIN_FORKNUM);
    }
}