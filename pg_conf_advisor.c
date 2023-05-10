#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/guc_tables.h"
#include "storage/fd.h"
#include "miscadmin.h"

#include "funcapi.h"
#include <sys/sysinfo.h>
#include <sys/statvfs.h>

PG_MODULE_MAGIC;

typedef enum pgca_var_type
{
    PGCA_INT = 0,
    PGCA_DOUBLE,
    PGCA_BOOL,
    PGCA_UNSUPPORTED
} pgca_var_type;

typedef struct pgca_conf_data
{
    char *name;
    int index;
    pgca_var_type var_type;
} pgca_conf_data;

typedef struct pgca_sys_resources
{
    uint32 cpus;
    uint32 cpu_cores;
    uint64 total_ram;
    uint64 free_ram;
    uint64 total_disk_space;
    uint64 free_disk_space;
    float percentage_free_ram;
    float percentage_free_disk_space;  
} pgca_sys_resources;

typedef struct pgca_cost_data
{
	Cost		seq_page_cost;      /* 1.0 default */
	Cost		random_page_cost;   /* 1.1 */
	Cost		cpu_tuple_cost;
	Cost		cpu_index_tuple_cost;
	Cost		cpu_operator_cost;
	Cost		parallel_setup_cost;
	Cost		parallel_tuple_cost;
} pgca_cost_data;

typedef struct pgca_mem_data
{
	double		shared_buffers;
	double		temp_buffers;
	double		work_mem;
	double		maintenance_work_mem;
	double		autovacuum_work_mem;
} pgca_mem_data;

// #checkpoint_timeout = 5min		# range 30s-1d
// => Change default to 30mins
// #checkpoint_completion_target = 0.9	# checkpoint target duration, 0.0 - 1.0
// #checkpoint_flush_after = 256kB		# measured in pages, 0 disables
// #max_wal_size = 2GB
// 

#define SHARED_BUFFERS "shared_buffers"
#define RANDOM_PAGE_COST "random_page_cost"

#define NUM_PG_SETTINGS_ATTS 17
#define PGCA_NUM_ATTS     3

#define PGCA_MAX_UNIT_LEN 3

/* GLOBAL VARIABLES */
pgca_sys_resources sys_res;
List *pgca_conf_list;

void _PG_init(void);

PG_FUNCTION_INFO_V1(pgca_get_system_info);
PG_FUNCTION_INFO_V1(pg_conf_advisor);
// PG_FUNCTION_INFO_V1(pg_conf_advisor_set_params);

static void pgca_get_cpu_info(void);
static void pgca_get_mem_info(void);
static void pgca_get_disk_info(char *config_data_path);

static char* SuggestedSharedBuffer(char *str_value);

static void pgca_parse_int_units(int *i, char *unit, char* str_value);
static char* pgca_double_to_text(double d);
// static char* pgca_int_to_text(int i);
static char* pgca_int_unit_to_text(int i, char *unit);


void
_PG_init(void)
{
    MemoryContext oldcxt;
    pgca_conf_data *conf_data;
    ListCell *lc = NULL;
    int i;
    int num_config;

    oldcxt = MemoryContextSwitchTo(TopMemoryContext);

    conf_data = (pgca_conf_data *) palloc0(sizeof(pgca_conf_data));
    conf_data->name = palloc0(NAMEDATALEN);
    snprintf(conf_data->name, NAMEDATALEN, SHARED_BUFFERS);
    pgca_conf_list = lappend(pgca_conf_list, conf_data);

    conf_data = (pgca_conf_data *) palloc0(sizeof(pgca_conf_data));
    conf_data->name = palloc0(NAMEDATALEN);
    snprintf(conf_data->name, NAMEDATALEN, RANDOM_PAGE_COST);
    pgca_conf_list = lappend(pgca_conf_list, conf_data);

    num_config = GetNumConfigOptions();
    for(i = 0; i < num_config; i++)
    {
        char *pg_conf_values[NUM_PG_SETTINGS_ATTS];
        bool noshow;

        GetConfigOptionByNum(i, (const char **)pg_conf_values, &noshow);

        foreach(lc, pgca_conf_list)
        {
            pgca_conf_data *d = (pgca_conf_data *) lfirst(lc);

            if (strcmp(pg_conf_values[0], d->name) == 0)
            {
                d->index = i;

                if (strcmp(pg_conf_values[7], "integer") == 0)
                    d->var_type = PGCA_INT;
                else if (strcmp(pg_conf_values[7], "real") == 0)
                    d->var_type = PGCA_DOUBLE;
                else if (strcmp(pg_conf_values[7], "bool") == 0)
                    d->var_type = PGCA_BOOL;
                else
                    d->var_type = PGCA_UNSUPPORTED;
            }
        }
    }

    MemoryContextSwitchTo(oldcxt);
}

Datum
pg_conf_advisor(PG_FUNCTION_ARGS)
{
    char *config_data_path = ".";
    ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
    // TupleDesc tupdesc;
    ListCell *lc = NULL;

    InitMaterializedSRF(fcinfo, 0);

    pgca_get_cpu_info();
    pgca_get_mem_info();
    pgca_get_disk_info(config_data_path);

    foreach(lc, pgca_conf_list)
    {
        int i = 0;
        Datum values[PGCA_NUM_ATTS];
        bool nulls[PGCA_NUM_ATTS] = {false, false, false};
        pgca_conf_data *conf_data;
        char *str_value = NULL;
        char *str_value_suggested = NULL;

        conf_data = (pgca_conf_data *) lfirst(lc);
        str_value = GetConfigOptionByName(conf_data->name, NULL, true);

        // elog(INFO, "%s => %s", conf_data->name, str_value);

        // if (strcmp(str_value, SHARED_BUFFERS) == 0)
        // {
        // elog(INFO, "1");
        //     str_value_suggested = pstrdup(str_value); //SuggestedSharedBuffer(str_value);
        // }
        // else if (strcmp(str_value, RANDOM_PAGE_COST) == 0)
        // {
        // elog(INFO, "2");
        //     str_value_suggested = pstrdup(str_value); //pgca_double_to_text(1.1);
        // }

        // elog(INFO, "%s => %d => %d => %d", conf_data->name, conf_data->index, NBuffers, (Size) BLCKSZ);
        values[i++] = CStringGetTextDatum(conf_data->name);
        values[i++] = CStringGetTextDatum(str_value);
        // elog(INFO, "3 = %s", str_value_suggested);
        values[i++] = CStringGetTextDatum(str_value);
        // elog(INFO, "4");

        tuplestore_putvalues(rsinfo->setResult, rsinfo->setDesc, values, nulls);
    }

    return (Datum) 0;
}

// Datum
// pg_conf_advisor_set_params(PG_FUNCTION_ARGS)
// {
//     char *params = text_to_cstring(PG_GETARG_TEXT_P(0));
// }

Datum
pgca_get_system_info(PG_FUNCTION_ARGS)
{
    char *config_data_path = text_to_cstring(PG_GETARG_TEXT_P(0));
    TupleDesc tuple_desc;
    HeapTuple tuple;
    Datum values[8];
    bool nulls[8] = {false};

    pgca_get_cpu_info();
    pgca_get_mem_info();
    pgca_get_disk_info(config_data_path);

    if (get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
    {
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("function returning record called in context that cannot accept type record")));
    }

    values[0] = Int32GetDatum(sys_res.cpus);
    values[1] = Int32GetDatum(sys_res.cpu_cores);
    values[2] = Int64GetDatum(sys_res.total_ram);
    values[3] = Int64GetDatum(sys_res.free_ram);
    values[4] = Float4GetDatum(sys_res.percentage_free_ram);
    values[5] = Int64GetDatum(sys_res.total_disk_space);
    values[6] = Int64GetDatum(sys_res.free_disk_space);
    values[7] = Float4GetDatum(sys_res.percentage_free_disk_space);

    tuple = heap_form_tuple(tuple_desc, values, nulls);

    PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}

void
pgca_get_mem_info(void)
{
    struct sysinfo sys_info;

    if (sysinfo(&sys_info) == -1)
    {
        ereport(ERROR,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_EXCEPTION),
                 errmsg("failed to read system information")));
    }

    sys_res.total_ram = sys_info.totalram;
    sys_res.free_ram = sys_info.freeram;
    sys_res.percentage_free_ram = (sys_res.free_ram * 100.0) / sys_res.total_ram;
}

void
pgca_get_disk_info(char *config_data_path)
{
    struct statvfs disk_info;

    if (statvfs(config_data_path, &disk_info) == -1)
    {
        ereport(ERROR,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_EXCEPTION),
                 errmsg("failed to read disk information")));
    }

    sys_res.total_disk_space = disk_info.f_blocks * disk_info.f_frsize;
    sys_res.free_disk_space = disk_info.f_bfree * disk_info.f_frsize;
    sys_res.percentage_free_disk_space = (sys_res.free_disk_space * 100.0) / sys_res.total_disk_space;
}

void
pgca_get_cpu_info(void)
{
    FILE *cpuinfo;
    char buffer[1024];
    int cpus = 0;
    int cores = 0;

    cpuinfo = fopen("/proc/cpuinfo", "rb");

    if (cpuinfo == NULL)
    {
        perror("Error opening /proc/cpuinfo");
    }
    else
    {
        while (fgets(buffer, sizeof(buffer), cpuinfo))
        {
            char *ptr = strchr(buffer, ':');
            if (ptr != NULL)
            {
                *ptr++ = '\0';
                while (*ptr == ' ' || *ptr == '\t')
                    ptr++;

                if (strncmp(buffer, "processor", 9) == 0)
                {
                    cpus++;
                }
                else if (strncmp(buffer, "cpu cores", 9) == 0)
                {
                    cores += atoi(ptr);
                }
            }
        }

        fclose(cpuinfo);
    }

    sys_res.cpus = cpus;
    sys_res.cpu_cores = cores;
}

/*
# Memory units:  B  = bytes            Time units:  us  = microseconds
#                kB = kilobytes                     ms  = milliseconds
#                MB = megabytes                     s   = seconds
#                GB = gigabytes                     min = minutes
#                TB = terabytes                     h   = hours
#                                                   d   = days
*/

void
pgca_parse_int_units(int *i, char *unit, char* str_value)
{
    int parse_count;

    unit[0] = '\0';
    parse_count = sscanf(str_value, "%d%s", i, unit);

    if (parse_count == 0)
    {
        /* elog report */
    }
    else if (parse_count == 1)
    {
        /* nothing to do here */
    }
    else /* count = 2 */
    {
        if (strcmp(unit, "B") == 0)
            ; /* Nothing to do here */
        else if (strcmp(unit, "kB") == 0)
            *i = 1024;
        else if (strcmp(unit, "MB") == 0)
            *i = 1024 * 1024;
        else if (strcmp(unit, "GB") == 0)
            *i = 1024 * 1024 * 1024;
        // else if (strcmp(unit, "TB") == 0)
        //     *i = 1024 * 1024 * 1024 * 1024;
    }
}

char *
pgca_double_to_text(double d)
{
    char *str_value;
    str_value = (char *) palloc0(MAXINT8LEN);

    pg_snprintf(str_value, MAXINT8LEN, "%.3lf", d);

    return str_value;
}

// char *
// pgca_int_to_text(int i)
// {
//     char *str_value;
//     str_value = (char *) palloc0(MAXINT8LEN);

//     pg_ltoa(i, str_value);

//     return str_value;
// }

char *
pgca_int_unit_to_text(int i, char *unit)
{
    char *str_value;
    str_value = (char *) palloc0(MAXINT8LEN + PGCA_MAX_UNIT_LEN);

    if (strcmp(unit, "B") == 0)
        ; /* Nothing to do here */
    else if (strcmp(unit, "kB") == 0)
        i = i / 1024;
    else if (strcmp(unit, "MB") == 0)
        i = i / (1024 * 1024);
    else if (strcmp(unit, "GB") == 0)
        i = i / (1024 * 1024 * 1024);
    // else if (strcmp(unit, "TB") == 0)
    //     i = i / (1024 * 1024 * 1024 * 1024);

    pg_snprintf(str_value, MAXINT8LEN + PGCA_MAX_UNIT_LEN, "%d%s", i, unit);

    return str_value;
}

char*
SuggestedSharedBuffer(char *str_value)
{
    int i;
    char units[64];

    pgca_parse_int_units(&i, units, str_value);
    i = sys_res.total_ram * 0.25;

    return pgca_int_unit_to_text(i, units);
}