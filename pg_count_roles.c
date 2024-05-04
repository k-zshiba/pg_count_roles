/*-------------------------------------------------------------------------
 *
 * pg_count_roles.c
 *		Simple background worker code scanning the number of roles
 *		present in database cluster.
 *
 * Copyright (c) 1996-2024, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pg_count_roles/pg_count_roles.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

/* These are always necessary for a bgworker */
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "postmaster/interrupt.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

/* these headers are used by this particular worker's code */
#include "access/xact.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/snapmgr.h"
#include "utils/wait_event.h"
#include "pgstat.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pg_count_roles_launch);

PGDLLEXPORT void pg_count_roles_main(Datum main_arg) pg_attribute_noreturn();

/* GUC variables */
static int pg_count_roles_check_duration = 10;
static char *pg_count_roles_database = NULL;

static volatile sig_atomic_t got_sigterm = false;
static volatile sig_atomic_t got_sighup = false;

static void
pg_count_roles_sigterm(SIGNAL_ARGS)
{
    int save_errno = errno;

    got_sigterm = true;
    if (MyProc)
        SetLatch(&MyProc->procLatch);
    errno = save_errno;
}

static void
pg_count_roles_sighup(SIGNAL_ARGS)
{
    got_sighup = true;
    if (MyProc)
        SetLatch(&MyProc->procLatch);
}

void
pg_count_roles_main(Datum main_arg)
{
    StringInfoData buf;
    /* Register functions for SIGTERM/SIGHUP management */
    pqsignal(SIGHUP, pg_count_roles_sighup);
    pqsignal(SIGTERM, pg_count_roles_sigterm);

    /* We're now ready to receive signals */
    BackgroundWorkerUnblockSignals();

    /* Connect to our database */
    BackgroundWorkerInitializeConnection(pg_count_roles_database, NULL, 0);
    initStringInfo(&buf);

    /* Build the query string */
    appendStringInfo(&buf,"SELECT count(*) FROM pg_roles;");
    while (!got_sigterm)
    {
        int ret;
        static uint32 wait_event_info = 0;

        if (wait_event_info == 0)
            wait_event_info  = WaitEventExtensionNew("PgCountRolesMain");
        

        WaitLatch(&MyProc->procLatch,
                        WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
                        pg_count_roles_check_duration * 1000L,
                        wait_event_info);

        ResetLatch(&MyProc->procLatch);

        StartTransactionCommand();
        SPI_connect();
        PushActiveSnapshot(GetTransactionSnapshot());
        pgstat_report_activity(STATE_RUNNING, buf.data);      

        ret = SPI_execute(buf.data, true, 0);

        /* Some error message in case of incorrect handling */
        if (ret != SPI_OK_SELECT)
            elog(FATAL, "SPI_execute failed: error code %d", ret);                                                                                                                                          

        if (SPI_processed > 0)
        {                                                                   
            int32 count;
            bool isnull;

            count = DatumGetInt32(SPI_getbinval(SPI_tuptable->vals[0],
                                                SPI_tuptable->tupdesc,
                                                1, &isnull));
            elog(LOG, "Currently %d roles in database cluster", count);
        }

        SPI_finish();
        PopActiveSnapshot();
        CommitTransactionCommand();
        pgstat_report_stat(true);
        pgstat_report_activity(STATE_IDLE, NULL);
    }
    proc_exit(0);
}

/*
 * Entrypoint of this module.
 */
void
_PG_init(void)
{
    BackgroundWorker worker;

	/*
	 * These GUCs are defined even if this library is not loaded with
	 * shared_preload_libraries, for pg_count_roles_launch().
	 */

    DefineCustomIntVariable("pg_count_roles.check_duration",
                            "Duration between each check (in seconds).",
                            NULL,
                            &pg_count_roles_check_duration,
                            10,
                            1,
                            INT_MAX,
                            PGC_SIGHUP,
                            0,
                            NULL,
                            NULL,
                            NULL);

    DefineCustomStringVariable("pg_count_roles.database",
                                "Database to connect to.",
                                NULL,
                                &pg_count_roles_database,
                                "postgres",
                                PGC_SIGHUP,
                                0,
                                NULL,NULL,NULL);

    if(!process_shared_preload_libraries_in_progress)
        return;
    
    MarkGUCPrefixReserved("pg_count_roles");

    /* register the worker processes */
    memset(&worker, 0, sizeof(worker));
    worker.bgw_flags = BGWORKER_SHMEM_ACCESS |
            BGWORKER_BACKEND_DATABASE_CONNECTION;
    worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
    snprintf(worker.bgw_library_name, BGW_MAXLEN, "pg_count_roles");
    snprintf(worker.bgw_function_name, BGW_MAXLEN,  "pg_count_roles_main");
    snprintf(worker.bgw_name, BGW_MAXLEN, "count roles");
    worker.bgw_restart_time = BGW_NEVER_RESTART;
    worker.bgw_main_arg = (Datum) 0;
    worker.bgw_notify_pid = 0;
    RegisterBackgroundWorker(&worker);
}

/*
 * Dynamically launch an SPI worker
 */
Datum
pg_count_roles_launch(PG_FUNCTION_ARGS)
{
    BackgroundWorker worker;
    BackgroundWorkerHandle *handle;
    BgwHandleStatus status;
    pid_t pid;

    memset(&worker, 0, sizeof(worker));
    worker.bgw_flags = BGWORKER_SHMEM_ACCESS |
            BGWORKER_BACKEND_DATABASE_CONNECTION;
    worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
    snprintf(worker.bgw_library_name, BGW_MAXLEN, "pg_count_roles");
    snprintf(worker.bgw_function_name, BGW_MAXLEN,  "pg_count_roles_main");
    snprintf(worker.bgw_name, BGW_MAXLEN, "count roles");
    worker.bgw_restart_time = BGW_NEVER_RESTART;
    worker.bgw_main_arg = (Datum) 0;
    worker.bgw_notify_pid = MyProcPid;

    if (!RegisterDynamicBackgroundWorker(&worker, &handle))
        PG_RETURN_NULL();
    
    status = WaitForBackgroundWorkerStartup(handle, &pid);

    if (status == BGWH_STOPPED)
        ereport(ERROR, 
                (errcode(ERRCODE_INSUFFICIENT_RESOURCES),
                 errmsg("could not start background process"),
                 errhint("More details may be available in the server log.")));
    if (status == BGWH_POSTMASTER_DIED)
        ereport(ERROR,
                (errcode(ERRCODE_INSUFFICIENT_RESOURCES),
                 errmsg("cannot start background processes without postmaster"),
                 errhint("Kill all remaining database processes and restart the database.")));
    Assert(status == BGWH_STARTED);

    PG_RETURN_INT32(pid);
}