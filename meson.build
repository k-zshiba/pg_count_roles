# Copyright (c) 2022-2024, PostgreSQL Global Development Group

pg_count_roles_sources = files(
  'pg_count_roles.c',
)

if host_system == 'windows'
  test_pg_count_roles_sources += rc_lib_gen.process(win32ver_rc, extra_args: [
    '--NAME', 'pg_count_roles',
    '--FILEDESC', 'pg_count_roles - background worker example',])
endif

pg_count_roles = shared_module('pg_count_roles',
  pg_count_roles_sources,
  kwargs: contrib_mod_args,
)
contrib_targets += pg_count_roles

install_data(
  'pg_count_roles--1.0.sql',
  'pg_count_roles.control',
  kwargs: contrib_data_args,
)

tests += {
  'name': 'pg_count_roles',
  'sd': meson.current_source_dir(),
  'bd': meson.current_build_dir(),
  'tap': {
    'tests': [
      't/001_pg_count_roles.pl',
    ],
  },
}
