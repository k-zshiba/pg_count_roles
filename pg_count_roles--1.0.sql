/* contrib/pg_count_roles/pg_count_roles--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_count_roles" to load this file. \quit

CREATE FUNCTION pg_count_roles_launch()
RETURNS pg_catalog.int4 STRICT
AS 'MODULE_PATHNAME'
LANGUAGE C;