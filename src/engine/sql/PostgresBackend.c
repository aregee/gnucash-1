/* 
 * FILE:
 * PostgressBackend.c
 *
 * FUNCTION:
 * Implements the callbacks for the Postgres backend.
 * The SINGLE modes mostly work and are mostly feature complete.
 * The multi-user modes are mostly in disrepair, and marginally
 * functional.
 *
 * HISTORY:
 * Copyright (c) 2000, 2001 Linas Vepstas
 * 
 */

#define _GNU_SOURCE
#include <glib.h>
#include <pwd.h>
#include <stdio.h>  
#include <string.h>  
#include <sys/types.h>  
#include <unistd.h>  

#include <pgsql/libpq-fe.h>  

#include "AccountP.h"
#include "Backend.h"
#include "BackendP.h"
#include "Group.h"
#include "gnc-book.h"
#include "gnc-commodity.h"
#include "gnc-engine.h"
#include "gnc-engine-util.h"
#include "gnc-event.h"
#include "guid.h"
#include "GNCId.h"
#include "GNCIdP.h"
#include "TransactionP.h"

#include "builder.h"
#include "gncquery.h"
#include "kvp-sql.h"
#include "PostgresBackend.h"

#include "putil.h"

static short module = MOD_BACKEND; 

static void pgendDisable (PGBackend *be);
static void pgendEnable (PGBackend *be);
static void pgendInit (PGBackend *be);

static const char * pgendSessionGetMode (PGBackend *be);

GUID nullguid;

/* hack alert -- this is the query buffer size, it can be overflowed.
 * Ideally, its dynamically resized.  On the other hand, Postgres
 * rejects queries longer than 8192 bytes,(according to the
 * documentation) so theres not much point in getting fancy ... 
 */
#define QBUFSIZE 16350

/* ============================================================= */
/* misc bogus utility routines */

static char *
pgendGetHostname (PGBackend *be)
{
   char * p;

   p = be->buff;
   *p = 0;
   if (0 == gethostname (p, QBUFSIZE/3)) 
   {
      p += strlen (p);
      p = stpcpy (p, ".");
   }
   getdomainname (p, QBUFSIZE/3);
   return be->buff;
}

static char *
pgendGetUsername (PGBackend *be)
{
   uid_t uid = getuid();
   struct passwd *pw = getpwuid (uid);
   if (pw) return (pw->pw_name);
   return NULL;
}

static char *
pgendGetUserGecos (PGBackend *be)
{
   uid_t uid = getuid();
   struct passwd *pw = getpwuid (uid);
   if (pw) return (pw->pw_gecos);
   return NULL;
}

/* ============================================================= */
/* This routine finds the commodity by parsing a string 
 * of the form NAMESPACE::MNEMONIC 
 */

static const gnc_commodity *
gnc_string_to_commodity (const char *str)
{
   /* hop through a couple of hoops for the commodity */
   /* it would be nice to simplify this ... */
   gnc_commodity_table *comtab = gnc_engine_commodities();
   gnc_commodity *com;
   char *space, *name;

   space = g_strdup(str);
   name = strchr (space, ':');
   *name = 0;
   name += 2;

   com = gnc_commodity_table_lookup(comtab, space, name);
   g_free (space);
   return com;
}

/* ============================================================= */
/* send the query, process the results */

gpointer
pgendGetResults (PGBackend *be, 
             gpointer (*handler) (PGBackend *, PGresult *, int, gpointer),
             gpointer data)
{   
   PGresult *result;
   int i=0;

   be->nrows=0;
   do {
      GET_RESULTS (be->connection, result);
      {
         int j, jrows;
         int ncols = PQnfields (result);
         jrows = PQntuples (result);
         be->nrows += jrows;
         PINFO ("query result %d has %d rows and %d cols",
            i, jrows, ncols);

         for (j=0; j<jrows; j++)
         {
            data = handler (be, result, j, data);
         }
      }
      i++;
      PQclear (result);
   } while (result);

   return data;
}


/* ============================================================= */
/* include the auto-generated code */

#include "base-autogen.c"

static const char *table_create_str = 
#include "table-create.c"
;

static const char *table_drop_str = 
#include "table-drop.c"
;

/* ============================================================= */
/* This routine updates the account structure if needed, and/or
 * stores it the first time if it hasn't yet been stored.
 * Note that it sets a mark to avoid excessive recursion:
 * This routine shouldn't be used outside of locks,
 * where the recursion prevention clears the marks ...
 */

static void
pgendStoreAccountNoLock (PGBackend *be, Account *acct,
                         gboolean do_mark)
{
   const gnc_commodity *com;

   if (!be || !acct) return;
   if ((FALSE == do_mark) && (FALSE == acct->core_dirty)) return;

   ENTER ("acct=%p, mark=%d", acct, do_mark);

   if (do_mark) 
   { 
      /* Check to see if we've processed this account recently.
       * If so, then return.  The goal here is to avoid excess
       * hits to the database, leading to poor performance.
       * Note that this marking makes this routine unsafe to use 
       * outside a lock (since we never clear the mark)
       */
      if (xaccAccountGetMark (acct)) return;
      xaccAccountSetMark (acct, 1);
   }

   pgendPutOneAccountOnly (be, acct);

   /* make sure the account's commodity is in the commodity table */
   /* hack alert -- it would be more efficient to do this elsewhere,
    * and not here.  Or put a mark on it ... */
   com = xaccAccountGetCommodity (acct);
   pgendPutOneCommodityOnly (be, (gnc_commodity *) com);

   pgendKVPStore (be, &(acct->guid), acct->kvp_data);
   LEAVE(" ");
}

/* ============================================================= */
/* This routine traverses the transaction structure and stores/updates
 * it in the database.  If checks the transaction splits as well,
 * updating those.  Finally, it makes sure that each account is present
 * as well. 
 */

static gpointer
delete_list_cb (PGBackend *be, PGresult *result, int j, gpointer data)
{
   GList * deletelist = (GList *) data;
   GUID guid = nullguid;

   string_to_guid (DB_GET_VAL ("entryGuid", j), &guid);
   /* If the database has splits that the engine doesn't,
    * collect 'em up & we'll have to delete em */
   if (NULL == xaccLookupEntity (&guid, GNC_ID_SPLIT))
   {
      deletelist = g_list_prepend (deletelist, 
                   g_strdup(DB_GET_VAL ("entryGuid", j)));
   }
   return deletelist;
}

static void
pgendStoreTransactionNoLock (PGBackend *be, Transaction *trans, 
                             gboolean do_mark)
{
   GList *start, *deletelist=NULL, *node;
   char * p;

   if (!be || !trans) return;
   ENTER ("trans=%p, mark=%d", trans, do_mark);


   /* first, we need to see which splits are in the database
    * since what is there may not match what we have cached in 
    * the engine. */
   p = be->buff; *p = 0;
   p = stpcpy (p, "SELECT entryGuid FROM gncEntry WHERE transGuid='");
   p = guid_to_string_buff(xaccTransGetGUID(trans), p);
   p = stpcpy (p, "';");

   SEND_QUERY (be,be->buff, );
   deletelist = pgendGetResults (be, delete_list_cb, deletelist);

   /* delete those splits that don't belong */
   p = be->buff; *p = 0;
   for (node=deletelist; node; node=node->next)
   {
      p = stpcpy (p, "DELETE FROM gncEntry WHERE entryGuid='");
      p = stpcpy (p, node->data);
      p = stpcpy (p, "';\n");
   }
   if (p != be->buff)
   {
      PINFO ("%s", be->buff);
      SEND_QUERY (be,be->buff, );
      FINISH_QUERY(be->connection);

      /* destroy any associated kvp data as well */
      for (node=deletelist; node; node=node->next)
      {
         pgendKVPDeleteStr (be, (char *)(node->data));
         g_free (node->data);
      }
   }

   /* Update the rest */
   start = xaccTransGetSplitList(trans);

   if ((start) && !(trans->do_free))
   { 
      for (node=start; node; node=node->next) 
      {
         Split * s = node->data;
         pgendPutOneSplitOnly (be, s);
         pgendKVPStore (be, &(s->guid), s->kvp_data);
      }
      pgendPutOneTransactionOnly (be, trans);
      pgendKVPStore (be, &(trans->guid), trans->kvp_data);
   }
   else
   {
      p = be->buff; *p = 0;
      for (node=start; node; node=node->next) 
      {
         Split * s = node->data;
         p = stpcpy (p, "DELETE FROM gncEntry WHERE entryGuid='");
         p = guid_to_string_buff (xaccSplitGetGUID(s), p);
         p = stpcpy (p, "';\n");
      }
      p = be->buff; 
      p = stpcpy (p, "DELETE FROM gncTransaction WHERE transGuid='");
      p = guid_to_string_buff (xaccTransGetGUID(trans), p);
      p = stpcpy (p, "';");
      PINFO ("%s\n", be->buff);
      SEND_QUERY (be,be->buff, );
      FINISH_QUERY(be->connection);

      /* destroy any associated kvp data as well */
      for (node=start; node; node=node->next) 
      {
         Split * s = node->data;
         pgendKVPDelete (be, &(s->guid));
      }
      pgendKVPDelete (be, &(trans->guid));
   }

   LEAVE(" ");
}

static void
pgendStoreTransaction (PGBackend *be, Transaction *trans)
{
   char * bufp;
   if (!be || !trans) return;
   ENTER ("be=%p, trans=%p", be, trans);

   /* lock it up so that we store atomically */
   bufp = "BEGIN;"
          "LOCK TABLE gncTransaction IN EXCLUSIVE MODE; "
          "LOCK TABLE gncEntry IN EXCLUSIVE MODE; "
          "LOCK TABLE gncAccount IN EXCLUSIVE MODE; "
          "LOCK TABLE gncCommodity IN EXCLUSIVE MODE; ";
   SEND_QUERY (be,bufp, );
   FINISH_QUERY(be->connection);

   pgendStoreTransactionNoLock (be, trans, FALSE);

   bufp = "COMMIT;";
   SEND_QUERY (be,bufp, );
   FINISH_QUERY(be->connection);
   LEAVE(" ");
}

/* ============================================================= */
/* This routine traverses the group structure and stores it into 
 * the database.  The NoLock version doesn't lock up the tables.
 */

static int
traverse_cb (Transaction *trans, void *cb_data)
{
   /* the callback is only called when marking... */
   pgendStoreTransactionNoLock ((PGBackend *) cb_data, trans, TRUE);
   return 0;
}

static void
pgendStoreGroupNoLock (PGBackend *be, AccountGroup *grp, 
                       gboolean do_mark)
{
   GList *start, *node;

   if (!be || !grp) return;
   ENTER("grp=%p mark=%d", grp, do_mark);

   /* walk the account tree, and store subaccounts */
   start = xaccGroupGetAccountList (grp);
   for (node=start; node; node=node->next) 
   {
      AccountGroup *subgrp;
      Account *acc = node->data;

      pgendStoreAccountNoLock (be, acc, do_mark);

      /* recursively walk to child accounts */
      subgrp = xaccAccountGetChildren (acc);
      if (subgrp) pgendStoreGroupNoLock(be, subgrp, do_mark);
   }
   LEAVE(" ");
}


static void
pgendStoreGroup (PGBackend *be, AccountGroup *grp)
{
   char *bufp;
   ENTER ("be=%p, grp=%p", be, grp);
   if (!be || !grp) return;

   /* lock it up so that we store atomically */
/* hack alert ---- we need to lock a bunch of tables, right??!! */
   bufp = "BEGIN;";
   SEND_QUERY (be,bufp, );
   FINISH_QUERY(be->connection);

   /* Clear the account marks; useful later to avoid recurision
    * during account consistency checks. */
   xaccClearMarkDownGr (grp, 0);

   /* reset the write flags. We use this to make sure we don't
    * get caught in infinite recursion */
   xaccGroupBeginStagedTransactionTraversals(grp);
   pgendStoreGroupNoLock (be, grp, TRUE);

   /* recursively walk transactions */
   xaccGroupStagedTransactionTraversal (grp, 1, traverse_cb, be);

   /* reset the write flags again */
   xaccClearMarkDownGr (grp, 0);

   bufp = "COMMIT;";
   SEND_QUERY (be,bufp, );
   FINISH_QUERY(be->connection);
   LEAVE(" ");
}

/* ============================================================= */

static void
pgendSync (Backend *bend, AccountGroup *grp)
{
   PGBackend *be = (PGBackend *)bend;
   ENTER ("be=%p, grp=%p", be, grp);

   /* hack alert -- this is *not* the correct implementation
    * of what the synchronize function is supposed to do. 
    * This is a sick placeholder.
    */
   pgendStoreGroup (be, grp);

   if ((MODE_SINGLE_FILE != be->session_mode) &&
       (MODE_SINGLE_UPDATE != be->session_mode))
   {
      /* Maybe this should be part of store group ?? */
      pgendGroupRecomputeAllCheckpoints (be, grp);
   }

   LEAVE(" ");
}

/* ============================================================= */

static void
pgendSyncSingleFile (Backend *bend, AccountGroup *grp)
{
   char *p;
   PGBackend *be = (PGBackend *)bend;
   ENTER ("be=%p, grp=%p", be, grp);

   /* In single file mode, we treat 'sync' as 'file save'.
    * We start by deleting *everything*, and then writing 
    * everything out.  This is rather nasty, ugly and dangerous,
    * but that's the nature of single-file mode.  Note: we
    * have to delete everything because there is no other way 
    * of finding out that an account, transaction or split
    * was deleted. i.e. there's no other way to delete.  So
    * start with a clean slate.
    */
    
   p = "DELETE FROM gncEntry; "
       "DELETE FROM gncTransaction; "
       "DELETE FROM gncAccount; "
       "DELETE FROM gncCommodity; ";
   SEND_QUERY (be,p, );
   FINISH_QUERY(be->connection);

   pgendStoreGroup (be, grp);

   LEAVE(" ");
}

/* ============================================================= */
/* 
 * The pgendCopyTransactionToEngine() routine 'copies' data out of 
 *    the SQL database and into the engine, for the indicated 
 *    Transaction GUID.  It starts by looking for an existing
 *    transaction in the engine with such a GUID.  If found, then
 *    it compares the date of last update to what's in the sql DB.
 *    If the engine data is older, or the engine doesn't yet have 
 *    this transaction, then the full update happens.  The full
 *    update sets up the stransaction structure, all of the splits
 *    in the transaction, and makes sure that all of the splits 
 *    are in the proper accounts.  If the pre-existing tranasaction
 *    in the engine has more splits than what's in the DB, then these
 *    are pruned so that the structure exactly matches what's in the 
 *    DB.  This routine then returns FALSE.
 *
 *    If this routine finds a pre-existing transaction in the engine,
 *    and the date of last modification of this transaction is 
 *    *newer* then what the DB holds, then this routine returns
 *    TRUE, and does *not* perform any update.
 */

static gboolean
pgendCopyTransactionToEngine (PGBackend *be, GUID *trans_guid)
{
   const gnc_commodity *modity=NULL;
   char *pbuff;
   Transaction *trans;
   PGresult *result;
   Account *acc, *previous_acc=NULL;
   gboolean do_set_guid=FALSE;
   gboolean engine_data_is_newer = FALSE;
   int i, j, nrows;
   GList *node, *db_splits=NULL, *engine_splits, *delete_splits=NULL;
   
   ENTER ("be=%p", be);
   if (!be || !trans_guid) return FALSE;

   /* disable callbacks into the backend, and events to GUI */
   gnc_engine_suspend_events();
   pgendDisable(be);

   /* first, see if we already have such a transaction */
   trans = (Transaction *) xaccLookupEntity (trans_guid, GNC_ID_TRANS);
   if (!trans)
   {
      trans = xaccMallocTransaction();
      do_set_guid=TRUE;
      engine_data_is_newer = FALSE;
   }

   /* build the sql query to get the transaction */
   pbuff = be->buff;
   pbuff[0] = 0;
   pbuff = stpcpy (pbuff, 
         "SELECT * FROM gncTransaction WHERE transGuid='");
   pbuff = guid_to_string_buff(trans_guid, pbuff);
   pbuff = stpcpy (pbuff, "';");

   SEND_QUERY (be,be->buff, FALSE);
   i=0; nrows=0;
   do {
      GET_RESULTS (be->connection, result);
      {
         int jrows;
         int ncols = PQnfields (result);
         jrows = PQntuples (result);
         nrows += jrows;
         j = 0;
         PINFO ("query result %d has %d rows and %d cols",
            i, nrows, ncols);

         if (1 < nrows)
         {
             /* since the guid is primary key, this error is totally
              * and completely impossible, theoretically ... */
             PERR ("!!!!!!!!!!!SQL database is corrupt!!!!!!!\n"
                   "too many transactions with GUID=%s\n",
                    guid_to_string (trans_guid));
             if (jrows != nrows) xaccTransCommitEdit (trans);
             xaccBackendSetError (&be->be, ERR_SQL_CORRUPT_DB);
             return FALSE;
         }

         /* First order of business is to determine whose data is
          * newer: the engine cache, or the database.  If the 
          * database has newer stuff, we update eh engine. If the
          * engine is newer, we need to poke into the database.
          * Of course, we know the database has newer data if this
          * transaction doesn't exist in the engine yet.
          * Also, make the date comparison so that engine
          * is considered newer only if engine is strictly newer,
          * so that 'equals' doesn't cause a database write.
          */
         if (!do_set_guid)
         {
            Timespec db_ts, cache_ts;
            db_ts = gnc_iso8601_to_timespec_local (DB_GET_VAL("date_entered",j));
            cache_ts = xaccTransRetDateEnteredTS (trans);
            if (0 < timespec_cmp (&db_ts, &cache_ts)) {
               engine_data_is_newer = TRUE;
            } else {
               engine_data_is_newer = FALSE;
            }
         }

         /* if the DB data is newer, copy it to engine */
         if (FALSE == engine_data_is_newer)
         {
            Timespec ts;
            xaccTransBeginEdit (trans);
            if (do_set_guid) xaccTransSetGUID (trans, trans_guid);
            xaccTransSetNum (trans, DB_GET_VAL("num",j));
            xaccTransSetDescription (trans, DB_GET_VAL("description",j));
            ts = gnc_iso8601_to_timespec_local (DB_GET_VAL("date_posted",j));
            xaccTransSetDatePostedTS (trans, &ts);
            ts = gnc_iso8601_to_timespec_local (DB_GET_VAL("date_entered",j));
            xaccTransSetDateEnteredTS (trans, &ts);

            /* hack alert -- don't set the transaction currency until
             * after all splits are restored. This hack is used to set
             * the reporting currency in an account. This hack will be 
             * obsolete when reporting currencies are removed from the
             * account. */
            modity = gnc_string_to_commodity (DB_GET_VAL("currency",j));
#if 0
             xaccTransSetCurrency (trans, 
                    gnc_string_to_commodity (DB_GET_VAL("currency",j)));
#endif
         }
      }
      PQclear (result);
      i++;
   } while (result);

   if (0 == nrows) 
   {
      /* hack alert -- not sure how to handle this case; we'll just 
       * punt for now ... */
      PERR ("no such transaction in the database. This is unexpected ...\n");
      xaccBackendSetError (&be->be, ERR_SQL_MISSING_DATA);
      return FALSE;
   }

   /* if engine data was newer, we are done */
   if (TRUE == engine_data_is_newer) return TRUE;

   /* ------------------------------------------------- */
   /* If we are here, then the sql database contains data that is
    * newer than what we have in the engine.  And so, below, 
    * we finish the job of yanking data out of the db.
    */

   /* build the sql query the splits */
   pbuff = be->buff;
   pbuff[0] = 0;
   pbuff = stpcpy (pbuff, 
         "SELECT * FROM gncEntry WHERE transGuid='");
   pbuff = guid_to_string_buff(trans_guid, pbuff);
   pbuff = stpcpy (pbuff, "';");

   SEND_QUERY (be,be->buff, FALSE);
   i=0; nrows=0;
   do {
      GET_RESULTS (be->connection, result);
      {
         int j, jrows;
         int ncols = PQnfields (result);
         jrows = PQntuples (result);
         nrows += jrows;
         PINFO ("query result %d has %d rows and %d cols",
            i, nrows, ncols);

         for (j=0; j<jrows; j++)
         {
            Split *s;
            GUID guid;
            Timespec ts;
            gint64 num, denom;
            gnc_numeric value, amount;

            /* --------------------------------------------- */
            /* first, lets see if we've already got this one */
            PINFO ("split GUID=%s", DB_GET_VAL("entryGUID",j));
            guid = nullguid;  /* just in case the read fails ... */
            string_to_guid (DB_GET_VAL("entryGUID",j), &guid);
            s = (Split *) xaccLookupEntity (&guid, GNC_ID_SPLIT);
            if (!s)
            {
               s = xaccMallocSplit();
               xaccSplitSetGUID(s, &guid);
            }

            /* next, restore some split data */
            /* hack alert - not all split fields handled */
            xaccSplitSetMemo(s, DB_GET_VAL("memo",j));
            xaccSplitSetAction(s, DB_GET_VAL("action",j));
            ts = gnc_iso8601_to_timespec_local (DB_GET_VAL("date_reconciled",j));
            xaccSplitSetDateReconciledTS (s, &ts);

            num = atoll (DB_GET_VAL("amountNum", j));
            denom = atoll (DB_GET_VAL("amountDenom", j));
            amount = gnc_numeric_create (num, denom);
            xaccSplitSetShareAmount (s, amount);

            num = atoll (DB_GET_VAL("valueNum", j));
            denom = atoll (DB_GET_VAL("valueDenom", j));
            value = gnc_numeric_create (num, denom);
            xaccSplitSetValue (s, value);

            xaccSplitSetReconcile (s, (DB_GET_VAL("reconciled", j))[0]);


            /* --------------------------------------------- */
            /* next, find the account that this split goes into */
            guid = nullguid;  /* just in case the read fails ... */
            string_to_guid (DB_GET_VAL("accountGUID",j), &guid);
            acc = (Account *) xaccLookupEntity (&guid, GNC_ID_ACCOUNT);
            if (!acc)
            {
               PERR ("account not found, will delete this split\n"
                     "\t(split with  guid=%s\n" 
                     "\twants an acct with guid=%s)\n", 
                     DB_GET_VAL("entryGUID",j),
                     DB_GET_VAL("accountGUID",j)
                     );
               xaccSplitDestroy (s);
            }
            else
            {
               xaccTransAppendSplit (trans, s);

               if (acc != previous_acc)
               {
                  xaccAccountCommitEdit (previous_acc);
                  xaccAccountBeginEdit (acc);
                  previous_acc = acc;
               }
               xaccAccountInsertSplit(acc, s);

               /* finally tally them up; we use this below to 
                * clean out deleted splits */
               db_splits = g_list_prepend (db_splits, s);
            }
         }
      }
      i++;
      PQclear (result);
   } while (result);

   /* close out dangling edit session */
   xaccAccountCommitEdit (previous_acc);

   /* ------------------------------------------------- */
   /* destroy any splits that the engine has that the DB didn't */

   i=0; j=0;
   engine_splits = xaccTransGetSplitList(trans);
   for (node = engine_splits; node; node=node->next)
   {
      /* if not found, mark for deletion */
      if (NULL == g_list_find (db_splits, node->data))
      {
         delete_splits = g_list_prepend (delete_splits, node->data);
         j++;
      }
      i++;
   }
   PINFO ("%d of %d splits marked for deletion", j, i);

   /* now, delete them ... */
   for (node=delete_splits; node; node=node->next)
   {
      xaccSplitDestroy ((Split *) node->data);
   }
   g_list_free (delete_splits);
   g_list_free (db_splits);

   /* ------------------------------------------------- */
   /* restore any kvp data associated with the transaction and splits */

   trans->kvp_data = pgendKVPFetch (be, &(trans->guid), trans->kvp_data);

   engine_splits = xaccTransGetSplitList(trans);
   for (node = engine_splits; node; node=node->next)
   {
      Split *s = node->data;
      s->kvp_data = pgendKVPFetch (be, &(s->guid), s->kvp_data);
   }

   /* ------------------------------------------------- */

   /* see note above as to why we do this set here ... */
   xaccTransSetCurrency (trans, modity);

   xaccTransCommitEdit (trans);

   /* re-enable events to the backend and GUI */
   pgendEnable(be);
   gnc_engine_resume_events();

   LEAVE (" ");
   return FALSE;
}

/* ============================================================= */
/* This routine 'synchronizes' the Transaction structure 
 * associated with the GUID.  Data is pulled out of the database,
 * the versions are compared, and updates made, if needed.
 * The splits are handled as well ...
 *
 * hack alert unfinished, incomplete 
 * hack alert -- philosophically speaking, not clear that this is the 
 * right metaphor.  Its OK to poke date into the engine, but writing
 * data out to the database should make use of versioning, and this
 * routine doesn't.
 */

static void
pgendSyncTransaction (PGBackend *be, GUID *trans_guid)
{
   Transaction *trans;
   gboolean engine_data_is_newer = FALSE;
   
   ENTER ("be=%p", be);
   if (!be || !trans_guid) return;

   /* disable callbacks into the backend, and events to GUI */
   gnc_engine_suspend_events();
   pgendDisable(be);

   engine_data_is_newer = pgendCopyTransactionToEngine (be, trans_guid);

   /* if engine data was newer, we save to the db. */
   if (TRUE == engine_data_is_newer) 
   {
      /* XXX hack alert -- fixme */
      PERR ("Data in the local cache is newer than the data in\n"
            "\tthe database.  Thus, the local data will be sent\n"
            "\tto the database.  This mode of operation is\n"
            "\tguarenteed to clobber other user's updates.\n");

      trans = (Transaction *) xaccLookupEntity (trans_guid, GNC_ID_TRANS);

      /* hack alert -- basically, we should use the pgend_commit_transaction
       * routine instead, and in fact, 'StoreTransaction'
       * pretty much shouldn't be allowed to exist in this
       * framework */
      pgendStoreTransaction (be, trans);
      return;
   }

   /* re-enable events to the backend and GUI */
   pgendEnable(be);
   gnc_engine_resume_events();

   LEAVE (" ");
}

/* ============================================================= */
/* The pgendRunQuery() routine performs a search on the SQL database for 
 * all of the splits that correspond to gnc-style query, and then 
 * integrates them into the engine cache.  It does this in several steps:
 *
 * 1) convert the engine style query to SQL.
 * 2) run the SQL query to get the splits that satisfy the query
 * 3) pull the transaction ids out of the split, and
 * 4) 'synchronize' the transactions.
 *
 * That is, we only ever pull complete transactions out of the 
 * engine, and never dangling splits. This helps make sure that
 * the splits always balance in a transaction; it also allows
 * the ledger to operate in 'journal' mode.
 *
 * The pgendRunQueryHelper() routine does most of the dirty work.
 *    It takes as an argument an sql command that must be of the
 *    form "SELECT * FROM gncEntry [...]"
 */

static gpointer
query_cb (PGBackend *be, PGresult *result, int j, gpointer data)
{
   GList *node, *xact_list = (GList *) data;
   GUID *trans_guid;

   /* find the transaction this goes into */
   trans_guid = xaccGUIDMalloc();
   *trans_guid = nullguid;  /* just in case the read fails ... */
   string_to_guid (DB_GET_VAL("transGUID",j), trans_guid);

   /* don't put transaction into the list more than once ... */
   for (node=xact_list; node; node=node->next)
   {
      if (guid_equal ((GUID *)node->data, trans_guid)) 
      {
         return xact_list;
      }
   }

   xact_list = g_list_prepend (xact_list, trans_guid);
   return xact_list;
}

static void 
pgendRunQueryHelper (PGBackend *be, const char *qstring)
{
   GList *node, *xact_list = NULL;

   ENTER ("string=%s\n", qstring);

   SEND_QUERY (be, qstring, );
   xact_list = pgendGetResults (be, query_cb, xact_list);

   /* restore the transactions */
   for (node=xact_list; node; node=node->next)
   {
      pgendCopyTransactionToEngine (be, (GUID *)node->data);
      xaccGUIDFree (node->data);
   }
   g_list_free(xact_list);

   LEAVE (" ");
}

static void 
pgendRunQuery (Backend *bend, Query *q)
{
   PGBackend *be = (PGBackend *)bend;
   const char * sql_query_string;
   sqlQuery *sq;

   ENTER (" ");
   if (!be || !q) return;

   gnc_engine_suspend_events();
   pgendDisable(be);

   /* first thing we do is convert the gnc-engine query into
    * an sql string. */
   sq = sqlQuery_new();
   sql_query_string = sqlQuery_build (sq, q);

   pgendRunQueryHelper (be, sql_query_string);

   sql_Query_destroy(sq);

   pgendEnable(be);
   gnc_engine_resume_events();

   LEAVE (" ");
}

/* ============================================================= */
/* This routine walks the account group, gets all KVP values */

static gpointer
restore_cb (Account *acc, void * cb_data)
{
   PGBackend *be = (PGBackend *) cb_data;
   acc->kvp_data = pgendKVPFetch (be, &(acc->guid), acc->kvp_data);
   return NULL;
}

static void 
pgendGetAllAccountKVP (PGBackend *be, AccountGroup *grp)
{
   if (!grp) return;

   xaccGroupForEachAccountDeeply (grp, restore_cb, be);
}

/* ============================================================= */
/* This routine restores all commodities in the database.
 */

static gpointer
get_commodities_cb (PGBackend *be, PGresult *result, int j, gpointer data)
{
   gnc_commodity_table *comtab = (gnc_commodity_table *) data;
   gnc_commodity *com;

   /* first, lets see if we've already got this one */
   com = gnc_commodity_table_lookup(comtab, 
         DB_GET_VAL("namespace",j), DB_GET_VAL("mnemonic",j));

   if (com) return comtab;

   /* no we don't ... restore it */
   com = gnc_commodity_new (
                    DB_GET_VAL("fullname",j), 
                     DB_GET_VAL("namespace",j), 
                     DB_GET_VAL("mnemonic",j),
                     DB_GET_VAL("code",j),
                     atoi(DB_GET_VAL("fraction",j)));

   gnc_commodity_table_insert (comtab, com);
   return comtab;
}

static void
pgendGetAllCommodities (PGBackend *be)
{
   gnc_commodity_table *comtab;
   char * p;
   if (!be) return;

   ENTER ("be=%p", be);

   comtab = gnc_engine_commodities();
   if (!comtab) {
      PERR ("can't get global commodity table");
      return;
   }

   /* Get them ALL */
   p = "SELECT * FROM gncCommodity;";
   SEND_QUERY (be, p, );
   pgendGetResults (be, get_commodities_cb, comtab);

   LEAVE (" ");
}

/* ============================================================= */
/* This routine restores the account heirarchy of *all* accounts in the DB.
 * It implicitly assumes that the database has only one account
 * heirarchy in it, i.e. any accounts without a parent will be stuffed
 * into the same top group.
 */

static gpointer
get_account_cb (PGBackend *be, PGresult *result, int j, gpointer data)
{
   AccountGroup *topgrp = (AccountGroup *) data;
   Account *parent;
   Account *acc;
   GUID guid;

   /* first, lets see if we've already got this one */
   PINFO ("account GUID=%s", DB_GET_VAL("accountGUID",j));
   guid = nullguid;  /* just in case the read fails ... */
   string_to_guid (DB_GET_VAL("accountGUID",j), &guid);
   acc = (Account *) xaccLookupEntity (&guid, GNC_ID_ACCOUNT);
   if (!acc) 
   {
      acc = xaccMallocAccount();
      xaccAccountBeginEdit(acc);
      xaccAccountSetGUID(acc, &guid);
   }

   xaccAccountSetName(acc, DB_GET_VAL("accountName",j));
   xaccAccountSetDescription(acc, DB_GET_VAL("description",j));
   xaccAccountSetCode(acc, DB_GET_VAL("accountCode",j));
   xaccAccountSetType(acc, xaccAccountStringToEnum(DB_GET_VAL("type",j)));
   xaccAccountSetCommodity(acc, 
       gnc_string_to_commodity (DB_GET_VAL("commodity",j)));

   /* try to find the parent account */
   PINFO ("parent GUID=%s", DB_GET_VAL("parentGUID",j));
   guid = nullguid;  /* just in case the read fails ... */
   string_to_guid (DB_GET_VAL("parentGUID",j), &guid);
   if (guid_equal(xaccGUIDNULL(), &guid)) 
   {
      /* if the parent guid is null, then this
       * account belongs in the top group */
      xaccGroupInsertAccount (topgrp, acc);
   }
   else
   {
      /* if we haven't restored the parent account, create
       * an empty holder for it */
      parent = (Account *) xaccLookupEntity (&guid, GNC_ID_ACCOUNT);
      if (!parent)
      {
         parent = xaccMallocAccount();
         xaccAccountBeginEdit(parent);
         xaccAccountSetGUID(parent, &guid);
      }
      xaccAccountInsertSubAccount(parent, acc);
   }
   xaccAccountCommitEdit(acc);

   return topgrp;
}

static AccountGroup *
pgendGetAllAccounts (PGBackend *be)
{
   AccountGroup *topgrp;
   char * bufp;

   ENTER ("be=%p", be);
   if (!be) return NULL;

   /* first, make sure commodities table is up to date */
   pgendGetAllCommodities (be);

   topgrp = xaccMallocAccountGroup();

   /* Get them ALL */
   bufp = "SELECT * FROM gncAccount;";
   SEND_QUERY (be, bufp, NULL);
   pgendGetResults (be, get_account_cb, topgrp);

   pgendGetAllAccountKVP (be, topgrp);

   /* Mark the newly read group as saved, since the act of putting
    * it together will have caused it to be marked up as not-saved.
    */
   xaccGroupMarkSaved (topgrp);

   LEAVE (" ");
   return topgrp;
}

/* ============================================================= */
/* Like the title suggests, this one sucks *all* of the 
 * transactions out of the database.  This is a potential 
 * CPU and memory-burner; its use is not suggested for anything
 * but single-user mode.
 *
 * To add injury to insult, this routine fetches in a rather 
 * inefficient manner, in particular, the account query.
 */

static void
pgendGetAllTransactions (PGBackend *be, AccountGroup *grp)
{

   gnc_engine_suspend_events();
   pgendDisable(be);

   pgendRunQueryHelper (be, "SELECT * FROM gncEntry;");

   pgendEnable(be);
   gnc_engine_resume_events();
}

/* ============================================================= */
/* return TRUE if this appears to be a fresh, 'null' transaction */
/* it would be better is somehow we could get the gui to mark this
 * as a fresh transaction, rather than having to scan a bunch of 
 * fields.  But this is minor in the scheme of things.
 */

static gboolean
is_trans_empty (Transaction *trans)
{
   Split *s;
   if (!trans) return TRUE;
   if (0 != (xaccTransGetDescription(trans))[0]) return FALSE;
   if (0 != (xaccTransGetNum(trans))[0]) return FALSE;
   if (1 != xaccTransCountSplits(trans)) return FALSE;

   s = xaccTransGetSplit(trans, 0);
   if (TRUE != gnc_numeric_zero_p(xaccSplitGetShareAmount(s))) return FALSE;
   if (TRUE != gnc_numeric_zero_p(xaccSplitGetValue(s))) return FALSE;
   if ('n' != xaccSplitGetReconcile(s)) return FALSE;
   if (0 != (xaccSplitGetMemo(s))[0]) return FALSE;
   if (0 != (xaccSplitGetAction(s))[0]) return FALSE;
   return TRUE;
}

/* ============================================================= */

static int
pgend_trans_commit_edit (Backend * bend, 
                         Transaction * trans,
                         Transaction * oldtrans)
{
   GList *start, *node;
   char * bufp;
   int ndiffs, rollback=0;
   PGBackend *be = (PGBackend *)bend;

   ENTER ("be=%p, trans=%p", be, trans);
   if (!be || !trans) return 1;  /* hack alert hardcode literal */

   /* lock it up so that we query and store atomically */
   /* its not at all clear to me that this isn't rife with deadlocks. */
   bufp = "BEGIN; "
          "LOCK TABLE gncTransaction IN EXCLUSIVE MODE; "
          "LOCK TABLE gncEntry IN EXCLUSIVE MODE; "
          "LOCK TABLE gncAccount IN EXCLUSIVE MODE; "
          "LOCK TABLE gncCommodity IN EXCLUSIVE MODE; ";
   SEND_QUERY (be,bufp, 555);
   FINISH_QUERY(be->connection);

   /* Check to see if this is a 'new' transaction, or not. 
    * The hallmark of a 'new' transaction is that all the 
    * fields are empty.  If its new, then we just go ahead 
    * and commit.  If its old, then we need some consistency 
    * checks.
    */
   if (FALSE == is_trans_empty (oldtrans))
   {
      /* See if the database is in the state that we last left it in.
       * Basically, the database should contain the 'old transaction'.
       * If it doesn't, then someone else has modified this transaction,
       * and thus, any further action on our part would be unsafe.  It
       * is recommended that this be spit back at the GUI, and let a 
       * human decide what to do next.
       */
      ndiffs = pgendCompareOneTransactionOnly (be, oldtrans); 
      if (0 < ndiffs) rollback++;
   
      /* be sure to check the old splits as well ... */
      start = xaccTransGetSplitList (oldtrans);
      for (node=start; node; node=node->next) 
      {
         Split * s = node->data;
         ndiffs = pgendCompareOneSplitOnly (be, s);
         if (0 < ndiffs) rollback++;
      }
   
      if (rollback) {
         bufp = "ROLLBACK;";
         SEND_QUERY (be,bufp,444);
         FINISH_QUERY(be->connection);
   
         PWARN ("Some other user changed this transaction. Please\n"
                "refresh your GUI, type in your changes and try again.\n"
                "(old tranasction didn't match DB, edit rolled back)\n");
         return 666;   /* hack alert */
      } 
   }

   /* if we are here, we are good to go */
   pgendStoreTransactionNoLock (be, trans, FALSE);

   bufp = "COMMIT;";
   SEND_QUERY (be,bufp,333);
   FINISH_QUERY(be->connection);

   /* hack alert -- the following code will get rid of that annoying
    * message from the GUI about saving one's data. However, it doesn't
    * do the right thing if the connection to the backend was ever lost.
    * what should happen is the user should get a chance to
    * resynchronize thier data with the backend, before quiting out.
    */
   {
      Split * s = xaccTransGetSplit (trans, 0);
      Account *acc = xaccSplitGetAccount (s);
      AccountGroup *top = xaccGetAccountRoot (acc);
      xaccGroupMarkSaved (top);
   }

   LEAVE ("commited");
   return 0;
}

/* ============================================================= */

static int
pgend_account_commit_edit (Backend * bend, 
                           Account * acct)
{
   char *p;
   PGBackend *be = (PGBackend *)bend;

   ENTER ("be=%p, acct=%p", be, acct);
   if (!be || !acct) return 1;  /* hack alert hardcode literal */
   if (FALSE == acct->core_dirty) return 0;

   /* lock it up so that we query and store atomically */
   /* its not at all clear to me that this isn't rife with deadlocks. */
   p = "BEGIN; "
       "LOCK TABLE gncAccount IN EXCLUSIVE MODE; "
       "LOCK TABLE gncCommodity IN EXCLUSIVE MODE; ";

   SEND_QUERY (be,p, 555);
   FINISH_QUERY(be->connection);

   /* hack alert -- we should compare old to new, 
    * i.e. compare version numbers, to see if 
    * we're clobbering someone elses changes.  */

   if (acct->do_free)
   {
      p = be->buff; *p = 0;
      p = stpcpy (p, "DELETE FROM gncAccount WHERE accountGuid='");
      p = guid_to_string_buff (xaccAccountGetGUID(acct), p);
      p = stpcpy (p, "';");
      SEND_QUERY (be,be->buff, 444);
      FINISH_QUERY(be->connection);
   }
   else
   {
      pgendStoreAccountNoLock (be, acct, FALSE);
   }

   p = "COMMIT;";
   SEND_QUERY (be,p,333);
   FINISH_QUERY(be->connection);

   /* Mark this up so that we don't get that annoying gui dialog
    * about having to save to file.  unfortunately,however, this
    * is too liberal, and could screw up synchronization if we've lost
    * contact with the back end at some point.  So hack alert -- fix 
    * this. */
   xaccGroupMarkSaved (xaccAccountGetParent(acct));
   LEAVE ("commited");
   return 0;
}

/* ============================================================= */

static const char *
pgendSessionGetMode (PGBackend *be)
{
   switch (be->session_mode)
   {
      case MODE_SINGLE_FILE:
         return "SINGLE-FILE";
      case MODE_SINGLE_UPDATE:
         return "SINGLE-UPDATE";
      case MODE_POLL:
         return "POLL";
      case MODE_EVENT:
         return "EVENT";
      default:
   }
   return "ERROR";
}

/* ============================================================= */
/* Instead of loading the book, just set the lock error */

static AccountGroup *
pgend_book_load_single_lockerr (Backend *bend)
{
   PGBackend *be = (PGBackend *)bend;
   if (!be) return NULL;

   xaccBackendSetError (&be->be, ERR_BACKEND_LOCKED);
   return NULL;
}

/* ============================================================= */
/* Determine whether we can start a session of the desired type.
 * The logic used is as follows:
 * -- if there is any session at all, and we want single
 *    (exclusive) access, then fail.
 * -- if we want any kind of session, and there is a single
 *    (exclusive) session going, then fail.
 * -- otherwise, suceed.
 * Return TRUE if we can get a session.
 *
 * This routine does not lock, but may be used inside a 
 * test-n-set atomic operation.
 */

static gpointer
get_session_cb (PGBackend *be, PGresult *result, int j, gpointer data)
{
   char *lock_holder = (char *) data;
   char *mode = DB_GET_VAL("session_mode", j);

   if ((MODE_SINGLE_FILE == be->session_mode) ||
       (MODE_SINGLE_UPDATE == be->session_mode) ||
       (0 == strcasecmp (mode, "SINGLE-FILE")) ||
       (0 == strcasecmp (mode, "SINGLE-UPDATE")))
   {
      char * hostname = DB_GET_VAL("hostname", j);
      char * username = DB_GET_VAL("login_name",j);
      char * gecos = DB_GET_VAL("gecos",j);
      char * datestr = DB_GET_VAL("time_on", j);

      PWARN ("This database is already opened by \n"
             "\t%s@%s (%s) in mode %s on %s \n",
             username, hostname, gecos, mode, datestr);
      PWARN ("The above messages should be handled by the GUI\n");

      if (lock_holder) return be;
      lock_holder = g_strdup (DB_GET_VAL("sessionGUID",j));
   }
   return lock_holder;
}

static gboolean
pgendSessionCanStart (PGBackend *be, int break_lock)
{
   gboolean retval = TRUE;
   char *p, *lock_holder;

   ENTER (" ");
   /* Find out if there are any open sessions.
    * If 'time_off' is infinity, then user hasn't logged off yet  */
   p = "SELECT * FROM gncSession "
       "WHERE time_off='INFINITY';";
   SEND_QUERY (be,p, FALSE);
   lock_holder = pgendGetResults (be, get_session_cb, NULL);
  
   if (lock_holder) retval = FALSE;

   /* If just one other user has a lock, then will go ahead and 
    * break the lock... If the user approved.  I don't like this
    * but that's what the GUI is set up to do ...
    */
   PINFO ("break_lock=%d nrows=%d lock_holder=%s\n", 
           break_lock, be->nrows, lock_holder);
   if (break_lock && (1==be->nrows) && lock_holder)
   {
      p = be->buff; *p=0;
      p = stpcpy (p, "UPDATE gncSession SET time_off='NOW' "
                     "WHERE sessionGuid='");
      p = stpcpy (p, lock_holder);
      p = stpcpy (p, "';");
     
      SEND_QUERY (be,be->buff, retval);
      FINISH_QUERY(be->connection);
      retval = TRUE;
   }

   if (lock_holder) g_free (lock_holder);

   LEAVE (" ");
   return retval;
}


/* ============================================================= */
/* Determine whether a valid session could be obtained.
 * Return TRUE if we have a session
 * This routine is implemented attomically as a test-n-set.
 */

static gboolean
pgendSessionValidate (PGBackend *be, int break_lock)
{
   gboolean retval = FALSE;
   char *p;
   ENTER(" ");

   if (MODE_NONE == be->session_mode) return FALSE;

   /* Lock it up so that we test-n-set atomically 
    * i.e. we want to avoid a race condition when testing
    * for the single-user session.
    */
   p = "BEGIN;"
       "LOCK TABLE gncSession IN EXCLUSIVE MODE; ";
   SEND_QUERY (be,p, FALSE);
   FINISH_QUERY(be->connection);

   /* check to see if we can start a session of the desired type.  */
   if (FALSE == pgendSessionCanStart (be, break_lock))
   {
      /* this error should be treated just like the 
       * file-lock error from the GUI perspective */
      be->be.book_load = pgend_book_load_single_lockerr;
      xaccBackendSetError (&be->be, ERR_BACKEND_LOCKED);
      retval = FALSE;
   } else {

      /* make note of the session. */
      be->sessionGuid = xaccGUIDMalloc();
      guid_new (be->sessionGuid);
      pgendStoreOneSessionOnly (be, (void *)-1, SQL_INSERT);
      retval = TRUE;
   }

   p = "COMMIT;";
   SEND_QUERY (be,p, FALSE);
   FINISH_QUERY(be->connection);

   LEAVE(" ");
   return retval;
}

/* ============================================================= */
/* log end of session in the database. */

static void
pgendSessionEnd (PGBackend *be)
{
   char *p;

   if (!be->sessionGuid) return;

   p = be->buff; *p=0;
   p = stpcpy (p, "UPDATE gncSession SET time_off='NOW' "
                  "WHERE sessionGuid='");
   p = guid_to_string_buff (be->sessionGuid, p);
   p = stpcpy (p, "';");
  
   SEND_QUERY (be,be->buff, );
   FINISH_QUERY(be->connection);

   xaccGUIDFree (be->sessionGuid); be->sessionGuid = NULL;
}

/* ============================================================= */

static void
pgend_session_end (Backend *bend)
{
   int i;
   PGBackend *be = (PGBackend *)bend;
   if (!be) return;

   ENTER("be=%p", be);

   /* prevent further callbacks into backend */
   pgendDisable(be);

   /* note the logoff time in the session directory */
   pgendSessionEnd (be);

   /* disconnect from the backend */
   if(be->connection) PQfinish (be->connection);
   be->connection = 0;

   if (be->dbName) { g_free(be->dbName); be->dbName = NULL; }
   if (be->portno) { g_free(be->portno); be->portno = NULL; }
   if (be->hostname) { g_free(be->hostname); be->hostname = NULL; }

   sqlBuilder_destroy (be->builder); be->builder = NULL;
   g_free (be->buff); be->buff = NULL; 

   /* free the path strings */
   for (i=0; i< be->path_cache_size; i++) 
   {
      if ((be->path_cache)[i]) g_free ((be->path_cache)[i]);
      (be->path_cache)[i] = NULL;
   }
   g_free (be->path_cache);
   be->path_cache = NULL;
   be->path_cache_size = 0;
   be->ipath_max = 0;

   LEAVE("be=%p", be);
}

/* ============================================================= */
/* the poll & event style load only loads accounts, never the
 * transactions. */

static AccountGroup *
pgend_book_load_poll (Backend *bend)
{
   AccountGroup *grp;
   PGBackend *be = (PGBackend *)bend;
   if (!be) return NULL;

   /* don't send events  to GUI, don't accept callaback to backend */
   gnc_engine_suspend_events();
   pgendDisable(be);

   pgendKVPInit(be);
   grp = pgendGetAllAccounts (be);
   pgendGroupGetAllCheckpoints (be, grp);

   /* re-enable events */
   pgendEnable(be);
   gnc_engine_resume_events();

   return grp;
}

/* ============================================================= */
/* The single-user mode loads all transactions.  Doesn't bother
 * with checkpoints */

static AccountGroup *
pgend_book_load_single (Backend *bend)
{
   AccountGroup *grp;
   PGBackend *be = (PGBackend *)bend;
   if (!be) return NULL;

   /* don't send events  to GUI, don't accept callaback to backend */
   gnc_engine_suspend_events();
   pgendDisable(be);

   pgendKVPInit(be);
   grp = pgendGetAllAccounts (be);
   pgendGetAllTransactions (be, grp);

   /* re-enable events */
   pgendEnable(be);
   gnc_engine_resume_events();

   return grp;
}

/* ============================================================= */

static void
pgend_session_begin (GNCBook *sess, const char * sessionid, 
                    gboolean ignore_lock, gboolean create_new_db)
{
   int really_do_create = 0;
   int rc;
   PGBackend *be;
   char *url, *start, *end;
   char *bufp;

   if (!sess) return;
   be = (PGBackend *) xaccGNCBookGetBackend (sess);

   ENTER("be=%p, sessionid=%s", be, sessionid);

   /* close any dangling sessions from before; reinitialize */
   pgend_session_end ((Backend *) be);
   pgendInit (be);

   /* connect to a bogus database ... */
   /* Parse the sessionid for the hostname, port number and db name.
    * The expected URL format is
    * postgres://some.host.com/db_name
    * postgres://some.host.com:portno/db_name
    * postgres://localhost/db_name
    * postgres://localhost:nnn/db_name
    * 
    * In the future, we might parse urls of the form
    * postgres://some.host.com/db_name?pgkey=pgval&pgkey=pgval
    * 
    */

   if (strncmp (sessionid, "postgres://", 11)) 
   {
      xaccBackendSetError (&be->be, ERR_SQL_BAD_LOCATION);
      return;
   }
   url = g_strdup(sessionid);
   start = url + 11;
   end = strchr (start, ':');
   if (end) 
   {
     /* if colon found, then extract port number */
     *end = 0x0;
     be->hostname = g_strdup (start);
     start = end+1;
     end = strchr (start, '/');
     if (!end) { g_free(url); return; }
     *end = 0;
     be->portno = g_strdup (start);
   } 
   else 
   {
     end = strchr (start, '/');
     if (!end) { g_free(url); return; }
     *end = 0;
     be->hostname = g_strdup (start);
   }
   start = end+1;
   if (0x0 == *start) 
   { 
      xaccBackendSetError (&be->be, ERR_SQL_BAD_LOCATION);
      g_free(url); 
      return; 
   }

   /* chop of trailing url-encoded junk, if present */
   end = strchr (start, '?');
   if (end) *end = 0;
   be->dbName = g_strdup (start);

   g_free(url);

   /* handle localhost as a special case */
   if (!safe_strcmp("localhost", be->hostname))
   {
      g_free (be->hostname);
      be->hostname = NULL;
   }

   be->connection = PQsetdbLogin (be->hostname, 
                                  be->portno,
                                  NULL, /* trace/debug options */
                                  NULL, /* file or tty for debug output */
                                  be->dbName, 
                                  NULL,  /* login */
                                  NULL);  /* pwd */

   /* check the connection status */
   if (CONNECTION_BAD == PQstatus(be->connection))
   {
      PWARN("Connection to database '%s' failed:\n"
           "\t%s", 
           be->dbName, PQerrorMessage(be->connection));
      PQfinish (be->connection);
      be->connection = NULL;

      /* OK, this part is convoluted.
       * I wish that postgres returned usable error codes. 
       * Alas, it does not, so we guess the true error.
       * If the host is 'localhost', and we couldn't connect,
       * then we assume that its because the database doesn't
       * exist (although this might also happen if the database
       * existed, but the user supplied a bad username/password)
       */
      if (NULL == be->hostname)
      {
         if (create_new_db) {
            really_do_create = TRUE;
         } else {
            xaccBackendSetError (&be->be, ERR_BACKEND_NO_SUCH_DB);
            return;
         }
      }
      else
      {
         xaccBackendSetError (&be->be, ERR_SQL_CANT_CONNECT);
         return;
      }
   }

   if (really_do_create)
   {
      char * p;
      be->connection = PQsetdbLogin (be->hostname, 
                                  be->portno,
                                  NULL, /* trace/debug options */
                                  NULL, /* file or tty for debug output */
                                  "gnucash", 
                                  NULL,  /* login */
                                  NULL);  /* pwd */

      /* check the connection status */
      if (CONNECTION_BAD == PQstatus(be->connection))
      {
         PERR("Can't connect to database 'gnucash':\n"
              "\t%s", 
              PQerrorMessage(be->connection));
         PQfinish (be->connection);
         be->connection = NULL;
         xaccBackendSetError (&be->be, ERR_SQL_CANT_CONNECT);
         return;
      }

      /* create the database */
      p = be->buff; *p =0;
      p = stpcpy (p, "CREATE DATABASE ");
      p = stpcpy (p, be->dbName);
      p = stpcpy (p, ";");
      SEND_QUERY (be,be->buff, );
      FINISH_QUERY(be->connection);
      PQfinish (be->connection);

      /* now connect to the newly created database */
      be->connection = PQsetdbLogin (be->hostname, 
                                  be->portno,
                                  NULL, /* trace/debug options */
                                  NULL, /* file or tty for debug output */
                                  be->dbName, 
                                  NULL,  /* login */
                                  NULL);  /* pwd */

      /* check the connection status */
      if (CONNECTION_BAD == PQstatus(be->connection))
      {
         PERR("Can't connect to the newly created database '%s':\n"
              "\t%s", 
              be->dbName,
              PQerrorMessage(be->connection));
         PQfinish (be->connection);
         be->connection = NULL;
         xaccBackendSetError (&be->be, ERR_SQL_CANT_CONNECT);
         return;
      }

      /* finally, create all the tables and indexes */
      SEND_QUERY (be,table_create_str, );
      FINISH_QUERY(be->connection);
   }

   // DEBUGCMD (PQtrace(be->connection, stderr));

   /* set the datestyle to something we can parse */
   bufp = "SET DATESTYLE='ISO';";
   SEND_QUERY (be,bufp, );
   FINISH_QUERY(be->connection);

   /* OK, lets see if we can get a valid session */
   /* hack alert -- we hard-code the access mode here,
    * but it should be user-adjustable.  */
   be->session_mode = MODE_SINGLE_UPDATE;
   rc = pgendSessionValidate (be, ignore_lock);

   /* set up pointers for appropriate behaviour */
   /* In single mode, we load all transactions right away.
    *    and we never have to query the database.
    */
   if (rc)
   {
      switch (be->session_mode)
      {
         case MODE_SINGLE_FILE:
            pgendEnable(be);
            be->be.book_load = pgend_book_load_single;
            be->be.account_begin_edit = NULL;
            be->be.account_commit_edit = NULL;
            be->be.trans_begin_edit = NULL;
            be->be.trans_commit_edit = NULL;
            be->be.trans_rollback_edit = NULL;
            be->be.run_query = NULL;
            be->be.sync = pgendSyncSingleFile;
            PWARN ("MODE_SINGLE_FILE is beta -- \n"
                   "we've fixed all known bugs but that doesn't mean\n"
                   "there aren't any!\n");
            break;

         case MODE_SINGLE_UPDATE:
            pgendEnable(be);
            be->be.book_load = pgend_book_load_single;
            be->be.account_begin_edit = NULL;
            be->be.account_commit_edit = pgend_account_commit_edit;
            be->be.trans_begin_edit = NULL;
            be->be.trans_commit_edit = pgend_trans_commit_edit;
            be->be.trans_rollback_edit = NULL;
            be->be.run_query = NULL;
            be->be.sync = pgendSync;
            PWARN ("MODE_SINGLE_FILE is beta -- \n"
                   "we've fixed all known bugs but that doesn't mean\n"
                   "there aren't any!\n");
            break;

         case MODE_POLL:
            pgendEnable(be);
            be->be.book_load = pgend_book_load_poll;
            be->be.account_begin_edit = NULL;
            be->be.account_commit_edit = pgend_account_commit_edit;
            be->be.trans_begin_edit = NULL;
            be->be.trans_commit_edit = pgend_trans_commit_edit;
            be->be.trans_rollback_edit = NULL;
            be->be.run_query = pgendRunQuery;
            be->be.sync = pgendSync;
            PWARN ("MODE_POLL is experimental -- you will corrupt your data\n");
            break;

         case MODE_EVENT:
            PERR ("MODE_EVENT is unimplemented");
            break;

         default:
            PERR ("bad mode specified");
            break;
      }
   }

   LEAVE("be=%p, sessionid=%s", be, sessionid);
}

/* ============================================================= */

static void
pgendDisable (PGBackend *be)
{
   if (0 > be->nest_count)
   {
      PERR ("too many nested enables");
   }
   be->nest_count ++;
   PINFO("nest count=%d", be->nest_count);
   if (1 < be->nest_count) return;

   /* save hooks */
   be->snr.account_begin_edit  = be->be.account_begin_edit;
   be->snr.account_commit_edit = be->be.account_commit_edit;
   be->snr.trans_begin_edit    = be->be.trans_begin_edit;
   be->snr.trans_commit_edit   = be->be.trans_commit_edit;
   be->snr.trans_rollback_edit = be->be.trans_rollback_edit;
   be->snr.run_query           = be->be.run_query;
   be->snr.sync                = be->be.sync;

   be->be.account_begin_edit  = NULL;
   be->be.account_commit_edit = NULL;
   be->be.trans_begin_edit    = NULL;
   be->be.trans_commit_edit   = NULL;
   be->be.trans_rollback_edit = NULL;
   be->be.run_query           = NULL;
   be->be.sync                = NULL;
}

/* ============================================================= */

static void
pgendEnable (PGBackend *be)
{
   if (0 >= be->nest_count)
   {
      PERR ("too many nested disables");
   }
   be->nest_count --;
   PINFO("nest count=%d", be->nest_count);
   if (be->nest_count) return;

   /* restore hooks */
   be->be.account_begin_edit  = be->snr.account_begin_edit;
   be->be.account_commit_edit = be->snr.account_commit_edit;
   be->be.trans_begin_edit    = be->snr.trans_begin_edit;
   be->be.trans_commit_edit   = be->snr.trans_commit_edit;
   be->be.trans_rollback_edit = be->snr.trans_rollback_edit;
   be->be.run_query           = be->snr.run_query;
   be->be.sync                = be->snr.sync;

}

/* ============================================================= */

static void 
pgendInit (PGBackend *be)
{
   int i;

   /* initialize global variable */
   nullguid = *(xaccGUIDNULL());

   /* access mode */
   be->session_mode = MODE_NONE;
   be->sessionGuid = NULL;

   /* generic backend handlers */
   be->be.book_begin = pgend_session_begin;
   be->be.book_load = NULL;
   be->be.book_end = pgend_session_end;

   be->be.account_begin_edit = NULL;
   be->be.account_commit_edit = NULL;
   be->be.trans_begin_edit = NULL;
   be->be.trans_commit_edit = NULL;
   be->be.trans_rollback_edit = NULL;
   be->be.run_query = NULL;
   be->be.sync = NULL;

   be->nest_count = 0;
   pgendDisable(be);

   be->be.last_err = ERR_BACKEND_NO_ERR;

   /* postgres specific data */
   be->hostname = NULL;
   be->portno = NULL;
   be->dbName = NULL;
   be->connection = NULL;

   be->builder = sqlBuilder_new();

   be->buff = g_malloc (QBUFSIZE);
   be->bufflen = QBUFSIZE;
   be->nrows = 0;

#define INIT_CACHE_SZ 1000
   be->path_cache = (char **) g_malloc (INIT_CACHE_SZ * sizeof(char *));
   be->path_cache_size = INIT_CACHE_SZ;
   for (i=0; i< be->path_cache_size; i++) {
      (be->path_cache)[i] = NULL;
   }
   be->ipath_max = 0;
}

/* ============================================================= */

Backend * 
pgendNew (void)
{
   PGBackend *be;
   be = (PGBackend *) g_malloc (sizeof (PGBackend));
   pgendInit (be);
   return (Backend *) be;
}

/* ======================== END OF FILE ======================== */
