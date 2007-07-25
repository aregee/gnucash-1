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
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
\********************************************************************/

/** @file
     @brief CSV import GUI
     *
     gnc-csv-import.h
     @author Copyright (c) 2007 Benny Sperisen <lasindi@gmail.com>
 */

#ifndef GNC_CSV_MODEL_H
#define GNC_CSV_MODEL_H

#include "config.h"

#include "Account.h"
#include "Transaction.h"

#include "stf/stf-parse.h"

/** Enumeration for column types. These are the different types of
 * columns that can exist in a CSV/Fixed-Width file. There should be
 * no two columns with the same type except for the GNC_CSV_NONE
 * type. */
enum GncCsvColumnType {GNC_CSV_NONE,
                       GNC_CSV_DATE,
                       GNC_CSV_DESCRIPTION,
                       GNC_CSV_AMOUNT,
                       GNC_CSV_NUM_COL_TYPES};

/** Enumeration for error types. These are the different types of
 * errors that various functions used for the CSV/Fixed-Width importer
 * can have. */
enum GncCsvErrorType {GNC_CSV_FILE_OPEN_ERR,
                      GNC_CSV_ENCODING_ERR};

/** Struct for containing a string. This struct simply contains
 * pointers to the beginning and end of a string. We need this because
 * the STF code that gnc_csv_parse calls requires these pointers. */
typedef struct
{
  char* begin;
  char* end;
} GncCsvStr;

/** Struct pairing a transaction with a line number. This struct is
 * used to keep the transactions in order. When rows are separated
 * into "valid" and "error" lists (in case some of the rows have cells
 * that are unparseable), we want the user to still be able to
 * "correct" the error list. If we keep the line numbers of valid
 * transactions, we can then put transactions created from the newly
 * corrected rows into the right places. */
typedef struct
{
  int line_no;
  Transaction* trans;
} GncCsvTransLine;

extern const int num_date_formats;
/* A set of date formats that the user sees. */
extern const gchar* date_format_user[];

/** Struct containing data for parsing a CSV/Fixed-Width file. */
typedef struct
{
  gchar* encoding;
  GMappedFile* raw_mapping; /**< The mapping containing raw_str */
  GncCsvStr raw_str; /**< Untouched data from the file as a string */
  GncCsvStr file_str; /**< raw_str translated into UTF-8 */
  GPtrArray* orig_lines; /**< file_str parsed into a two-dimensional array of strings */
  GStringChunk* chunk; /**< A chunk of memory in which the contents of orig_lines is stored */
  StfParseOptions_t* options; /**< Options controlling how file_str should be parsed */
  GArray* column_types; /**< Array of values from the GncCsvColumnType enumeration */
  GList* error_lines; /**< List of row numbers in orig_lines that have errors */
  GList* transactions; /**< List of GncCsvTransLine*s created using orig_lines and column_types */
  int date_format; /**< The format of the text in the date columns from date_format_internal. */
} GncCsvParseData;

GncCsvParseData* gnc_csv_new_parse_data(void);

void gnc_csv_parse_data_free(GncCsvParseData* parse_data);

int gnc_csv_load_file(GncCsvParseData* parse_data, const char* filename,
                      GError** error);

int gnc_csv_convert_encoding(GncCsvParseData* parse_data, const char* encoding, GError** error);

int gnc_csv_parse(GncCsvParseData* parse_data, gboolean guessColTypes, GError** error);

int gnc_parse_to_trans(GncCsvParseData* parse_data, Account* account, gboolean redo_errors);

#endif