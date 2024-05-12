use strict;
use warnings FATAL => 'all';
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use Time::Piece;


my $node = PostgreSQL::Test::Cluster->new('mynode');
$node->init;
$node->start;

note "testing dynamic bgworkder";

$node->safe_psql('postgres', 'CREATE EXTENSION pg_count_roles;');
my $pid = $node->safe_psql('postgres', 'SELECT pg_count_roles_launch();');
ok($pid > 0, "dynamic bgworker launched");

# Check the wait event used by the dynamic bgworker.
ok($node->poll_query_until(
    'postgres', 
    q[SELECT count(*) > 0 FROM pg_stat_activity 
        WHERE wait_event = 'PgCountRolesMain' AND state = 'idle';]),
    'dynamic bgworker has reported "PgCountRolesMain" as wait event');

# Check the "query" column of pg_stat_activity
ok($node->poll_query_until(
    'postgres', 
    q[SELECT count(*) > 0 FROM pg_stat_activity 
        WHERE wait_event = 'PgCountRolesMain' AND query = 'SELECT count(*) FROM pg_roles;';]),
    'pg_count_roles_main appears in query column of pg_stat_activity');

# Check the wait event used by the dynamic bgworker appears in pg_wait_events
ok($node->poll_query_until(
    'postgres',
    q[SELECT count(*) > 0 FROM pg_wait_events 
        WHERE type = 'Extension' AND name = 'PgCountRolesMain';]),
    '"PgCountRolesMain" is reported in pg_wait_events');

# reload check_duration
$node->append_conf(
    'postgresql.conf', q{
pg_count_roles.check_duration = 5
});
$node->reload;
$node->wait_for_log(qr/roles in database cluster/);
my $log_offset = -s $node->logfile;
$node->wait_for_log(qr/roles in database cluster/,$log_offset);
my $start = Time::Piece->strptime(substr(slurp_file($node->logfile, $log_offset),11,12),'%T.%N');
$log_offset = -s $node->logfile;
$node->wait_for_log(qr/roles in database cluster/,$log_offset);
my $end = Time::Piece->strptime(substr(slurp_file($node->logfile, $log_offset),11,12),'%T.%N');
my $duration = $end - $start;
ok($duration >= 5,'Test whether the database is accessed at the interval set in pg_count_roles.check_duration');

$node->safe_psql('postgres',qq[SELECT pg_terminate_backend($pid);]);
my $result = $node->safe_psql('postgres', 
        qq[SELECT * FROM pg_stat_activity 
            WHERE wait_event = 'PgCountRolesMain' AND pid = $pid]);
is($result, '', 'stop worker by sending SIGTERM');

note "testing bgworkers loaded with shared_preload_libraries";

$node->safe_psql('postgres', q(CREATE DATABASE mydb;));
$node->append_conf(
    'postgresql.conf', q{
shared_preload_libraries = 'pg_count_roles'
pg_count_roles.database = 'mydb'
});
$node->restart;

ok($node->poll_query_until(
    'mydb', 
    q[SELECT count(*) > 0 FROM pg_stat_activity 
        WHERE wait_event = 'PgCountRolesMain';]),
    'connect to the database specified in pg_count_roles.database');

done_testing();