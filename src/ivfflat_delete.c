#include "ivfflat_delete.h"
#include "access/generic_xlog.h"
#include "common/relpath.h"
#include "ivfflat_page.h"
#include "storage/block.h"
#include "storage/bufmgr.h"
#include "storage/off.h"

IndexBulkDeleteResult *
ivfflat_bulkdelete(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
				  IndexBulkDeleteCallback callback, void *callback_state)
{
    Buffer center_buf,buf;
    Page center_page,page;
    OffsetNumber center_offset,offset;
    OffsetNumber center_max_offset,max_offset,deletable[MaxOffsetNumber];
    BlockNumber list_pages[MaxOffsetNumber],search_page,insert_page;
    ListInfoData list_info_data;
    IvfflatList list;
    int ndeletable;
    GenericXLogState *state;
    IndexTuple index_tup;
    ItemPointer heap_tup;

    BlockNumber start_blkno = IVFFLAT_HEAD_BLKNO;
    BufferAccessStrategy strategy = GetAccessStrategy(BAS_BULKREAD);
    if(stats == NULL){
        stats = (IndexBulkDeleteResult *) palloc0(sizeof(IndexBulkDeleteResult));
    }

    //scan list pages
    while(BlockNumberIsValid(start_blkno)){
        center_buf = ReadBuffer(info->index,start_blkno);
        LockBuffer(center_buf,BUFFER_LOCK_SHARE);
        center_page = BufferGetPage(center_buf);

        center_max_offset = PageGetMaxOffsetNumber(center_page);
        //scan center lists
        for(center_offset = FirstOffsetNumber;
            center_offset <= center_max_offset;
            center_offset = OffsetNumberNext(center_offset)){
            list = (IvfflatList) PageGetItem(
                center_page,
                PageGetItemId(center_page,center_offset));
            list_pages[center_offset - FirstOffsetNumber] = list->start_page;
        }

        list_info_data.blknum = start_blkno;
        start_blkno = IvfflatPageGetOpaque(center_page)->nextblkno;
        UnlockReleaseBuffer(center_buf);

        //scan entries
        for(center_offset = FirstOffsetNumber;
            center_offset <= center_max_offset;
            center_offset = OffsetNumberNext(center_offset)){
            search_page = list_pages[center_offset - FirstOffsetNumber];
            insert_page = InvalidBlockNumber;

            //scan entries pages
            while(BlockNumberIsValid(search_page)){
                vacuum_delay_point();
                buf = ReadBufferExtended(
                    info->index,
                    MAIN_FORKNUM,
                    search_page,
                    RBM_NORMAL,
                    strategy);
                
                //delete entries from pages may be blocked
                LockBufferForCleanup(buf);

                state = GenericXLogStart(info->index);
                page = GenericXLogRegisterBuffer(state, buf, 0);

                max_offset = PageGetMaxOffsetNumber(page);
                ndeletable = 0;

                //scan entries on the page
                for(offset = FirstOffsetNumber; 
                    offset <= max_offset; 
                    offset = OffsetNumberNext(offset)){
                    index_tup = (IndexTuple) PageGetItem(page,
                         PageGetItemId(page, offset));
                    heap_tup = &(index_tup->t_tid);
                    if(callback(heap_tup, callback_state)){
                        deletable[ndeletable++] = offset;
                        stats->tuples_removed++;
                    }else{
                        stats->num_index_tuples++;
                    }
                }

                if(!BlockNumberIsValid(insert_page) && ndeletable > 0){
                    insert_page = search_page;
                }

                search_page = IvfflatPageGetOpaque(page)->nextblkno;
                if(ndeletable > 0){
                    PageIndexMultiDelete(page, deletable, ndeletable);
                    GenericXLogFinish(state);
                }else{
                    GenericXLogAbort(state);
                }

                UnlockReleaseBuffer(buf);
            }
            
            if(BlockNumberIsValid(insert_page)){
                list_info_data.offnum = center_offset;
                ivfflat_update_list(
                    info->index,
                    &list_info_data,
                    insert_page,
                    InvalidBlockNumber,
                    InvalidBlockNumber,
                    MAIN_FORKNUM
                );
            }
        }
    }
    FreeAccessStrategy(strategy);
    return stats;
}

IndexBulkDeleteResult *
ivfflat_vacuumcleanup(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{
    Relation rel = info->index;
    if(info->analyze_only){
        return stats;
    }
    if(stats == NULL){
        return NULL;
    }
    stats->num_pages = RelationGetNumberOfBlocks(rel);
    return stats;
}
