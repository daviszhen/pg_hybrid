#include "ivffat.h"

PGDLLEXPORT PG_FUNCTION_INFO_V1(ivfflat_handler);
Datum
ivfflat_handler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine = makeNode(IndexAmRoutine);
    //0 自定义操作符号
    amroutine->amstrategies = 0;
    //PROC的最大编号
    amroutine->amsupport = 0;
    //不支持按列排序
    amroutine->amcanorder = false;
    //支持按操作符结果排序
	amroutine->amcanorderbyop = true;
    //支持唯一索引
    amroutine->amcanunique = false;
    //支持多列索引
	amroutine->amcanmulticol = false;

	amroutine->ambuild = ivfflat_build;
	amroutine->ambuildempty = ivfflat_buildempty;
	amroutine->aminsert = ivfflat_insert;

    amroutine->ambulkdelete = ivfflat_bulkdelete;
    amroutine->amvacuumcleanup = ivfflat_vacuumcleanup;
    amroutine->amcostestimate = ivfflat_costestimate;

    amroutine->amoptions = ivfflat_options;
    amroutine->ambuildphasename = ivfflat_buildphasename;
    amroutine->amvalidate = ivfflat_validate;
    
    amroutine->ambeginscan = ivfflat_beginscan;
	amroutine->amrescan = ivfflat_rescan;
	amroutine->amgettuple = ivfflat_gettuple;
	amroutine->amendscan = ivfflat_endscan;

    PG_RETURN_POINTER(amroutine);
}

IndexBulkDeleteResult *
ivfflat_bulkdelete(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
				  IndexBulkDeleteCallback callback, void *callback_state)
{

    return NULL;
}

IndexBulkDeleteResult *
ivfflat_vacuumcleanup(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{

    return NULL;
}

static void
ivfflat_costestimate(PlannerInfo *root, IndexPath *path, double loop_count,
    Cost *indexStartupCost, Cost *indexTotalCost,
    Selectivity *indexSelectivity, double *indexCorrelation,
    double *indexPages){
    
}

static bytea *
ivfflat_options(Datum reloptions, bool validate){

    return NULL;
}

static char *
ivfflat_buildphasename(int64 phasenum){
    return NULL;
}

static bool
ivfflat_validate(Oid opclassoid)
{
	return true;
}

IndexScanDesc
ivfflat_beginscan(Relation index, int nkeys, int norderbys){

    return NULL;
}

void
ivfflat_rescan(IndexScanDesc scan, ScanKey keys, int nkeys, ScanKey orderbys, int norderbys){

}

bool
ivfflat_gettuple(IndexScanDesc scan, ScanDirection dir){
    return false;
}

void
ivfflat_endscan(IndexScanDesc scan){

}