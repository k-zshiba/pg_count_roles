use strict;
use warnings FATAL => 'all';
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;


my $node = PostgreSQL::Test::Cluster->new('mynode');
$node->init;
$node->start;

note "testing dynamic bgworkder";

$node->safe_psql('postgres', 'CREATE EXTENSION pg_count_roles;');
my $result = $node->safe_psql('postgres', 'SELECT pg_count_roles_launch() IS NOT NULL;');
is($result, 't', "dynamic bgworker launched");

# Check the wait event used by the dynamic bgworker.
$result = $node->safe_psql('postgres', q[SELECT state FROM pg_stat_activity WHERE wait_event = 'PgCountRolesMain';]);
is($result, 'idle', 'dynamic bgworker has reported "PgCountRolesMain" as wait event');

# Check the wait event used by the dynamic bgworker appears in pg_wait_events
$result = $node->safe_psql('postgres',
    q[SELECT count(*) > 0 FROM pg_wait_events WHERE type = 'Extension' AND name = 'PgCountRolesMain';]);
is($result, 't', '"PgCountRolesMain" is reported in pg_wait_events');

$node->append_conf(
    'postgresql.conf', q{
pg_count_roles.database = 'dummydb'
});
$node->restart;
my $log_offset = -s $node->logfile;
$node->safe_psql('postgres', 'SELECT pg_count_roles_launch();');
$node->wait_for_log(qr/database "dummydb" does not exist/,
                    $log_offset);

note "testing bgworkers loaded with shared_preload_libraries";

$node->safe_psql('postgres', q(CREATE DATABASE mydb;));
$node->append_conf(
    'postgresql.conf', q{
shared_preload_libraries = 'pg_count_roles'
pg_count_roles.database = 'mydb'
pg_count_roles.check_duration = 5
});
$node->restart;

$result = $node->safe_psql('mydb', q[SELECT state FROM pg_stat_activity WHERE wait_event = 'PgCountRolesMain';]);
is($result, 'idle', 'start pg_count_roles after the system is up');


done_testing();