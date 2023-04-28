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

typedef struct pgca_cost_data
{
	Cost		seq_page_cost;
	Cost		random_page_cost;
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


PG_FUNCTION_INFO_V1(get_system_info);
PG_FUNCTION_INFO_V1(pg_sysinfo);
PG_FUNCTION_INFO_V1(pg_conf_advisor);
PG_FUNCTION_INFO_V1(pg_conf_advisor_set_params);

static void get_cpu_info(int *p_cpu_count, int *p_core_count);



Datum
pg_conf_advisor(PG_FUNCTION_ARGS)
{
    // Insert your code to read CPU, RAM, and disk space information
    // and retrieve PostgreSQL configuration parameter values here.

    // Return the result as a text value.
    PG_RETURN_TEXT_P(cstring_to_text("Your configuration advice goes here."));
}

Datum
pg_conf_advisor_set_params(PG_FUNCTION_ARGS)
{
    char *params = text_to_cstring(PG_GETARG_TEXT_P(0));
}

Datum
pg_sysinfo(PG_FUNCTION_ARGS)
{
    FuncCallContext *funcctx;
    int call_cntr;
    int max_calls;
    TupleDesc tupdesc;
    AttInMetadata *attinmeta;

    // Initialize function context
    if (SRF_IS_FIRSTCALL())
    {
        MemoryContext oldcontext;

        funcctx = SRF_FIRSTCALL_INIT();
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        // Initialize tuple descriptor
        tupdesc = CreateTemplateTupleDesc(2);
        TupleDescInitEntry(tupdesc, (AttrNumber)1, "parameter", TEXTOID, -1, 0);
        TupleDescInitEntry(tupdesc, (AttrNumber)2, "value", TEXTOID, -1, 0);

        attinmeta = TupleDescGetAttInMetadata(tupdesc);
        funcctx->attinmeta = attinmeta;

        MemoryContextSwitchTo(oldcontext);
    }

    funcctx = SRF_PERCALL_SETUP();
    call_cntr = funcctx->call_cntr;
    max_calls = 4;
    attinmeta = funcctx->attinmeta;

    // Process and return system information
    if (call_cntr < max_calls)
    {
        struct sysinfo sys_info;
        struct statvfs stat;

        char *values[2];
        HeapTuple tuple;
        Datum result;

        sysinfo(&sys_info);
        statvfs("/", &stat);

        switch (call_cntr)
        {
            case 0:
                values[0] = "CPU cores";
                values[1] = psprintf("%d", get_nprocs());
                break;
            case 1:
                values[0] = "RAM (MB)";
                values[1] = psprintf("%lu", sys_info.totalram / 1024 / 1024);
                break;
            case 2:
                values[0] = "Disk space (MB)";
                values[1] = psprintf("%lu", (stat.f_blocks * stat.f_frsize) / (1024 * 1024));
                break;
            case 3:
                values[0] = "Free disk space (MB)";
                values[1] = psprintf("%lu", (stat.f_bavail * stat.f_frsize) / (1024 * 1024));
                break;
        }

        tuple = BuildTupleFromCStrings(attinmeta, values);
        result = HeapTupleGetDatum(tuple);

        SRF_RETURN_NEXT(funcctx, result);
    }
    else
    {
        SRF_RETURN_DONE(funcctx);
    }
}

Datum
get_system_info(PG_FUNCTION_ARGS)
{
    struct sysinfo sys_info;
    struct statvfs disk_info;
    char *config_data_path = text_to_cstring(PG_GETARG_TEXT_P(0));
    int cpus;
    int cpu_cores;
    long total_ram;
    long free_ram;
    long total_disk_space;
    long free_disk_space;
    TupleDesc tuple_desc;
    AttInMetadata *attinmeta;
    HeapTuple tuple;
    Datum values[8];
    bool nulls[8] = {false, false, false, false, false};

    elog(INFO, "shared_buffers = %s", GetConfigOptionByName("shared_buffers", NULL, true));

    if (statvfs(config_data_path, &disk_info) == -1)
    {
        ereport(ERROR,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_EXCEPTION),
                 errmsg("failed to read disk information")));
    }

    if (sysinfo(&sys_info) == -1)
    {
        ereport(ERROR,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_EXCEPTION),
                 errmsg("failed to read system information")));
    }

    get_cpu_info(&cpus, &cpu_cores);

    total_ram = sys_info.totalram;
    free_ram = sys_info.freeram;
    total_disk_space = disk_info.f_blocks * disk_info.f_frsize;
    free_disk_space = disk_info.f_bfree * disk_info.f_frsize;

    if (get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
    {
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("function returning record called in context that cannot accept type record")));
    }

    attinmeta = TupleDescGetAttInMetadata(tuple_desc);

    values[0] = Int32GetDatum(cpus);
    values[1] = Int32GetDatum(cpu_cores);
    values[2] = Int64GetDatum(total_ram);
    values[3] = Int64GetDatum(free_ram);
    values[4] = Float4GetDatum((free_ram * 100.0) / total_ram);
    values[5] = Int64GetDatum(total_disk_space);
    values[6] = Int64GetDatum(free_disk_space);
    values[7] = Float4GetDatum((free_disk_space * 100.0) / total_disk_space);

    tuple = heap_form_tuple(tuple_desc, values, nulls);

    PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}

void
get_cpu_info(int *p_cpu_count, int *p_core_count)
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

    *p_cpu_count = cpus;
    *p_core_count = cores;
}