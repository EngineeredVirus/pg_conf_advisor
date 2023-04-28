# contrib/pg_conf_advisor/Makefile

MODULE_big = pg_conf_advisor
OBJS = \
    $(WIN32RES) \
    pg_conf_advisor.o

EXTENSION = pg_conf_advisor
DATA = pg_conf_advisor--1.0.sql
PGFILEDESC = "pg_conf_advisor - recomends better GUC configuration for your host"

LDFLAGS_SL += $(filter -lm, $(LIBS))

#REGRESS_OPTS = --temp-config $(top_srcdir)/contrib/pg_conf_advisor/pg_conf_advisor.conf
#REGRESS = pg_conf_advisor oldextversions
# Disabled because these tests require "shared_preload_libraries=pg_conf_advisor",
# which typical installcheck users do not have (e.g. buildfarm clients).

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pg_conf_advisor
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

