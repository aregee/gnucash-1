/********************************************************************
 * window-main.h -- public GNOME main window functions              *
 * Copyright (C) 1998,1999 Linas Vepstas                            *
 * Copyright (C) 2001 Bill Gribble <grib@gnumatic.com>              *
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
 ********************************************************************/

#ifndef WINDOW_MAIN_H
#define WINDOW_MAIN_H

#include "gnc-mdi-utils.h"

GNCMDIInfo * gnc_main_window_new (void);
void gnc_shutdown(int);

/*
 * Functions used as callbacks from multiple dialogs.
 */
void gnc_main_window_about_cb (GtkWidget *widget, gpointer data);
void gnc_main_window_file_save_cb(GtkWidget * widget, gpointer data);
void gnc_main_window_file_save_as_cb(GtkWidget * widget, gpointer data);
void gnc_main_window_tutorial_cb (GtkWidget *widget, gpointer data);
void gnc_main_window_totd_cb (GtkWidget *widget, gpointer data);
void gnc_main_window_help_cb (GtkWidget *widget, gpointer data);
void gnc_main_window_exit_cb (GtkWidget *widget, gpointer data);
void gnc_main_window_file_save_cb(GtkWidget * widget, gpointer data);
void gnc_main_window_file_save_as_cb(GtkWidget * widget, gpointer data);
void gnc_main_window_fincalc_cb(GtkWidget *widget, gpointer data);
void gnc_main_window_gl_cb(GtkWidget *widget, gpointer data);
void gnc_main_window_prices_cb(GtkWidget *widget, gpointer data);
void gnc_main_window_find_transactions_cb (GtkWidget *widget, gpointer data);
void gnc_main_window_sched_xaction_cb (GtkWidget *widget, gpointer data);
void gnc_main_window_commodities_cb(GtkWidget *widget, gpointer data);

#endif
