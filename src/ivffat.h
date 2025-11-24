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

#define IVFFLAT_VERSION 1

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

static void
ivfflat_costestimate(PlannerInfo *root, IndexPath *path, double loop_count,
    Cost *indexStartupCost, Cost *indexTotalCost,
    Selectivity *indexSelectivity, double *indexCorrelation,
    double *indexPages);

static bytea *
ivfflat_options(Datum reloptions, bool validate);

static char *
ivfflat_buildphasename(int64 phasenum);

static bool
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