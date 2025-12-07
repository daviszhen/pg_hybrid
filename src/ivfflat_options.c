#include "ivffat.h"
#include "ivfflat_options.h"
#include "storage/lockdefs.h"
#include "utils/guc.h"

int ivfflat_probes;
relopt_kind ivfflat_relopt_kind;

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
    "ivfflat.probes",
    "Sets the number of probes",
    "Valid range is 1..lists.",
     &ivfflat_probes,
    IVFFLAT_DEFAULT_PROBES,
    IVFFLAT_MIN_LIST_COUNT,
    IVFFLAT_MAX_LIST_COUNT,
    PGC_USERSET, 0, NULL, NULL, NULL);
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