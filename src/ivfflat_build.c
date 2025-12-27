#include "ivfflat_build.h"
#include "access/genam.h"
#include "access/generic_xlog.h"
#include "c.h"
#include "fmgr.h"
#include "ivfflat_page.h"
#include "postgres.h"
#include "src/ivffat.h"
#include "src/vector.h"
#include "catalog/pg_operator_d.h"
#include "miscadmin.h"
#include "access/tableam.h"
#include "storage/block.h"
#include "storage/bufmgr.h"
#include "storage/off.h"
#include <limits.h>
#include <math.h>
#include <float.h>
#include "catalog/index.h"
#include "utils/array.h"
#include "utils/elog.h"
#include "utils/palloc.h"
#include "utils/sampling.h"
#include "varatt.h"

IndexBuildResult *
ivfflat_build(Relation heap, Relation index, IndexInfo *indexInfo)
{
    IndexBuildResult *result = NULL;
    IvfflatBuildCtx ctx = ivfflat_build_init_ctx(
        heap,
        index,
        indexInfo,
        MAIN_FORKNUM);

    ivfflat_build_index(ctx, MAIN_FORKNUM);

    result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));
    result->heap_tuples = ctx->rel_tuple_count;
    result->index_tuples = ctx->index_tuple_count;

    ivfflat_build_destroy_ctx(ctx);
    return result;
}

void
ivfflat_buildempty(Relation index)
{
    IndexInfo *index_info;
    IvfflatBuildCtx ctx;
    index_info = BuildIndexInfo(index);

     ctx = ivfflat_build_init_ctx(
        NULL,
        index,
        index_info,
        INIT_FORKNUM);

    ivfflat_build_index(ctx, INIT_FORKNUM);

    ivfflat_build_destroy_ctx(ctx);
}

IvfflatBuildCtx
ivfflat_build_init_ctx(
    Relation heap,
    Relation index,
    IndexInfo *index_info,
    ForkNumber forkNum
)
{
    IvfflatBuildCtx ctx = (IvfflatBuildCtx) palloc(sizeof(IvfflatBuildCtxData));
    ctx->heap = heap;
    ctx->index = index;
    ctx->index_info = index_info;
    ctx->tupdesc = RelationGetDescr(index);
    ctx->vector_type = ivfflat_get_vector_type(index);

    ctx->list_count = IVFFLAT_DEFAULT_LISTS_COUNT;
    ctx->dimensions = TupleDescAttr(index->rd_att, 0)->atttypmod;
    if (ctx->dimensions > IVFFLAT_MAX_DIMENSIONS) {
        ereport(ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("dimensions must be <= %d for ivfflat index", IVFFLAT_MAX_DIMENSIONS)));
    }

    ctx->rel_tuple_count = 0;
    ctx->index_tuple_count = 0;

    ctx->vector_distance_proc = index_getprocinfo(index, 1, IVFFALT_VECTOR_DISTANCE_PROC);
    ctx->vector_normalize_proc = ivfflat_get_proc_info(index, IVFFALT_VECTOR_NORMALIZATION_PROC);
    ctx->vector_kmeans_normalize_proc = ivfflat_get_proc_info(index, IVFFALT_KMEANS_NORMALIZATION_PROC);
    ctx->collation = index->rd_indcollation[0];

    if(ctx->vector_kmeans_normalize_proc != NULL && ctx->dimensions == 1){
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
             errmsg("dimensions must be greater than one for this opclass")));
    }

    ctx->sort_desc = CreateTemplateTupleDesc(3);
    TupleDescInitEntry(ctx->sort_desc, (AttrNumber) 1, "list", INT4OID, -1, 0);
    TupleDescInitEntry(ctx->sort_desc, (AttrNumber) 2, "tid", TIDOID, -1, 0);
    TupleDescInitEntry(ctx->sort_desc, (AttrNumber) 3, "vector", TupleDescAttr(ctx->tupdesc, 0)->atttypid, -1, 0);

    ctx->sort_slot = MakeSingleTupleTableSlot(ctx->sort_desc, &TTSOpsVirtual);
    ctx->centers = array_create(
        ctx->list_count,
         ctx->dimensions, 
         ctx->vector_type->item_size(ctx->dimensions));
    ctx->list_infos = array_create(
        ctx->list_count,
        1,
        sizeof(ListInfoData));
    ctx->tmp_ctx = AllocSetContextCreate(
        CurrentMemoryContext,
        "ivfflat temp ctx",
        ALLOCSET_DEFAULT_SIZES);
    return ctx;
}

void
ivfflat_build_destroy_ctx(IvfflatBuildCtx ctx)
{
    array_destroy(ctx->centers);
    array_destroy(ctx->list_infos);
    MemoryContextDelete(ctx->tmp_ctx);
    pfree(ctx);
}

void
ivfflat_build_index(IvfflatBuildCtx ctx,ForkNumber fork_num){
    //step 1. calculate the centers
    ivfflat_calculate_centers(ctx);
    //step 2. create the meta page
    ivfflat_create_meta_page(
        ctx->index,
         ctx->dimensions,
          ctx->list_count,
           fork_num);
    //step 3. create the list pages
    ivfflat_create_list_pages(
        ctx->index,
        ctx->centers,
        ctx->dimensions,
        ctx->list_count,
        fork_num,
        ctx->list_infos);
    //step 4. create the entry pages
    ivfflat_create_entry_pages(ctx,fork_num);
    if(fork_num == INIT_FORKNUM){
        log_newpage_range(
            ctx->index,
            fork_num,
            0,
            RelationGetNumberOfBlocksInFork(ctx->index, fork_num),
            true
        );
    }
}

FmgrInfo *
ivfflat_get_proc_info(Relation index, uint16 procnum){
    if(!OidIsValid(index_getprocid(index,1,procnum))){
        return NULL;
    }
    return index_getprocinfo(index, 1, procnum);
}

void
ivfflat_calculate_centers(IvfflatBuildCtx ctx){
    int cnt;
    //1. samples
    cnt = ctx->list_count * 50;
    cnt = Max(cnt, 10000);

    //unlogged table
    if(ctx->heap == NULL){
        cnt = 1;
    }

    ctx->samples = array_create(
        cnt, 
        ctx->dimensions,
        ctx->centers->item_size);
    if(ctx->heap != NULL){
        //do sample
        ivfflat_sample_tuples(ctx);

        if(ctx->samples->length < ctx->list_count){
            elog(NOTICE, "ivfflat index created with little data");
            elog(NOTICE, "This will cause low recall.");
            elog(NOTICE, "Drop the index until the table has more data.");
        }
    }

    //2. calculate centers
    if(ctx->samples->length == 0){
        ivfflat_random_centers(ctx);
    }else{
        ivfflat_elkan_kmeans(
            ctx->index,
            ctx->samples,
            ctx->centers,
            ctx->vector_type
        );
    }
    

    array_destroy(ctx->samples);
    ctx->samples = NULL;
}

void 
ivfflat_sample_tuples_callback(
    Relation index,
    ItemPointer tid,
    Datum *values,
    bool *isnull,
    bool tuple_is_alive,
    void *state
){
    IvfflatBuildCtx ctx = (IvfflatBuildCtx) state;
    MemoryContext old_ctx;

    if(isnull[0]){
        return;
    }

    old_ctx = MemoryContextSwitchTo(ctx->tmp_ctx);
    
    ivfflat_sample_tuples_internal(ctx, values);

    MemoryContextSwitchTo(old_ctx);
    MemoryContextReset(ctx->tmp_ctx);
}

void
ivfflat_sample_tuples_internal(IvfflatBuildCtx ctx, Datum *values){
    int need_cnt = ctx->samples->max_length;

    Datum value = PointerGetDatum(PG_DETOAST_DATUM(values[0]));

    //球形距离需要单位向量
    if(ctx->vector_kmeans_normalize_proc != NULL){
        if(!ivfflat_norm_non_zero(
            ctx->vector_kmeans_normalize_proc, 
            ctx->collation, 
            value)){
            return;
        }
        value = ivfflat_normalize_value(
            ctx->vector_type,
             ctx->collation, 
             value);
    }

    //加入采样数组
    if(ctx->samples->length < need_cnt){
        array_copy(
            ctx->samples, 
            ctx->samples->length, 
            DatumGetPointer(value));
        ctx->samples->length++;
    }else{
        //计算要跳过的个数
        if(ctx->skip_count < 0){
            ctx->skip_count = reservoir_get_next_S(
                &ctx->resvr_state, 
                ctx->samples->length, 
                need_cnt);
        }

        if(ctx->skip_count <= 0){
            //保留此tuple
            //选择要替换的位置
            int k = (int)(need_cnt * sampler_random_fract(&ctx->resvr_state.randstate));
            array_copy(
                ctx->samples, 
                k, 
                DatumGetPointer(value));
        }

        //倒计
        ctx->skip_count--;
    }
}

void
ivfflat_sample_tuples(IvfflatBuildCtx ctx){
    int need_cnt = ctx->samples->max_length;
    BlockNumber total_blocks = RelationGetNumberOfBlocks(ctx->heap);

    ctx->skip_count = -1;

    //块采样。选择需要的块
    BlockSampler_Init(
        &ctx->block_sampler,
        total_blocks,
        need_cnt,
        RandomInt()
    );

    //蓄水池采样。选择需要的样本
    reservoir_init_selection_state(
        &ctx->resvr_state,
        need_cnt
    );

    //采样块
    while(BlockSampler_HasMore(&ctx->block_sampler)){
        BlockNumber block = BlockSampler_Next(&ctx->block_sampler);

        //采样块内的tuple
        table_index_build_range_scan(
            ctx->heap,
            ctx->index,
            ctx->index_info,
            false,
            true,
            false,
            block,
            1,
            ivfflat_sample_tuples_callback,
            (void *) ctx,
            NULL
        );
    }
}

void
ivfflat_random_centers(IvfflatBuildCtx ctx){
    float *temp = (float *) palloc(
        sizeof(float)*ctx->centers->dimensions);
    FmgrInfo *normalize_proc = ivfflat_get_proc_info(ctx->index, IVFFALT_VECTOR_NORMALIZATION_PROC);

    for(; ctx->centers->length < ctx->centers->max_length;){
        Pointer center = array_get(ctx->centers, ctx->centers->length);
        for(int i = 0; i < ctx->centers->dimensions; i++){
            temp[i] = RandomDouble();
        }
        ctx->vector_type->update_center(center, ctx->centers->dimensions, temp);
        ctx->centers->length++;
    }

    if(normalize_proc != NULL){
        ivfflat_normalize_centers(ctx->vector_type, ctx->collation, ctx->centers);
    }

}

void
ivfflat_normalize_centers(
    IvfflatVectorType vector_type,
    Oid collation,
    Array centers
){
    for(int i = 0; i < centers->max_length; i++){
        Datum center = PointerGetDatum(array_get(centers, i));
        Datum center2 = ivfflat_normalize_value(vector_type, collation, center);
        Size sz = VARSIZE_ANY(DatumGetPointer(center2));
        if(sz > centers->item_size){
            elog(ERROR, "center %d size %zu > %zu", i, sz, centers->item_size);
        }
        array_copy(centers, i, DatumGetPointer(center2));
    }
}

/*
choose centers from samples using kmeans++ seeding technique:

1a. Choose an initial center c1 uniformly at random from X .
1b. Choose the next center ci, selecting ci = x′ ∈ X  with probability D(x′)^2 / Sigma(D(x)^2) .
    let D(x) denote the shortest distance from a data point x to the closest center we have already chosen.
1c. Repeat Step 1b until we have chosen a total of k centers. 
2-4. Proceed as with the standard k-means algorithm.
*/
void 
ivfflat_kmeans_plusplus(
    Relation index,
    Array samples,
    Array centers,
    float *lower_bounds //x_j distance to centers[i]
){
    Oid collation;
    int64 i;
    int j;
    int num_centers = centers->max_length;
    int num_samples = samples->length;
    float *weight;//D(x) = min_i=1,...,k ||x - c_i||^2
    FmgrInfo *distance_proc = ivfflat_get_proc_info(
        index,
    IVFFALT_KMEANS_DISTANCE_PROC);
    weight = palloc(num_samples * sizeof(float));
    collation = index->rd_indcollation[0];

    //step 1a. Choose an initial center c1 uniformly at random from X .
    array_copy(
        centers, 
        0, 
        array_get(samples, RandomInt() % samples->length));
    centers->length++;

    for(i = 0; i < num_samples; i++){
        weight[i] = FLT_MAX;
    }

    //evaluate the next center
    for(i = 0; i < num_centers; i++){
        double		sum;
		double		choice;
		CHECK_FOR_INTERRUPTS();

		sum = 0.0;

        for(j=0; j<num_samples; j++){
            //evaluate D(x) -- the shortest distance from a data point x 
            // to the closest center we have already chosen.
            Datum vec = PointerGetDatum(
                array_get(samples,j));//sample_j
            double distance = DatumGetFloat8(
                FunctionCall2Coll(
                    distance_proc,
                    collation,
                    vec,//sample_j
                    PointerGetDatum(
                        array_get(centers,i))//centers[i]
                )
            );
            lower_bounds[j * num_centers + i] = distance;

            //evaluate the shortest squared distance
            distance *= distance;
            if (distance < weight[j]){
                weight[j] = distance;
            }
            sum += weight[j];
        }

        if(i == num_centers - 1){//all centers have been chosen
            break;
        }

        //step 1b. Choose the next center ci, selecting ci = x′ ∈ X  
        // with probability D(x′)^2 / Sigma(D(x)^2) .
        choice = RandomDouble() * sum;
        for(j = 0; j < num_samples - 1; j++){
            choice -= weight[j];
            if(choice <= 0.0){
                break;
            }
        }
        //must be a center been chosen
        array_copy(
            centers,
            i + 1,//next centers[i+1]
            array_get(samples,j)//sample_j
        );
        centers->length++;
    }

    pfree(weight);
}

/*
Triangle Inequality algorithm avoids unnecessary distance calculations 
by applying the triangle inequality in two different ways, 
and by keeping track of lower and upper bounds for distances between points and centers.
*/
void
ivfflat_elkan_kmeans(
    Relation index,
    Array samples,
    Array centers,
    const IvfflatVectorType vector_type
){
    FmgrInfo *distance_proc;
    FmgrInfo *normalize_proc;
    Oid collation;
    float *lower_bounds;//l(x,c)
    float *upper_bounds;//u(x,c)
    int *closest_centers;
    float *half_distances;//d(c_i,c_j)/2
    float *s;//s(c) = min(half_distances)
    float *agg;//agg[i * dimensions] = sum of samples closest to center[i]
    Array new_centers;//mean(c)
    int *center_counts;//center_counts[i] = count of samples closest to center[i]
    int iteration;
    float *new_d;//d(c,mean(c))
    int num_centers = centers->max_length;

    Size sample_size;
    Size centers_size;
    Size new_centers_size;
    Size agg_size;
    Size center_counts_size;
    Size closest_centers_size;
    Size lower_bounds_size;
    Size upper_bounds_size;
    Size s_size;
    Size half_dist_size;
    Size new_dist_size;
    Size t_size;

    //step 0: prepare
    sample_size = ARRAY_SIZE(samples->max_length, samples->item_size);
    centers_size = ARRAY_SIZE(centers->max_length, centers->item_size);
    new_centers_size = ARRAY_SIZE(num_centers, centers->item_size);
    agg_size = sizeof(float) * (int64)num_centers * (int64)centers->dimensions;
    center_counts_size = sizeof(int) * (int64)num_centers;
    closest_centers_size = sizeof(int) * (int64)samples->length;
    lower_bounds_size = sizeof(float) * (int64)samples->length * (int64)num_centers;
    upper_bounds_size = sizeof(float) * (int64)samples->length;
    s_size = sizeof(float) * (int64)num_centers;
    half_dist_size = sizeof(float) * (int64)num_centers * (int64)num_centers;
    new_dist_size = sizeof(float) * (int64)num_centers;
    
    t_size = sample_size + 
        centers_size + 
        new_centers_size + 
        agg_size + 
        center_counts_size + 
        closest_centers_size + 
        lower_bounds_size + 
        upper_bounds_size + 
        s_size + 
        half_dist_size + 
        new_dist_size;

    if(t_size > (Size)maintenance_work_mem * 1024L){
        ereport(ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
             errmsg("memory %zu MB > %d MB",
                    t_size / (1024 * 1024) + 1, maintenance_work_mem / 1024)));
    }

    if((int64)num_centers*(int64)num_centers > INT_MAX){
        elog(ERROR, "centers count overflow");
    }

    distance_proc = index_getprocinfo(
        index,1, IVFFALT_KMEANS_DISTANCE_PROC);
    normalize_proc = ivfflat_get_proc_info(
        index, IVFFALT_KMEANS_NORMALIZATION_PROC);
    collation = index->rd_indcollation[0];

    agg = palloc(agg_size);
    center_counts = palloc(center_counts_size);
    closest_centers = palloc(closest_centers_size);
    lower_bounds = palloc_extended(lower_bounds_size, MCXT_ALLOC_HUGE);
    upper_bounds = palloc(upper_bounds_size);
    s = palloc(s_size);
    half_distances = palloc_extended(half_dist_size, MCXT_ALLOC_HUGE);
    new_d = palloc(new_dist_size);
    
    new_centers = array_create(
        num_centers,
        centers->dimensions,
        centers->item_size
    );
    new_centers->length = num_centers;
    
    //step 1. Init
    //step 1.1 choose init centers
    ivfflat_kmeans_plusplus(
        index,
        samples,
        centers,
        lower_bounds
    );

    //step 1.2 assign sample_j to its closest center[i]
    for(int j = 0; j < samples->length; j++){
        float min_distance = FLT_MAX;
        int closest_center = 0;

        for(int i = 0; i < num_centers; i++){
            float distance = lower_bounds[j * num_centers + i];//sample_j distance to centers[i]
            if(distance < min_distance){
                min_distance = distance;
                closest_center = i;
            }
        }
        upper_bounds[j] = min_distance;
        closest_centers[j] = closest_center;
    }

    //step 2. Update
    iteration = 0;
    while(iteration < 500){
        int changes = 0;
        bool reset;

        CHECK_FOR_INTERRUPTS();

        //algorithm step1.
        //step 2.1 evaluate distance between all centers
        for(int i = 0; i < num_centers; i++){
            //center[i]
            Datum center = PointerGetDatum(array_get(centers, i));
            for(int j = i+1; j < num_centers; j++){
                float distance = DatumGetFloat8(
                    FunctionCall2Coll(
                        distance_proc,
                        collation,
                        center,//center i
                        PointerGetDatum(
                            array_get(centers, j)//center j
                        )
                    )
                )/2;
                half_distances[i * num_centers + j] = distance;
                half_distances[j * num_centers + i] = distance;
            }
        }

        //step 2.2 evaluate s(c) = min(half_distances)
        for(int i = 0; i < num_centers; i++){
            float min_distance = FLT_MAX;
            for(int j = 0; j < num_centers; j++){
                float d;
                if(i == j){
                    continue;
                }
                d = half_distances[i * num_centers + j];
                if(d < min_distance){
                    min_distance = d;
                }
            }
            s[i] = min_distance;
        }

        reset = iteration != 0;

        //step 2.3 assign sample_j to its closest center[i]
        for(int j = 0; j < samples->length; j++){
            bool jreset;
            //alogrithm step2. skip u(sample_j) <= s(c(sample_j))
            if(upper_bounds[j] <= s[closest_centers[j]]){
                continue;
            }

            jreset = reset;

            //
            for(int i = 0; i < num_centers; i++){
                Datum sample_j;
                float d;

                /*
                skip centers:
                algorithm step3(1) i == closest_centers[j]
                algorithm step3(2) u(sample_j) <= l(sample_j,c_i)
                algorithm step3(3) u(sample_j) <= s(closest_centers[j],i)
                */
                if(i == closest_centers[j]){
                    continue;
                }
                if(upper_bounds[j] <= lower_bounds[j * num_centers + i]){
                    continue;
                }
                if(upper_bounds[j] <= half_distances[closest_centers[j] * num_centers + i]){
                    continue;
                }

                //algorithm step3a.
                sample_j = PointerGetDatum(array_get(samples, j));
                if(jreset){
                    d = DatumGetFloat8(
                        FunctionCall2Coll(
                            distance_proc, 
                            collation, 
                            sample_j, 
                            PointerGetDatum(
                                array_get(centers, 
                                    closest_centers[j]))
                            )
                        );
                    lower_bounds[j * num_centers + closest_centers[j]] = d;
                    upper_bounds[j] = d;
                    jreset = false;
                }else{
                    d = upper_bounds[j];
                }

                //algorithm step3b.
                if(d > lower_bounds[j * num_centers + i] ||
                    d > half_distances[closest_centers[j] * num_centers + i]){
                    float d2 = DatumGetFloat8(
                        FunctionCall2Coll(
                            distance_proc, 
                            collation, 
                            sample_j, 
                            PointerGetDatum(
                                array_get(centers, i))
                            )
                        );
                    lower_bounds[j * num_centers + i] = d2;
                    if(d2 < d){
                        closest_centers[j] = i;
                        upper_bounds[j] = d2;
                        changes++;
                    }
                }
            }
        }

        //algorithm step4. evaluate the mean center-- m(c)
        ivfflat_new_centers(
            samples,
            agg,
            new_centers,
            center_counts,
            closest_centers,
            normalize_proc,
            collation,
            vector_type
        );

        //algorithm step5. update lower_bounds
        // evaluete the distance between the mean center 
        // and all centers
        for(int i = 0; i < num_centers; i++){
            Datum center = PointerGetDatum(array_get(centers, i));
            Datum new_center = PointerGetDatum(array_get(new_centers, i));
            new_d[i] = DatumGetFloat8(
                FunctionCall2Coll(
                    distance_proc,
                    collation,
                    center,
                    new_center
            ));
        }

        for(int j = 0; j < samples->length; j++){
            for(int i = 0; i < num_centers; i++){
                float d = lower_bounds[j * num_centers+i] - new_d[i];
                if(d < 0){
                    d = 0;
                }
                lower_bounds[j * num_centers+i] = d;
            }
        }

        //algorithm step6. update upper_bounds
        for(int j = 0; j < samples->length; j++){
            upper_bounds[j] += new_d[closest_centers[j]];
        }

        //algorithm step7. replace c by mean(c)
        for(int i = 0; i < num_centers; i++){
            array_copy(
                centers,
                i,
                array_get(new_centers, i)
            );
        }
        if(changes == 0 && iteration != 0){
            break;
        }

        iteration++;
    }
}

void ivfflat_new_centers(
    Array samples,
    float *agg,
    Array new_centers,
    int *center_counts,
    int *closest_centers,
    FmgrInfo *normalize_proc,
    Oid collation,
    IvfflatVectorType vector_type
){
    //sum = 0, count = 0
    for(int i = 0; i < new_centers->length; i++){
        float *data= agg + ((int64)i * (int64)new_centers->dimensions);
        for(int j = 0; j < new_centers->dimensions; j++){
            data[j] = 0.0;
        }
        center_counts[i] = 0;
    }

    //sum samples closest to the center
    ivfflat_sum_centers(
        samples,
        agg,
        closest_centers,
        vector_type
    );

    //sum count of samples closest to the center
    for(int j = 0; j < samples->length; j++){
        center_counts[closest_centers[j]]++;
    }

    //evaluate the new centers -- mean center
    for(int i = 0; i < new_centers->length; i++){
        float *data = agg + ((int64)i * (int64)new_centers->dimensions);
        if(center_counts[i] > 0){
            for(int j = 0; j < new_centers->dimensions; j++){
                if(isinf(data[j])){
                    data[j] = data[j] > 0 ? FLT_MAX : -FLT_MAX;
                }
            }

            for(int j = 0; j < new_centers->dimensions; j++){
                data[j] /= center_counts[i];
            }
        }else{
            for(int j = 0; j < new_centers->dimensions; j++){
                data[j] = RandomDouble();
            }
        }
    }

    //update the centers
    ivfflat_update_centers(
        agg,
        new_centers,
        vector_type
    );

    if(normalize_proc != NULL){
        ivfflat_normalize_centers(vector_type, collation, new_centers);
    }
}

void
ivfflat_sum_centers(
    Array samples,
    float *agg,
    int *closest_centers,
    IvfflatVectorType vector_type
){
    //add sample_j to its closest center sum
    for(int j = 0; j < samples->length; j++){
        float *data = agg + ((int64)closest_centers[j] * 
            (int64)samples->dimensions);
        vector_type->sum_center(
            array_get(samples, j),
            data
        );
    }
}

void
ivfflat_update_centers(
    float *agg,
    Array centers,
    IvfflatVectorType vector_type
){
    for(int i = 0; i < centers->length; i++){
        float *data = agg + ((int64)i * (int64)centers->dimensions);
        vector_type->update_center(
            array_get(centers, i),
            centers->dimensions,
            data
        );
    }
}

void 
ivfflat_create_meta_page(
    Relation index, 
    int dimensions,
    int list_count,
    ForkNumber forkNum
){
    Buffer buf ;
    Page page;
    GenericXLogState *state;
    IvfflatMetaPage meta;
    buf= ivfflat_new_buffer(index, forkNum);
    ivfflat_start_xlog(index, &buf, &page, &state);
    meta = IvfflatPageGetMeta(page);
    meta->version = IVFFLAT_VERSION;
    meta->dimensions = dimensions;
    meta->list_count = list_count;
    ((PageHeader) page)->pd_lower =
        ((char *) meta + sizeof(IvfflatMetaPageData)) - (char *) page;
    ivfflat_commit_xlog(buf, state);
}

void
ivfflat_create_list_pages(
    Relation index,
    Array centers,
    int dimensions,
    int list_count,
    ForkNumber fork_num,
    Array list_infos
){
    Size list_size;
    IvfflatList list_entry;
    Buffer buf;
    Page page;
    GenericXLogState *state;
    Pointer center;
    ListInfo list_info;

    list_size = MAXALIGN(IVFFLAT_LIST_SIZE(centers->item_size));
    list_entry = (IvfflatList) palloc0(list_size);

    buf = ivfflat_new_buffer(index, fork_num);
    ivfflat_start_xlog(index, &buf, &page, &state);

    for(int i = 0; i < list_count; i++){
        OffsetNumber offno;
        MemSet(list_entry,0, list_size);

        list_entry->start_page = InvalidBlockNumber;
        list_entry->insert_page = InvalidBlockNumber;
        center = array_get(centers, i);
        memcpy(
            &list_entry->center, 
            center, 
            VARSIZE_ANY(center));

        if(PageGetFreeSpace(page) < list_size){
            ivfflat_append_page(index, &buf, &page, &state, fork_num);
        }

        offno = PageAddItem(page, (Item) list_entry, list_size, InvalidOffsetNumber, false, false);
        if (offno == InvalidOffsetNumber){
            elog(ERROR, "failed to add list entry to page");
        }
        list_info = (ListInfo) array_get(list_infos, i);
        list_info->blknum = BufferGetBlockNumber(buf);
        list_info->offnum = offno;
    }

    ivfflat_commit_xlog(buf, state);
    pfree(list_entry);
}

void
ivfflat_create_entry_pages(IvfflatBuildCtx ctx, ForkNumber fork_num){
    ivfflat_scan_tuples(ctx);
    tuplesort_performsort(ctx->sort_state);
    ivfflat_insert_tuples(ctx,fork_num);
    tuplesort_end(ctx->sort_state);
}

void
ivfflat_scan_tuples(IvfflatBuildCtx ctx){
    ctx->sort_state = ivfflat_init_sort_state(
        ctx->sort_desc,
        maintenance_work_mem,
        NULL
    );
    if(ctx->heap != NULL){
        ctx->rel_tuple_count = table_index_build_scan(
            ctx->heap,
            ctx->index,
            ctx->index_info,
            true,
            true,
            ivfflat_sort_tuples_callback,
            (void *) ctx,
            NULL
        );
    }
}

Tuplesortstate *
ivfflat_init_sort_state(TupleDesc tupdesc,int memory, SortCoordinate coordinate){
    AttrNumber	attNums[] = {1};
	Oid			sortOperators[] = {Int4LessOperator};
	Oid			sortCollations[] = {InvalidOid};
	bool		nullsFirstFlags[] = {false};

	return tuplesort_begin_heap(
        tupdesc,
        1,
        attNums,
        sortOperators,
        sortCollations,
        nullsFirstFlags,
        memory,
        coordinate,
        false);
}

void
ivfflat_sort_tuples_callback(
    Relation index,
    ItemPointer tid,
    Datum *values,
    bool *isnull,
    bool tupleIsAlive,
    void *state){
    IvfflatBuildCtx ctx = (IvfflatBuildCtx) state;
    MemoryContext old_ctx;

    if(isnull[0]){
        return;
    }

    old_ctx = MemoryContextSwitchTo(ctx->tmp_ctx);

    ivfflat_sort_tuples(index, tid, values, ctx);

    MemoryContextSwitchTo(old_ctx);
	MemoryContextReset(ctx->tmp_ctx);
}

void
ivfflat_sort_tuples(
    Relation index,
    ItemPointer tid,
    Datum *values,
    IvfflatBuildCtx ctx){
    double distance;
    double min_distance = DBL_MAX;
    int closest_center = 0;
    Pointer center;
    Datum   value;
    value = PointerGetDatum(PG_DETOAST_DATUM(values[0]));

    if(ctx->vector_normalize_proc != NULL){
        if(!ivfflat_norm_non_zero(ctx->vector_normalize_proc, ctx->collation, value)){
            return;
        }
        value = ivfflat_normalize_value(ctx->vector_type, ctx->collation, value);
    }

    for(int i = 0; i < ctx->centers->length; i++){
        center = array_get(ctx->centers, i);
        distance = DatumGetFloat8(FunctionCall2Coll(
            ctx->vector_distance_proc,
            ctx->collation,
            value,
            PointerGetDatum(center)
        ));
        if(distance < min_distance){
            min_distance = distance;
            closest_center = i;
        }
    }

    //fill tuple
    ExecClearTuple(ctx->sort_slot);
    ctx->sort_slot->tts_values[0] = Int32GetDatum(closest_center);
    ctx->sort_slot->tts_isnull[0] = false;
    ctx->sort_slot->tts_values[1] = PointerGetDatum(tid);
    ctx->sort_slot->tts_isnull[1] = false;
    ctx->sort_slot->tts_values[2] = value;
    ctx->sort_slot->tts_isnull[2] = false;
    ExecStoreVirtualTuple(ctx->sort_slot);
    tuplesort_puttupleslot(ctx->sort_state, ctx->sort_slot);

    ctx->index_tuple_count++;
}

void
ivfflat_insert_tuples(IvfflatBuildCtx ctx, ForkNumber fork_num){
    TupleTableSlot *slot;
    TupleDesc tupdesc;
    IndexTuple itup;
    int list_no;
    ListInfo list_info;

    slot = MakeSingleTupleTableSlot(
        ctx->sort_desc,
        &TTSOpsMinimalTuple
    );
    tupdesc = ctx->tupdesc;

    ivfflat_get_next_tuple(
        ctx->sort_state, 
        tupdesc,
        slot,
        &itup,
        &list_no);

    for(int i = 0; i < ctx->centers->length; i++){
        Buffer buf;
        Page page;
        GenericXLogState *state;
        BlockNumber start_page;
        BlockNumber insert_page;
        OffsetNumber offno;

        CHECK_FOR_INTERRUPTS();

        buf = ivfflat_new_buffer(ctx->index, fork_num);
        ivfflat_start_xlog(ctx->index, &buf, &page, &state);

        start_page = BufferGetBlockNumber(buf);
        while(list_no == i){
            Size    itemsz = MAXALIGN(IndexTupleSize(itup));
            if(PageGetFreeSpace(page) < itemsz){
                ivfflat_append_page(
                    ctx->index,
                    &buf,
                    &page,
                    &state,
                    fork_num);
            }
            offno = PageAddItem(
                page,
                (Item) itup,
                itemsz,
                InvalidOffsetNumber,
                false,
                false);
            if(offno == InvalidOffsetNumber){
                elog(ERROR, "failed to add list entry to page");
            }

            pfree(itup);

            ivfflat_get_next_tuple(
                ctx->sort_state,
                tupdesc,
                slot,
                &itup,
                &list_no);
        }
        insert_page = BufferGetBlockNumber(buf);
        ivfflat_commit_xlog(buf, state);
        list_info = (ListInfo) array_get(ctx->list_infos, i);
        ivfflat_update_list(
            ctx->index,
            list_info,
            insert_page,
            InvalidBlockNumber,
            start_page,
            fork_num
        );
    }
}

void
ivfflat_get_next_tuple(
    Tuplesortstate *sort_state,
    TupleDesc tupdesc,
    TupleTableSlot *slot,
    IndexTuple *itup,
    int *list_no
){
    if(tuplesort_gettupleslot(sort_state, true, false, slot, NULL)){
        Datum value;
        bool isnull;

        //sort desc  1: list_no, 2: tid, 3: vector(values[0])
        *list_no = DatumGetInt32(slot_getattr(slot, 1, &isnull));
        value = slot_getattr(slot, 3, &isnull);
        *itup = index_form_tuple(tupdesc, &value, &isnull);
        (*itup)->t_tid = *((ItemPointer) DatumGetPointer(
            slot_getattr(slot, 2, &isnull)));
    }else{
        *list_no = -1;
    }
}

void
ivfflat_update_list(
    Relation index,
    ListInfo list_info,
    BlockNumber insert_page,
    BlockNumber original_insert_page,
    BlockNumber start_page,
    ForkNumber fork_num
){
    Buffer buf;
    Page page;
    GenericXLogState *state;
    IvfflatList list;
    bool changed = false;

    buf = ReadBufferExtended(
    index,
    fork_num,
    list_info->blknum,
    RBM_NORMAL,
    NULL);
    LockBuffer(buf, BUFFER_LOCK_EXCLUSIVE);
    state = GenericXLogStart(index);
    page = GenericXLogRegisterBuffer(state, buf, 0);
    list = (IvfflatList) PageGetItem(page, 
        PageGetItemId(page, 
            list_info->offnum));
    
    if(BlockNumberIsValid(insert_page) && 
        insert_page != list->insert_page){
        if(!BlockNumberIsValid(original_insert_page) || 
            insert_page >= original_insert_page){
            list->insert_page = insert_page;
            changed = true;
        }
    }

    if(BlockNumberIsValid(start_page) && start_page != list->start_page){
        list->start_page = start_page;
        changed = true;
    }

    if(changed){
        ivfflat_commit_xlog(buf, state);
    }else{
        ivfflat_abort_xlog(buf, state);
    }
}