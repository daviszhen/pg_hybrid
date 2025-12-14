#include "ivfflat_page.h"
#include "access/generic_xlog.h"
#include "c.h"
#include "storage/block.h"
#include "storage/bufmgr.h"
#include "storage/bufpage.h"
#include "storage/off.h"
#include "utils/relcache.h"
#include <float.h>

Buffer
ivfflat_new_buffer(Relation rel, ForkNumber forkNum){ 
    Buffer		buf;
    // elog(INFO, "==pengzhen==new_buffer111:");
    buf = ReadBufferExtended(
        rel,
        forkNum,
        P_NEW,
        RBM_NORMAL,
        NULL);
        // elog(INFO, "==pengzhen==new_buffer222:");
	LockBuffer(buf, BUFFER_LOCK_EXCLUSIVE);
    // elog(INFO, "==pengzhen==new_buffer333:");
	return buf;
}

void
ivfflat_init_page(Buffer buf, Page page){
    PageInit(
        page, 
        BufferGetPageSize(buf),
         sizeof(IvfflatPageOpaqueData));
	IvfflatPageGetOpaque(page)->nextblkno = InvalidBlockNumber;
	IvfflatPageGetOpaque(page)->page_id = IVFFLAT_PAGE_ID;
}

void
ivfflat_append_page(
    Relation index,
    Buffer *buf,
    Page *page,
    GenericXLogState **state,
    ForkNumber fork_num){
    Buffer new_buf;
    Page new_page;
    new_buf = ivfflat_new_buffer(index, fork_num);
    new_page = GenericXLogRegisterBuffer(*state, new_buf, GENERIC_XLOG_FULL_IMAGE);
    IvfflatPageGetOpaque(*page)->nextblkno = BufferGetBlockNumber(new_buf);
    ivfflat_init_page(new_buf, new_page);

    GenericXLogFinish(*state);
    UnlockReleaseBuffer(*buf);

    *state = GenericXLogStart(index);
    *page = GenericXLogRegisterBuffer(*state, new_buf, GENERIC_XLOG_FULL_IMAGE);
    *buf = new_buf;
}

void
ivfflat_get_meta_page(Relation index, int *list_count, int *dimensions){
    Buffer buf;
    Page page;
    IvfflatMetaPage meta;

    buf = ReadBuffer(index, IVFFLAT_METAPAGE_BLKNO);
    LockBuffer(buf, BUFFER_LOCK_SHARE);
    page = BufferGetPage(buf);
    meta = IvfflatPageGetMeta(page);

    if(list_count != NULL){
        *list_count = meta->list_count;
    }
    if(dimensions != NULL){
        *dimensions = meta->dimensions;
    }

    UnlockReleaseBuffer(buf);
}

void
ivfflat_find_insert_page(
    Relation index,
    Datum *values,
    BlockNumber *insert_page,
    ListInfo list_info
){
    double min_distance = DBL_MAX;
    BlockNumber next_blkno = IVFFLAT_HEAD_BLKNO;
    FmgrInfo *distance_proc;
    Oid collation;
    Buffer buf;
    Page page;
    OffsetNumber max_offset;
    IvfflatList list;
    double distance;

    distance_proc = index_getprocinfo(index,1, IVFFALT_VECTOR_DISTANCE_PROC);
    collation = index->rd_indcollation[0];

    while(BlockNumberIsValid(next_blkno)){
        buf = ReadBuffer(index, next_blkno);
        LockBuffer(buf, BUFFER_LOCK_SHARE);
        page = BufferGetPage(buf);
        max_offset = PageGetMaxOffsetNumber(page);

        for(
            OffsetNumber offset = FirstOffsetNumber;
            offset <= max_offset;
            offset = OffsetNumberNext(offset)
        ){
            list = (IvfflatList) PageGetItem(
                page,
                PageGetItemId(page, offset));
            distance = DatumGetFloat8(
                FunctionCall2Coll(
                    distance_proc, 
                    collation, 
                    values[0], 
                    PointerGetDatum(&list->center)));
            if(distance < min_distance || 
                !BlockNumberIsValid(*insert_page)){
                *insert_page = list->insert_page;
                list_info->blknum = next_blkno;
                list_info->offnum = offset;
                min_distance = distance;
            }
        }
        next_blkno = IvfflatPageGetOpaque(page)->nextblkno;
        UnlockReleaseBuffer(buf);
    }
}

void
ivfflat_start_xlog(Relation index,Buffer *buf,Page *page, GenericXLogState **state){
    // elog(INFO, "==pengzhen==start_xlog000: state: %d index: %d",state != NULL,index != NULL);
    *state = GenericXLogStart(index);
    // elog(INFO, "==pengzhen==start_xlog111:");
    ivfflat_append_xlog(buf, page, *state);
    // elog(INFO, "==pengzhen==start_xlog222:");
}

void
ivfflat_init_register_page(
    Relation index,
    Buffer *buf,
    Page *page,
    GenericXLogState **state
){
    // elog(INFO, "==pengzhen==ivfflat_init_register_page111:");
    *state = GenericXLogStart(index);
    // elog(INFO, "==pengzhen==ivfflat_init_register_page222:");
    *page = GenericXLogRegisterBuffer(*state, *buf, GENERIC_XLOG_FULL_IMAGE);
    // elog(INFO, "==pengzhen==ivfflat_init_register_page333:");
    ivfflat_init_page(*buf,*page);
    // elog(INFO, "==pengzhen==ivfflat_init_register_page444:");
}

void
ivfflat_append_xlog(
    Buffer *buf,
    Page *page,
    GenericXLogState *state){
    *page = GenericXLogRegisterBuffer(state, *buf, GENERIC_XLOG_FULL_IMAGE);
    // elog(INFO, "==pengzhen==append_xlog111:");
    ivfflat_init_page(*buf, *page);
    // elog(INFO, "==pengzhen==append_xlog222:");
}

void
ivfflat_commit_xlog(Buffer buf, GenericXLogState *state){
    GenericXLogFinish(state);
    UnlockReleaseBuffer(buf);
}

void
ivfflat_abort_xlog(Buffer buf, GenericXLogState *state){
    GenericXLogAbort(state);
    UnlockReleaseBuffer(buf);
}