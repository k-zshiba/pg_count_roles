use strict;
use warnings FATAL => 'all';
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;


my $node = PostgreSQL::Test::Cluster->new('mydb');
$node->init;
$node->start;
$node->safe_psql('postgres', 'CREATE EXTENSION pg_count_roles;');
$node->safe_psql('postgres', 'SELECT pg_count_roles_launch();');

my $ret = $node->safe_psql('postgres', q[SELECT state FROM pg_stat_activity WHERE wait_event = 'pg_count_roles_main';]);
is($ret, 'active', 'dynamically launch pg_count_roles');

$node->stop('fast');

$node->append_conf('postgresql.conf', q{shared_preload_libraries = 'pg_count_roles'});
$node->start;
$ret = $node->safe_psql('postgres', q[SELECT state FROM pg_stat_activity WHERE wait_event = 'pg_count_roles_main';]);
is($ret, 'active', 'start pg_count_roles after the system is up');

done_testing();