#ifndef IVFFAT_H
#define IVFFAT_H

#include "postgres.h"
#include "fmgr.h"

#include "access/amapi.h"
#include "access/reloptions.h"
#include "commands/progress.h"
#include "commands/vacuum.h"
#include "nodes/execnodes.h"
#include "nodes/pathnodes.h"
#include "common/pg_prng.h"

#define IVFFLAT_VERSION 1
#define IVFFLAT_PAGE_ID          0xFF84

#define RandomDouble() pg_prng_double(&pg_global_prng_state)
#define RandomInt() pg_prng_uint32(&pg_global_prng_state)
#define SeedRandom(seed) pg_prng_seed(&pg_global_prng_state, seed)

IndexBuildResult *
ivfflat_build(Relation heap, Relation index, IndexInfo *indexInfo);

void
ivfflat_buildempty(Relation index);

bool
ivfflat_insert(Relation index, Datum *values, bool *isnull, ItemPointer heap_tid,
    Relation heap, IndexUniqueCheck checkUnique
    ,bool indexUnchanged
    ,IndexInfo *indexInfo
);

IndexBulkDeleteResult *
ivfflat_bulkdelete(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
				  IndexBulkDeleteCallback callback, void *callback_state);

IndexBulkDeleteResult *
ivfflat_vacuumcleanup(IndexVacuumInfo *info, IndexBulkDeleteResult *stats);

void
ivfflat_costestimate(PlannerInfo *root, IndexPath *path, double loop_count,
    Cost *indexStartupCost, Cost *indexTotalCost,
    Selectivity *indexSelectivity, double *indexCorrelation,
    double *indexPages);

bytea *
ivfflat_options(Datum reloptions, bool validate);

char *
ivfflat_buildphasename(int64 phasenum);

bool
ivfflat_validate(Oid opclassoid);

IndexScanDesc
ivfflat_beginscan(Relation index, int nkeys, int norderbys);

void
ivfflat_rescan(IndexScanDesc scan, ScanKey keys, int nkeys, ScanKey orderbys, int norderbys);


bool
ivfflat_gettuple(IndexScanDesc scan, ScanDirection dir);

void
ivfflat_endscan(IndexScanDesc scan);

#endif