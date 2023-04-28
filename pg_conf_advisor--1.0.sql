CREATE FUNCTION pg_conf_advisor(
    OUT parameter TEXT,
    OUT param_value TEXT)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_conf_advisor'
LANGUAGE C STRICT;

CREATE FUNCTION pg_sysinfo(
    OUT parameter TEXT,
    OUT param_value TEXT)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_sysinfo'
LANGUAGE C STRICT;

CREATE FUNCTION get_system_info(
    IN data_path TEXT,
    OUT cpus INT4,
    OUT cpu_cores INT4,
    OUT total_ram INT8,
    OUT free_ram INT8,
    OUT percentage_free_ram FLOAT4,
    OUT total_disk_space INT8,
    OUT free_disk_space INT8,
    OUT percentage_free_disk_space FLOAT4)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'get_system_info'
LANGUAGE C STRICT;
