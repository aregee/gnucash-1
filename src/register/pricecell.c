/********************************************************************\
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
 * along with this program; if not, write to the Free Software      *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.        *
\********************************************************************/

/*
 * FILE:
 * pricecell.c
 *
 * FUNCTION:
 * Implements the price cell
 *
 * HISTORY:
 * Copyright (c) 1998, 1999, 2000 Linas Vepstas
 */

#include <ctype.h>
#include <string.h>
#include <locale.h>

#include "util.h"

#include "basiccell.h"
#include "pricecell.h"


static void PriceSetValue (BasicCell *, const char *);
static char * xaccPriceCellPrintValue (PriceCell *cell);

/* set the color of the text to red, if the value is negative */
/* hack alert -- the actual color should probably be configurable */
#define COLORIZE(cell,amt) {			\
   if (0.0 > amt) {				\
      /* red */					\
      cell->cell.fg_color = 0xff0000;		\
   } else {					\
      /* black */				\
      cell->cell.fg_color = 0x0;		\
   }						\
}

#define SET(cell,str) { 			\
   if ((cell)->value) free ((cell)->value);	\
   (cell)->value = strdup (str);		\
}

#define PRTBUF 40

/* ================================================ */

static const char * 
PriceEnter (BasicCell *_cell,
            const char *val,
            int *cursor_position,
            int *start_selection,
            int *end_selection)
{
  *cursor_position = -1;
  *start_selection = 0;
  *end_selection   = -1;

  return val;
}

/* ================================================ */
/* This callback only allows numbers with a single
 * decimal point in them */

static const char * 
PriceMV (BasicCell *_cell, 
         const char *oldval, 
         const char *change, 
         const char *newval,
         int *cursor_position,
         int *start_selection,
         int *end_selection)
{
   PriceCell *cell = (PriceCell *) _cell;
   struct lconv *lc = gnc_localeconv();
   char decimal_point;
   char thousands_sep;

   if (cell->monetary)
     decimal_point = lc->mon_decimal_point[0];
   else
     decimal_point = lc->decimal_point[0];

   if (cell->monetary)
     thousands_sep = lc->mon_thousands_sep[0];
   else
     thousands_sep = lc->thousands_sep[0];

   /* accept the newval string if user action was delete, etc. */
   if (change != NULL)
   {
      int i, count=0;

      for (i = 0; 0 != change[i]; i++)
      {
        /* accept only numbers or a decimal point or a thousands sep */
        if (!isdigit(change[i]) &&
            (decimal_point != change[i]) &&
            (thousands_sep != change[i]))
          return NULL;

        if (decimal_point == change[i])
          count++;
      }

      for (i = 0; 0 != oldval[i]; i++)
        if (decimal_point == oldval[i])
          count++;

      if (1 < count) return NULL;
   }

   /* parse the float pt value and store it */
   cell->amount = xaccParseAmount (newval, cell->monetary);
   SET ((&(cell->cell)), newval);
   return newval; 
}

/* ================================================ */

static const char * 
PriceLeave (BasicCell *_cell, const char *val) 
{
   PriceCell *cell = (PriceCell *) _cell;
   char *newval;

   newval = xaccPriceCellPrintValue(cell);

   /* If they are identical, return the original */
   if (strcmp(newval, val) == 0)
     return val;

   /* Otherwise, return the new one. */
   SET ((&(cell->cell)), newval);
   return strdup(newval);
}

/* ================================================ */

static char *
PriceHelp(BasicCell *bcell)
{
  PriceCell *cell = (PriceCell *) bcell;

  if ((bcell->value != NULL) && (bcell->value[0] != 0))
  {
    char *help_str;

    help_str = xaccPriceCellPrintValue(cell);

    return strdup(help_str);
  }

  if (bcell->blank_help != NULL)
    return strdup(bcell->blank_help);

  return NULL;
}

/* ================================================ */

PriceCell *
xaccMallocPriceCell (void)
{
   PriceCell *cell;
   cell = (PriceCell *) malloc (sizeof (PriceCell));
   xaccInitPriceCell (cell);
   return cell;
}

/* ================================================ */

void
xaccInitPriceCell (PriceCell *cell)
{
   xaccInitBasicCell( &(cell->cell));

   cell->amount = 0.0;
   cell->blank_zero = GNC_T;
   cell->monetary = GNC_T;
   cell->shares_value = GNC_F;

   SET (&(cell->cell), "");

   cell->cell.use_fg_color = 1;
   cell->cell.enter_cell = PriceEnter;
   cell->cell.modify_verify = PriceMV;
   cell->cell.leave_cell = PriceLeave;
   cell->cell.set_value = PriceSetValue;
   cell->cell.get_help_value = PriceHelp;
}

/* ================================================ */

void
xaccDestroyPriceCell (PriceCell *cell)
{
   cell->amount = 0.0;
   xaccDestroyBasicCell (&(cell->cell));
}

/* ================================================ */

static char *
xaccPriceCellPrintValue (PriceCell *cell)
{
  static char buff[PRTBUF];
  short flags = PRTSEP;

  if (cell->blank_zero && DEQ(cell->amount, 0.0)) {
     strcpy(buff, "");
     return buff;
  }

  if (cell->shares_value)
    flags |= PRTSHR;

  xaccSPrintAmount(buff, cell->amount, flags, NULL);

  return buff;
}

/* ================================================ */
double
xaccGetPriceCellValue (PriceCell *cell)
{
  if (cell == NULL)
    return 0.0;

  return cell->amount;
}

void
xaccSetPriceCellValue (PriceCell * cell, double amt)
{
   char *buff;

   if (cell == NULL)
     return;

   cell->amount = amt;
   buff = xaccPriceCellPrintValue (cell);

   SET ( &(cell->cell), buff);

   /* set the cell color to red if the value is negative */
   COLORIZE (cell, amt);
}

void
xaccSetPriceCellBlank (PriceCell *cell)
{
  if (cell == NULL)
    return;

  cell->amount = 0.0;

  SET ( &(cell->cell), "");

  COLORIZE (cell, 0.0);
}

/* ================================================ */

void
xaccSetPriceCellSharesValue (PriceCell * cell, gncBoolean shares_value)
{
  assert(cell != NULL);

  cell->shares_value = shares_value;
}

/* ================================================ */

void
xaccSetPriceCellMonetary (PriceCell * cell, gncBoolean monetary)
{
  assert(cell != NULL);

  cell->monetary = monetary;
}

/* ================================================ */

void
xaccSetPriceCellBlankZero (PriceCell *cell, gncBoolean blank_zero)
{
  assert(cell != NULL);

  cell->blank_zero = blank_zero;
}

/* ================================================ */

void xaccSetDebCredCellValue (PriceCell * debit,
                              PriceCell * credit,
                              double amount)
{
   debit->cell.fg_color = 0xff0000;
   credit->cell.fg_color = 0x0;

   /* debits are positive, credits are negative */
   if (amount > 0.0) {
      xaccSetPriceCellValue (debit, amount);
      xaccSetPriceCellValue (credit, 0.0);
      if (!credit->blank_zero) {
        SET(&credit->cell, "");
      }
   } else {
      xaccSetPriceCellValue (debit, 0.0);
      if (!debit->blank_zero) {
        SET(&debit->cell, "");
      }
      xaccSetPriceCellValue (credit, -amount);
   }
}

/* ================================================ */

static void 
PriceSetValue (BasicCell *_cell, const char *str)
{
   PriceCell *cell = (PriceCell *) _cell;
   double amount;

   if (str == NULL)
     str = "";

   if (!cell->blank_zero && (*str == '\0'))
     xaccSetPriceCellBlank(cell);
   else {
     amount = xaccParseAmount (str, cell->monetary);
     xaccSetPriceCellValue (cell, amount);
   }
}

/* --------------- end of file ---------------------- */
