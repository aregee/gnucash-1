/********************************************************************\
 * gnc-gui-query.c -- functions for creating dialogs for GnuCash    * 
 * Copyright (C) 1998, 1999, 2000 Linas Vepstas                     *
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
\********************************************************************/

#include "config.h"

#include <gnome.h>
 
#include "dialog-utils.h"
#include "gnc-gconf-utils.h"
#include "gnc-engine-util.h"
#include "gnc-gui-query.h"
#include "gnc-ui.h"
#include "messages.h"


/* This static indicates the debugging module that this .o belongs to.  */
/* static short module = MOD_GUI; */

void gnc_remember_all_toggled (GtkToggleButton *togglebutton, gpointer user_data);


void
gnc_remember_all_toggled (GtkToggleButton *togglebutton,
			  gpointer user_data)
{
  GtkWidget *other_button;
  gboolean active;

  active = gtk_toggle_button_get_active(togglebutton);
  other_button = gnc_glade_lookup_widget(GTK_WIDGET(togglebutton),
					 "remember_one");
  gtk_widget_set_sensitive(other_button, !active);
}


static gint
gnc_remember_common(gncUIWidget parent, const gchar *dialog_name,
		    const gchar *message, const gchar *gconf_key,
		    const gchar *first_button_text, ...)
{
    GladeXML *xml;
    GtkWidget *dialog, *label, *box, *checkbox;
    gint response;
    const gchar *text;
    va_list args;

    /* Does the user want to see this question? If not, return the
     * previous answer. */
    response = gnc_gconf_get_int(GCONF_WARNINGS_PERM, gconf_key, NULL);
    if (response != 0)
      return response;
    response = gnc_gconf_get_int(GCONF_WARNINGS_TEMP, gconf_key, NULL);
    if (response != 0)
      return response;

    /* Find the glade page layout */
    xml = gnc_glade_xml_new ("gnc-gui-query.glade", dialog_name);
    dialog = glade_xml_get_widget (xml, dialog_name);

    /* Insert the message. */
    label = glade_xml_get_widget (xml, "label");
    gtk_label_set_markup(GTK_LABEL(label), message);

    /* Hide the checkboxes if there's no key */
    box = glade_xml_get_widget (xml, "remember_vbox");
    if (gconf_key == NULL)
      gtk_widget_hide(box);

    /* Set the buttons */
    va_start(args, first_button_text);
    for (text = first_button_text; text != NULL; ) {
      response = va_arg(args, gint);
      gtk_dialog_add_button(GTK_DIALOG(dialog), text, response);
      text = va_arg(args, gchar *);
    }
    va_end(args);
//    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
    
    /* Tell the window manager if there's a parent window. */
    if (parent)
      gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW (parent));

    /* Get a response */
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if ((response == GTK_RESPONSE_NONE) || (response == GTK_RESPONSE_DELETE_EVENT)) {
      gtk_widget_destroy(GTK_WIDGET(dialog));
      return GTK_RESPONSE_NO;
    }

    /* Save the answer? */
    checkbox = glade_xml_get_widget (xml, "remember_all");
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox)))
      gnc_gconf_set_int(GCONF_WARNINGS_PERM, gconf_key, response, NULL);
    checkbox = glade_xml_get_widget (xml, "remember_one");
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox)))
      gnc_gconf_set_int(GCONF_WARNINGS_TEMP, gconf_key, response, NULL);

    gtk_widget_destroy(GTK_WIDGET(dialog));
    return response;
}


/********************************************************************\
 * gnc_ok_cancel_dialog                                             *
 *   display a message, and asks the user to press "Ok" or "Cancel" *
 *                                                                  *
 * NOTE: This function does not return until the dialog is closed   *
 *                                                                  *
 * Args:   parent  - the parent window                              *
 *         default - the button that will be the default            *
 *         message - the message to display                         *
 *         format - the format string for the message to display    *
 *                   This is a standard 'printf' style string.      *
 *         args - a pointer to the first argument for the format    *
 *                string.                                           *
 * Return: the result the user selected                             *
\********************************************************************/
gint
gnc_ok_cancel_dialog(gncUIWidget parent,
		     gint default_result,
		     const gchar *format,...)
{
  GtkWidget *dialog = NULL;
  gint result;
  gchar *buffer;
  va_list args;

  if (parent == NULL)
    parent = gnc_ui_get_toplevel();

  va_start(args, format);
  buffer = g_strdup_vprintf(format, args);
  dialog = gtk_message_dialog_new (GTK_WINDOW(parent),
				   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_QUESTION,
				   GTK_BUTTONS_OK_CANCEL,
				   buffer);
  g_free(buffer);
  va_end(args);

  gtk_dialog_set_default_response (GTK_DIALOG(dialog), default_result);
  result = gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy (dialog);
  return(result);
}



/********************************************************************\
 * gnc_verify_cancel_dialog                                         *
 *   display a message, and asks the user to press "Yes", "No", or  *
 *   "Cancel"                                                       *
 *                                                                  *
 * NOTE: This function does not return until the dialog is closed   *
 *                                                                  *
 * Args:   parent  - the parent window                              *
 *         default - the button that will be the default            *
 *         format - the format string for the message to display    *
 *                   This is a standard 'printf' style string.      *
 *         args - a pointer to the first argument for the format    *
 *                string.                                           *
 * Return: the result the user selected                             *
\********************************************************************/

gint
gnc_verify_cancel_dialog(GtkWidget *parent,
			 gint default_result,
			 const gchar *format, ...)
{
  GtkWidget *dialog;
  gint result;
  gchar *buffer;
  va_list args;

  if (parent == NULL)
    parent = gnc_ui_get_toplevel();

  va_start(args, format);
  buffer = g_strdup_vprintf(format, args);
  dialog = gtk_message_dialog_new (GTK_WINDOW(parent),
				   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_QUESTION,
				   GTK_BUTTONS_NONE,
				   buffer);
  g_free(buffer);
  va_end(args);

  gtk_dialog_add_buttons (GTK_DIALOG(dialog),
			  GTK_STOCK_YES,    GTK_RESPONSE_YES,
			  GTK_STOCK_NO,     GTK_RESPONSE_NO,
			  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			  NULL);

  gtk_dialog_set_default_response(GTK_DIALOG(dialog), default_result);
  result = gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy (dialog);
  return(result);
}


/********************************************************************\
 * gnc_verify_dialog                                                *
 *   display a message, and asks the user to press "Yes" or "No"    *
 *                                                                  *
 * NOTE: This function does not return until the dialog is closed   *
 *                                                                  *
 * Args:   parent  - the parent window                              *
 *         yes_is_default - If true, "Yes" is default,              *
 *                          "No" is the default button.             *
 *         format - the format string for the message to display    *
 *                   This is a standard 'printf' style string.      *
 *         args - a pointer to the first argument for the format    *
 *                string.                                           *
\********************************************************************/
gboolean
gnc_verify_dialog(gncUIWidget parent, gboolean yes_is_default,
		  const gchar *format, ...)
{
  GtkWidget *dialog;
  gchar *buffer;
  gint result;
  va_list args;

  if (parent == NULL)
    parent = gnc_ui_get_toplevel();

  va_start(args, format);
  buffer = g_strdup_vprintf(format, args);
  dialog = gtk_message_dialog_new (GTK_WINDOW(parent),
				   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_QUESTION,
				   GTK_BUTTONS_YES_NO,
				   buffer);
  g_free(buffer);
  va_end(args);

  gtk_dialog_set_default_response(GTK_DIALOG(dialog),
				  (yes_is_default ? GTK_RESPONSE_YES : GTK_RESPONSE_NO));
  result = gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy (dialog);
  return (result == GTK_RESPONSE_YES);
}


gint
gnc_verify_remember_dialog(gncUIWidget parent, const gchar *gconf_key,
			   const gchar *yes_label, const gchar *no_label,
			   const gchar *format, ...)
{
    gchar *buffer;
    gint response;
    va_list args;

    va_start(args, format);
    buffer = g_strdup_vprintf(format, args);
    response = gnc_remember_common(parent, "Question Dialog", buffer, gconf_key,
				   yes_label, GTK_RESPONSE_YES,
				   no_label, GTK_RESPONSE_NO,
				   NULL);
    g_free(buffer);
    va_end(args);
    return response;
}


/********************************************************************\
 * gnc_info_dialog                                                  * 
 *   displays an information dialog box                             * 
 *                                                                  * 
 * Args:   parent  - the parent window                              *  
 *         format - the format string for the message to display    *
 *                   This is a standard 'printf' style string.      *
 *         args - a pointer to the first argument for the format    *
 *                string.                                           *
 * Return: none                                                     * 
\********************************************************************/
void 
gnc_info_dialog(GtkWidget *parent, const gchar *format, ...)
{
  GtkWidget *dialog;
  gchar *buffer;
  va_list args;

  if (parent == NULL)
    parent = gnc_ui_get_toplevel();

  va_start(args, format);
  buffer = g_strdup_vprintf(format, args);
  dialog = gtk_message_dialog_new (GTK_WINDOW(parent),
				   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_INFO,
				   GTK_BUTTONS_CLOSE,
				   buffer);
  va_end(args);

  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy (dialog);
}



/********************************************************************\
 * gnc_warning_dialog_common                                        * 
 *   displays a warning dialog box                                  * 
 *                                                                  * 
 * Args:   parent  - the parent window                              *  
 *         format - the format string for the message to display    *
 *                   This is a standard 'printf' style string.      *
 *         args - a pointer to the first argument for the format    *
 *                string.                                           *
 * Return: none                                                     * 
\********************************************************************/
static void 
gnc_warning_dialog_common(GtkWidget *parent, const gchar *format, va_list args)
{
  GtkWidget *dialog = NULL;
  gchar *buffer;

  if (parent == NULL)
    parent = GTK_WIDGET(gnc_ui_get_toplevel());

  buffer = g_strdup_vprintf(format, args);
  dialog = gtk_message_dialog_new (GTK_WINDOW(parent),
				   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_WARNING,
				   GTK_BUTTONS_CLOSE,
				   buffer);
  g_free(buffer);

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

void 
gnc_warning_dialog_va(const gchar *format, va_list args)
{
  gnc_warning_dialog_common(NULL, format, args);
}

void 
gnc_warning_dialog(GtkWidget *parent, const gchar *format, ...)
{
  va_list args;

  va_start(args, format);
  gnc_warning_dialog_common(parent, format, args);
  va_end(args);
}


gint
gnc_warning_remember_dialog(gncUIWidget parent, const gchar *gconf_key,
			   const gchar *yes_label, const gchar *no_label,
			   const gchar *format, ...)
{
    gchar *buffer;
    gint response;
    va_list args;

    va_start(args, format);
    buffer = g_strdup_vprintf(format, args);
    response = gnc_remember_common(parent, "Warning Dialog", buffer, gconf_key,
				   yes_label, GTK_RESPONSE_YES,
				   no_label, GTK_RESPONSE_NO,
				   NULL);
    g_free(buffer);
    va_end(args);
    return response;
}



/********************************************************************\
 * gnc_error_dialog_common                                          * 
 *   displays an error dialog box                                   * 
 *                                                                  * 
 * Args:   parent  - the parent window                              *
 *         format - the format string for the message to display    *
 *                   This is a standard 'printf' style string.      *
 *         args - a pointer to the first argument for the format    *
 *                string.                                           *
 * Return: none                                                     * 
\********************************************************************/
static void 
gnc_error_dialog_common(GtkWidget *parent, const gchar *format, va_list args)
{
  GtkWidget *dialog;
  gchar *buffer;

  if (parent == NULL)
    parent = GTK_WIDGET(gnc_ui_get_toplevel());

  buffer = g_strdup_vprintf(format, args);
  dialog = gtk_message_dialog_new (GTK_WINDOW(parent),
				   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_ERROR,
				   GTK_BUTTONS_CLOSE,
				   buffer);
  g_free(buffer);

  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy (dialog);
}

void 
gnc_error_dialog_va(const gchar *format, va_list args)
{
  gnc_error_dialog_common(NULL, format, args);
}

void 
gnc_error_dialog(GtkWidget *parent, const gchar *format, ...)
{
  va_list args;

  va_start(args, format);
  gnc_error_dialog_common(parent, format, args);
  va_end(args);
}


/********************************************************************\
 * gnc_generic_dialog_common                                        *
 *   display a message, and asks the user to choose from a          *
 *    number of selections.                                         *
 *                                                                  *
 * NOTE: This function does not return until the dialog is closed   *
 *                                                                  *
 * Args:   parent  - the parent window                              *
 *         type - type of dialog to display (use gnome constants)   *
 *         buttons - Names of the buttons to display                *
 *         format - the format string for the message to display    *
 *                   This is a standard 'printf' style string.      *
 *         args - a pointer to the first argument for the format    *
 *                string.                                           *
\********************************************************************/
#define MAX_BUTTONS 5
static int
gnc_generic_dialog_common(gncUIWidget parent, const char *type,
			  const char **buttons_in,
			  const gchar *format, va_list args)
{
  GtkWidget *verify_box = NULL;
  const gchar *buttons[MAX_BUTTONS+1];
  gchar *buffer;
  gint i;

  /* Translate the buttons */
  for (i = 0; (i < MAX_BUTTONS) && buttons_in[i]; i++) {
    buttons[i] = gettext(buttons_in[i]);
  }
  g_assert(i < MAX_BUTTONS);
  buttons[i] = NULL;

  buffer = g_strdup_vprintf(format, args);
  verify_box = gnome_message_box_newv(buffer, type, buttons);
  g_free(buffer);

  if (parent != NULL)
    gnome_dialog_set_parent(GNOME_DIALOG(verify_box), GTK_WINDOW(parent));
  
  gnome_dialog_set_default(GNOME_DIALOG(verify_box), 0);

  return (gnome_dialog_run_and_close(GNOME_DIALOG(verify_box)));
}

int
gnc_generic_question_dialog(GtkWidget *parent, const char **buttons,
			    const gchar *format, ...)
{
  int result;
  va_list args;

  va_start(args, format);
  result = gnc_generic_dialog_common(parent,
				     GNOME_MESSAGE_BOX_QUESTION,
				     buttons, format, args);
  va_end(args);
  return(result);
}

int
gnc_generic_warning_dialog(GtkWidget *parent, const char **buttons,
			   const char *format, ...)
{
  int result;
  va_list args;

  va_start(args, format);
  result = gnc_generic_dialog_common(parent,
				     GNOME_MESSAGE_BOX_WARNING,
				     buttons, format, args);
  va_end(args);
  return(result);
}

static void
gnc_choose_radio_button_cb(GtkWidget *w, gpointer data)
{
  int *result = data;

  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)))
    *result = GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(w)));
}

/********************************************************************
 gnc_choose_radio_option_dialog

 display a group of radio_buttons and return the index of
 the selected one
*/

int
gnc_choose_radio_option_dialog(gncUIWidget parent,
			       const char *title, 
			       const char *msg,
			       int default_value,
			       GList *radio_list)
{
  int radio_result = 0; /* initial selected value is first one */
  GtkWidget *vbox;
  GtkWidget *main_vbox;
  GtkWidget *label;
  GtkWidget *frame;
  GtkWidget *radio_button;
  GtkWidget *dialog;
  GtkWidget *dvbox;
  GSList *group = NULL;
  GList *node;
  int i;

  main_vbox = gtk_vbox_new(FALSE, 3);
  gtk_container_border_width(GTK_CONTAINER(main_vbox), 5);
  gtk_widget_show(main_vbox);

  label = gtk_label_new(msg);
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
  gtk_box_pack_start(GTK_BOX(main_vbox), label, FALSE, FALSE, 0);
  gtk_widget_show(label);

  frame = gtk_frame_new(NULL);
  gtk_container_border_width(GTK_CONTAINER(frame), 5);
  gtk_box_pack_start(GTK_BOX(main_vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show(frame);

  vbox = gtk_vbox_new(TRUE, 3);
  gtk_container_border_width(GTK_CONTAINER(vbox), 5);
  gtk_container_add(GTK_CONTAINER(frame), vbox);
  gtk_widget_show(vbox);

  for (node = radio_list, i = 0; node; node = node->next, i++)
  {
    radio_button = gtk_radio_button_new_with_label(group, node->data);
    group = gtk_radio_button_group(GTK_RADIO_BUTTON(radio_button));

    if (i == default_value) /* default is first radio button */
    {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_button), TRUE);
      radio_result = default_value;
    }

    gtk_widget_show(radio_button);
    gtk_box_pack_start(GTK_BOX(vbox), radio_button, FALSE, FALSE, 0);
    gtk_object_set_user_data(GTK_OBJECT(radio_button), GINT_TO_POINTER(i));
    gtk_signal_connect(GTK_OBJECT(radio_button), "clicked",
		       GTK_SIGNAL_FUNC(gnc_choose_radio_button_cb),
		       &radio_result);
  }

  dialog = gtk_dialog_new_with_buttons (title, GTK_WINDOW(parent),
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					GTK_STOCK_OK, GTK_RESPONSE_OK,
					NULL);

  /* default to ok */
  gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

  dvbox = GTK_DIALOG(dialog)->vbox;

  gtk_box_pack_start(GTK_BOX(dvbox), main_vbox, TRUE, TRUE, 0);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK)
    radio_result = -1;

  gtk_widget_destroy (dialog);

  return radio_result;
}
