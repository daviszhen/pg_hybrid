#include "ivffat.h"
#include "ivfflat_options.h"
#include "storage/lockdefs.h"
#include "utils/guc.h"

int ivfflat_probes;
int ivfflat_iterative_scan;
int ivfflat_max_probes;
static relopt_kind ivfflat_relopt_kind;

static const struct config_enum_entry ivfflat_iterative_scan_options[] = {
	{"off", IVFFLAT_ITERATIVE_SCAN_OFF, false},
	{"relaxed_order", IVFFLAT_ITERATIVE_SCAN_RELAXED, false},
	{NULL, 0, false}
};


void ivfflat_init_options(void){
    ivfflat_relopt_kind = add_reloption_kind();

    add_int_reloption(
        ivfflat_relopt_kind,
        "lists",
        "Number of inverted lists",
        IVFFLAT_DEFAULT_LIST_COUNT,
        IVFFLAT_MIN_LIST_COUNT,
        IVFFLAT_MAX_LIST_COUNT,
        AccessExclusiveLock
    );


    DefineCustomIntVariable(
    "pg_hybrid_ivfflat.probes",
    "Sets the number of probes",
    "Valid range is 1..lists.",
     &ivfflat_probes,
    IVFFLAT_DEFAULT_PROBES,
    IVFFLAT_MIN_LIST_COUNT,
    IVFFLAT_MAX_LIST_COUNT,
    PGC_USERSET, 0, NULL, NULL, NULL);

    DefineCustomEnumVariable(
    "pg_hybrid_ivfflat.iterative_scan", 
    "Sets the mode for iterative scans",
    NULL,
    &ivfflat_iterative_scan,
    IVFFLAT_ITERATIVE_SCAN_OFF,
    ivfflat_iterative_scan_options, 
    PGC_USERSET, 0, NULL, NULL, NULL);

    DefineCustomIntVariable(
    "pg_hybrid_ivfflat.max_probes", 
    "Sets the max number of probes for iterative scans",
    NULL, 
    &ivfflat_max_probes,
    IVFFLAT_MAX_LIST_COUNT, 
    IVFFLAT_MIN_LIST_COUNT, 
    IVFFLAT_MAX_LIST_COUNT, 
    PGC_USERSET, 0, NULL, NULL, NULL);

    MarkGUCPrefixReserved("pg_hybrid_ivfflat");
}

bytea *
ivfflat_options(Datum reloptions, bool validate){
    static const relopt_parse_elt tab[] = {
		{
            "lists",
             RELOPT_TYPE_INT,
              offsetof(IvfflatOptions, list_count)},
	};

    return (bytea *) build_reloptions(
        reloptions,
        validate,
         ivfflat_relopt_kind, 
         sizeof(IvfflatOptions),
          tab,
           lengthof(tab));
}