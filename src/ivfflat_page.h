#ifndef IVFFLAT_PAGE_H
#define IVFFLAT_PAGE_H

#include "postgres.h"
#include "src/ivfflat_build.h"
#include "storage/block.h"
#include "utils/rel.h"
#include "storage/bufmgr.h"
#include "access/generic_xlog.h"
#include "vector.h"

#define IVFFLAT_METAPAGE_BLKNO 0
#define IVFFLAT_HEAD_BLKNO 1

typedef struct IvfflatMetaPageData {
    uint32 version;
    uint16 dimensions;
    uint16 list_count;
} IvfflatMetaPageData;

typedef IvfflatMetaPageData * IvfflatMetaPage;

typedef struct IvfflatPageOpaqueData {
    BlockNumber nextblkno;
    uint16 unused;
    uint16 page_id;
} IvfflatPageOpaqueData;

typedef IvfflatPageOpaqueData * IvfflatPageOpaque;

typedef struct IvfflatListData {
    BlockNumber start_page;
    BlockNumber insert_page;
    VectorData center;
} IvfflatListData;

typedef IvfflatListData * IvfflatList;

#define IVFFLAT_LIST_SIZE(size) \
    (offsetof(IvfflatListData, center) + size)

#define IvfflatPageGetMeta(page)	((IvfflatMetaPageData *) PageGetContents(page))
#define IvfflatPageGetOpaque(page)	((IvfflatPageOpaque) PageGetSpecialPointer(page))
#define IvfflatPageMaxSpace \
    (BLCKSZ - MAXALIGN(SizeOfPageHeaderData) - MAXALIGN(sizeof(IvfflatPageOpaqueData)) - sizeof(ItemIdData))

Buffer
ivfflat_new_buffer(Relation rel, ForkNumber forkNum);

void
ivfflat_init_page(Buffer buf, Page page);

void
ivfflat_append_page(
    Relation index,
    Buffer *buf,
    Page *page,
    GenericXLogState **state,
    ForkNumber fork_num);

void
ivfflat_get_meta_page(Relation index, int *list_count, int *dimensions);

void
ivfflat_find_insert_page(
    Relation index,
    Datum *values,
    BlockNumber *insert_page,
    ListInfo list_info
);

void
ivfflat_start_xlog(
    Relation index,
    Buffer *buf,
    Page *page,
    GenericXLogState **state);

void
ivfflat_append_xlog(
    Buffer *buf,
    Page *page,
    GenericXLogState *state);

void
ivfflat_commit_xlog(Buffer buf, GenericXLogState *state);

void
ivfflat_abort_xlog(Buffer buf, GenericXLogState *state);
#endif