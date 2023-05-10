CREATE FUNCTION pg_conf_advisor(
    OUT name TEXT,
    OUT current TEXT,
    OUT suggested TEXT)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_conf_advisor'
LANGUAGE C STRICT;

CREATE FUNCTION pgca_get_system_info(
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
AS 'MODULE_PATHNAME', 'pgca_get_system_info'
LANGUAGE C STRICT;

CREATE VIEW pg_conf_advisor
AS
    SELECT  *
    FROM    pg_conf_advisor();

CREATE VIEW pg_conf_advisor_sys_info
AS
    SELECT  cpus
            , cpu_cores
            , pg_size_pretty(total_ram)                 AS total_ram
            , pg_size_pretty(free_ram)                  AS free_ram
            , percentage_free_ram::NUMERIC(6,3)         AS percentage_free_ram
            , pg_size_pretty(total_disk_space)          AS total_disk_space
            , pg_size_pretty(free_disk_space)           AS free_disk_space
            , percentage_free_disk_space::NUMERIC(6,3)  AS percentage_free_disk_space
    FROM    pgca_get_system_info('.');
