#include "ivffat.h"

PGDLLEXPORT PG_FUNCTION_INFO_V1(pg_hybrid_ivfflat_handler);
Datum
pg_hybrid_ivfflat_handler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine = makeNode(IndexAmRoutine);
    //0 自定义操作符号
    amroutine->amstrategies = 0;
    //PROC的最大编号 - 支持5个支持函数（距离、归一化、K-means距离、K-means归一化、向量类型）
    amroutine->amsupport = 5;
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

char *
ivfflat_buildphasename(int64 phasenum){

    return NULL;
}

bool
ivfflat_validate(Oid opclassoid)
{
	return true;
}
