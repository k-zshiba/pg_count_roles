MODULE_big = pg_count_roles
OBJS = \
	$(WIN32RES) \
	pg_count_roles.o

EXTENSION = pg_count_roles

DATA = pg_count_roles--1.0.sql
PGFILEDESC = 'pg_count_roles - Count the number of roles in database cluster'

TAP_TESTS = 1

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pg_count_roles
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif