/********************************************************************
 * gnc-engine.c  -- top-level initialization for Gnucash Engine     *
 * Copyright 2000 Bill Gribble <grib@billgribble.com>               *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
 ********************************************************************/

#include "config.h"

#include <glib.h>

#include "gnc-engine.h"
#include "qof.h"

#include "AccountP.h"
#include "GroupP.h"
#include "SX-book-p.h"
#include "gnc-budget-book-p.h"
#include "TransactionP.h"
#include "gnc-commodity.h"
#include "gnc-lot-p.h"
#include "SchedXactionP.h"
#include "FreqSpecP.h"
#include "gnc-pricedb-p.h"

/** gnc file backend library name */
#define GNC_LIB_NAME "libgnc-backend-file.la"
/** init_fcn for gnc file backend library. */
#define GNC_LIB_INIT "gnc_provider_init"

static GList * engine_init_hooks = NULL;
static int engine_is_initialized = 0;

/* GnuCash version functions */
unsigned int
gnucash_major_version (void)
{
  return GNUCASH_MAJOR_VERSION;
}

unsigned int
gnucash_minor_version (void)
{
  return GNUCASH_MINOR_VERSION;
}

unsigned int
gnucash_micro_version (void)
{
  return GNUCASH_MICRO_VERSION;
}

/********************************************************************
 * gnc_engine_init
 * initialize backend, load any necessary databases, etc. 
 ********************************************************************/

void 
gnc_engine_init(int argc, char ** argv)
{
  gnc_engine_init_hook_t hook;
  GList                  * cur;

  if (1 == engine_is_initialized) return;
  engine_is_initialized = 1;

  /* initialize logging to our file. */
  qof_log_init_filename("/tmp/gnucash.trace");
  /* Only set the core log_modules here
	the rest can be set locally.  */
  gnc_set_log_level(GNC_MOD_ENGINE, GNC_LOG_WARNING);
  gnc_set_log_level(GNC_MOD_IO, GNC_LOG_WARNING);
  gnc_set_log_level(GNC_MOD_GUI, GNC_LOG_WARNING);
  qof_log_set_default(GNC_LOG_WARNING);
  
  guid_init ();
  qof_object_initialize ();
  qof_query_init ();
  qof_book_register ();

  /* Now register our core types */
  xaccSplitRegister ();
  xaccTransRegister ();
  xaccAccountRegister ();
  xaccGroupRegister ();
  gnc_sxtt_register ();
  FreqSpecRegister ();
  SXRegister ();
  gnc_budget_register();
  gnc_pricedb_register ();
  gnc_commodity_table_register();
  gnc_lot_register ();

  g_return_if_fail((qof_load_backend_library 
		(QOF_LIB_DIR, "libqof-backend-qsf.la", "qsf_provider_init")));
  g_return_if_fail((qof_load_backend_library
		(QOF_LIB_DIR, GNC_LIB_NAME, GNC_LIB_INIT)));

  /* call any engine hooks */
  for (cur = engine_init_hooks; cur; cur = cur->next)
  {
    hook = (gnc_engine_init_hook_t)cur->data;

    if (hook)
      (*hook)(argc, argv);
  }
}

/********************************************************************
 * gnc_engine_shutdown
 * shutdown backend, destroy any global data, etc.
 ********************************************************************/

void
gnc_engine_shutdown (void)
{
  qof_query_shutdown ();
  qof_object_shutdown ();
  guid_shutdown();
  gnc_engine_string_cache_destroy ();
  qof_log_shutdown();
}

/********************************************************************
 * gnc_engine_add_init_hook
 * add a startup hook 
 ********************************************************************/

void
gnc_engine_add_init_hook(gnc_engine_init_hook_t h) {
  engine_init_hooks = g_list_append(engine_init_hooks, (gpointer)h);
}
