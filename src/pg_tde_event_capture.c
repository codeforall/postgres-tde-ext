/*-------------------------------------------------------------------------
 *
 * pg_tde_event_capture.c
 *      event trigger logic to identify if we are creating the encrypted table or not.
 *
 * IDENTIFICATION
 *    contrib/pg_tde/src/pg_tde_event_trigger.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "funcapi.h"
#include "fmgr.h"
#include "utils/rel.h"
#include "utils/builtins.h"
#include "catalog/pg_class.h"
#include "access/table.h"
#include "catalog/pg_event_trigger.h"
#include "catalog/namespace.h"
#include "commands/event_trigger.h"
#include "common/pg_tde_utils.h"
#include "pg_tde_event_capture.h"

/* Global variable that gets set at ddl start and cleard out at ddl end*/
TdeCreateEvent tdeCurrentCreateEvent = {.relation = NULL};


static void reset_current_tde_create_event(void);

PG_FUNCTION_INFO_V1(pg_tde_ddl_command_start_capture);
PG_FUNCTION_INFO_V1(pg_tde_ddl_command_end_capture);

TdeCreateEvent*
GetCurrentTdeCreateEvent(void)
{
    return &tdeCurrentCreateEvent;
}

/*
 * pg_tde_ddl_command_start_capture is an event trigger function triggered
 * at the start of any DDL command execution.
 *
 * The function specifically focuses on CREATE INDEX and CREATE TABLE statements,
 * aiming to determine if the create table or the table on which an index is being created
 * utilizes the pg_tde access method for encryption.
 * Once it confirms the table's encryption requirement or usage,
 * it updates the table information in the tdeCurrentCreateEvent global variable.
 * This information can be accessed by SMGR or any other component
 * during the execution of this DDL statement.
 */
Datum
pg_tde_ddl_command_start_capture(PG_FUNCTION_ARGS)
{
	EventTriggerData *trigdata;
	Node *parsetree;

	/* Ensure this function is being called as an event trigger */
	if (!CALLED_AS_EVENT_TRIGGER(fcinfo)) /* internal error */
		ereport(ERROR,
            (errmsg("Function can only be fired by event trigger manager")));

	trigdata = (EventTriggerData *)fcinfo->context;
	parsetree = trigdata->parsetree;

	elog(DEBUG2, "EVENT TRIGGER (%s) %s", trigdata->event, nodeToString(parsetree));
    reset_current_tde_create_event();

	if (IsA(parsetree, IndexStmt))
	{
		IndexStmt *stmt = (IndexStmt *)parsetree;
        Oid relationId= RangeVarGetRelid(stmt->relation, NoLock, true);

        tdeCurrentCreateEvent.eventType = TDE_INDEX_CREATE_EVENT;
        tdeCurrentCreateEvent.baseTableOid = relationId;
        tdeCurrentCreateEvent.relation = stmt->relation;

        if (relationId != InvalidOid)
        {
            LOCKMODE	lockmode = AccessShareLock; /* TODO. Verify lock mode? */
            Relation rel = table_open(relationId, lockmode);
            if (rel->rd_rel->relam == get_tde_table_am_oid())
            {
                /* We are creating the index on encrypted table */
                ereport(NOTICE,
                    (errmsg("Creating index on **ENCRYPTED** relation:%s with Oid:%d",stmt->relation->relname,relationId)));
                /* set the global state */
                tdeCurrentCreateEvent.encryptMode = true;
            }
            else
                ereport(DEBUG1,
                    (errmsg("Creating index on relation:%s with Oid:%d",stmt->relation->relname,relationId)));
            table_close(rel, lockmode);
        }
        else
            ereport(DEBUG1,(errmsg("Failed to get relation Oid for relation:%s",stmt->relation->relname)));

	}
	else if (IsA(parsetree, CreateStmt))
	{
		CreateStmt *stmt = (CreateStmt *)parsetree;

        tdeCurrentCreateEvent.eventType = TDE_TABLE_CREATE_EVENT;
        tdeCurrentCreateEvent.relation = stmt->relation;

		elog(DEBUG1, "CREATING TABLE %s Using Access Method %s", stmt->relation->relname, stmt->accessMethod);
		if (stmt->accessMethod && !strcmp(stmt->accessMethod,"pg_tde"))
		{
            tdeCurrentCreateEvent.encryptMode = true;
		}
	}
    PG_RETURN_NULL();
}

/*
 * trigger function called at the end of DDL statement execution.
 * It just clears the tdeCurrentCreateEvent global variable.
 */
Datum
pg_tde_ddl_command_end_capture(PG_FUNCTION_ARGS)
{
	/* Ensure this function is being called as an event trigger */
	if (!CALLED_AS_EVENT_TRIGGER(fcinfo)) /* internal error */
		ereport(ERROR,
            (errmsg("Function can only be fired by event trigger manager")));

    elog(DEBUG1,"Type:%s EncryptMode:%s, Oid:%d, Relation:%s ",
                (tdeCurrentCreateEvent.eventType == TDE_INDEX_CREATE_EVENT) ?"CREATE INDEX":
                    (tdeCurrentCreateEvent.eventType == TDE_TABLE_CREATE_EVENT) ?"CREATE TABLE":"UNKNOWN",
                tdeCurrentCreateEvent.encryptMode ?"true":"false",
                tdeCurrentCreateEvent.baseTableOid,
                tdeCurrentCreateEvent.relation?tdeCurrentCreateEvent.relation->relname:"UNKNOWN");

    /* All we need to do is to clear the event state */
    reset_current_tde_create_event();
    PG_RETURN_NULL();
}

static void
reset_current_tde_create_event(void)
{
    tdeCurrentCreateEvent.encryptMode = false;
    tdeCurrentCreateEvent.eventType = TDE_UNKNOWN_CREATE_EVENT;
    tdeCurrentCreateEvent.baseTableOid = InvalidOid;
    tdeCurrentCreateEvent.relation = NULL;
}