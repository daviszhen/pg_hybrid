#ifndef IVFFLAT_INSERT_H
#define IVFFLAT_INSERT_H

#include "ivffat.h"

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
);

void
ivfflat_insert_tuple(
    Relation index, 
    Datum *values, 
    bool *isnull, 
    ItemPointer heap_tid, 
    Relation heap_rel);

#endif