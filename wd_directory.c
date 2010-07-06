//-----------------------------------------------------------------------------
// wd_directory.c
//
// connect inotify watch descriptor (wd) to the directory it is watching
//-----------------------------------------------------------------------------
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <sqlite3.h>

#include "wd_directory.h"
#include "error_text.h"

static const char * database_path = "/tmp/spideroak_inotify_db";

static const char * schema =
"CREATE TABLE wd_directory ("
"   wd          INTEGER PRIMARY KEY,"
"   parent_wd   INTEGER,"
"   path        TEXT"
");"
"CREATE UNIQUE INDEX wd_directory_path ON wd_directory(path);"
"CREATE INDEX wd_directory_parent ON wd_directory(parent_wd);"
;

static const char * add_entry =
"INSERT INTO wd_directory VALUES(%i, %i, %Q);";

static const char * remove_entry =
"DELETE FROM wd_directory where wd = %i;";

static const char * fetch_wd = 
"SELECT wd from wd_directory where path = %Q;";

static const char * fetch_parent = 
"SELECT parent_wd from wd_directory where wd = %i;";

static const char * fetch_path = 
"SELECT path from wd_directory where wd = %i;";

static const char * fetch_children = 
"SELECT wd from wd_directory where parent_wd = %i;";

static sqlite3 * sqlite3_p = NULL;

//----------------------------------------------------------------------------
static void execute_sql(const char * format_p, ...) {
//----------------------------------------------------------------------------
   va_list ap;
   char * sql_p;
   char * error_message_p;
   int result;

   va_start(ap, format_p);
   sql_p = sqlite3_vmprintf(format_p, ap);
   va_end(ap);

   error_message_p = NULL;
   result = sqlite3_exec(sqlite3_p, sql_p, NULL, NULL, &error_message_p); 
   if (result != SQLITE_OK) {
      error_file = fopen(error_path, "w");
      syslog(LOG_ERR, "%s %s", sqlite3_errmsg(sqlite3_p), sql_p);
      fprintf(error_file, "%s %s\n", sqlite3_errmsg(sqlite3_p), sql_p);
      if (error_message_p != NULL) {
         syslog(LOG_ERR, "SQL error %s", error_message_p);
         fprintf(error_file, "SQL error %s\n", error_message_p);
         sqlite3_free(error_message_p);
         error_message_p = NULL;
      }
      fclose(error_file);
      exit(-1);
   }

   sqlite3_free(sql_p);

} // execute_sql

//----------------------------------------------------------------------------
static sqlite3_stmt * prepare_sql_statement(const char * format_p, ...) {
//----------------------------------------------------------------------------
   va_list ap;
   char * sql_p;
   sqlite3_stmt * statement_p;
   const char * tail_p; 
   int result;

   va_start(ap, format_p);
   sql_p = sqlite3_vmprintf(format_p, ap);
   va_end(ap);

   result = sqlite3_prepare(
      sqlite3_p, 
      sql_p, 
      strlen(sql_p), 
      &statement_p,
      &tail_p
   );    
   sqlite3_free(sql_p);

   if (result != SQLITE_OK) {
      error_file = fopen(error_path, "w");
      fprintf(
         error_file, 
         "sqlite3_prepare %s %s\n", 
         sqlite3_errmsg(sqlite3_p), 
         sql_p
      );
      fclose(error_file);
      syslog(
         LOG_ERR, 
         "sqlite3_prepare %s %s", 
         sqlite3_errmsg(sqlite3_p), 
         sql_p
      );

      sqlite3_close(sqlite3_p);
      exit(-1);
   }

   return statement_p;

} // prepare_sql_statement

//-----------------------------------------------------------------------------
int wd_directory_initialize(void) {
//-----------------------------------------------------------------------------
   int result;

   if (0 == access(database_path, F_OK)) {
      unlink(database_path);
   }

   result = sqlite3_open(database_path, &sqlite3_p);
   if (result != 0) {
      error_file = fopen(error_path, "w");
      fprintf(error_file, "sqlite3_open %s\n", sqlite3_errmsg(sqlite3_p));
      fclose(error_file);
      syslog(LOG_ERR, "sqlite3_open %s", sqlite3_errmsg(sqlite3_p));
      sqlite3_close(sqlite3_p);
      exit(-1);
   }

   execute_sql(schema); 

   return 0;
} // wd_directory_initialize

//-----------------------------------------------------------------------------
void wd_directory_close(void) {
//-----------------------------------------------------------------------------
   sqlite3_close(sqlite3_p);
} // wd_directory_close

//-----------------------------------------------------------------------------
int add_wd_directory(int wd, int parent_wd, const char * path_p) {
//-----------------------------------------------------------------------------
   execute_sql(add_entry, wd, parent_wd, path_p);

   return 0;
} // add_wd_directory

//-----------------------------------------------------------------------------
int wd_directory_exists(int wd) {
//-----------------------------------------------------------------------------
   int result;
   sqlite3_stmt * statement_p;
   int ret_val = 0;

   statement_p = prepare_sql_statement(fetch_path, wd);

   result = sqlite3_step(statement_p);
   switch (result) {
      case SQLITE_DONE:
         ret_val = 0;
         break;
      case SQLITE_ROW:
         ret_val = 1;
         break;
      default:
         error_file = fopen(error_path, "w");
         fprintf(error_file, "sqlite3_step %s\n", sqlite3_errmsg(sqlite3_p));
         fclose(error_file);
         syslog( LOG_ERR, "sqlite3_step %s", sqlite3_errmsg(sqlite3_p));
         sqlite3_close(sqlite3_p);
         exit(-1);
   } // switch

   sqlite3_finalize(statement_p);

   return ret_val;

} // wd_directory_exists

//-----------------------------------------------------------------------------
const char * find_wd_directory(int wd, char * dest_p, size_t max_len) {
//-----------------------------------------------------------------------------
   int result;
   sqlite3_stmt * statement_p;
   const unsigned char * path_column_p;
   const char * path_p;

   statement_p = prepare_sql_statement(fetch_path, wd);

   result = sqlite3_step(statement_p);
   switch (result) {
      case SQLITE_DONE:
         path_p = NULL;
         break;
      case SQLITE_ROW:
         path_column_p = sqlite3_column_text(statement_p, 0);
         strncpy(dest_p, (const char *) path_column_p, max_len);
         path_p = dest_p; 
         break;
      default:
         error_file = fopen(error_path, "w");
         fprintf(error_file, "sqlite3_step %s\n", sqlite3_errmsg(sqlite3_p));
         fclose(error_file);
         syslog( LOG_ERR, "sqlite3_step %s", sqlite3_errmsg(sqlite3_p));
         sqlite3_close(sqlite3_p);
         exit(-1);
   } // switch

   sqlite3_finalize(statement_p);

   return path_p;

} // find_wd_directory

//-----------------------------------------------------------------------------
int find_directory_wd(const char * path_p) {
//-----------------------------------------------------------------------------
   int result;
   sqlite3_stmt * statement_p;
   int result_wd;

   statement_p = prepare_sql_statement(fetch_wd, path_p);

   result = sqlite3_step(statement_p);
   switch (result) {
      case SQLITE_DONE:
         result_wd = NULL_WD;
         break;
      case SQLITE_ROW:
         result_wd = sqlite3_column_int(statement_p, 0);
         break;
      default:
         error_file = fopen(error_path, "w");
         fprintf(error_file, "sqlite3_step %s\n", sqlite3_errmsg(sqlite3_p));
         fclose(error_file);
         syslog(LOG_ERR, "sqlite3_step %s", sqlite3_errmsg(sqlite3_p));
         sqlite3_close(sqlite3_p);
         exit(-1);
   } // switch

   sqlite3_finalize(statement_p);

   return result_wd;

} // find_directory_wd

//-----------------------------------------------------------------------------
int find_wd_parent(int wd) {
//-----------------------------------------------------------------------------
   int result;
   sqlite3_stmt * statement_p;
   int result_wd;

   statement_p = prepare_sql_statement(fetch_parent, wd);

   result = sqlite3_step(statement_p);
   switch (result) {
      case SQLITE_DONE:
         result_wd = NULL_WD;
         break;
      case SQLITE_ROW:
         result_wd = sqlite3_column_int(statement_p, 0);
         break;
      default:
         error_file = fopen(error_path, "w");
         fprintf(error_file, "sqlite3_step %s\n", sqlite3_errmsg(sqlite3_p));
         fclose(error_file);
         syslog(LOG_ERR, "sqlite3_step %s", sqlite3_errmsg(sqlite3_p));
         sqlite3_close(sqlite3_p);
         exit(-1);
   } // switch

   sqlite3_finalize(statement_p);

   return result_wd;

} // find_wd_parent

//-----------------------------------------------------------------------------
int remove_wd_directory(int wd) {
//-----------------------------------------------------------------------------
   execute_sql(remove_entry, wd);

   return 0;

} // remove_wd_directory

//-----------------------------------------------------------------------------
// recursively find the wds that have the parent wd as a parent
static WD_LIST_NODE_P find_children(int wd) {
//-----------------------------------------------------------------------------
   WD_LIST_NODE_P head_p;
   WD_LIST_NODE_P node_p;
   int result;
   sqlite3_stmt * statement_p;

   statement_p = prepare_sql_statement(fetch_children, wd);

   node_p = head_p = NULL;
   while((result = sqlite3_step(statement_p)) != SQLITE_DONE) {
      switch (result) {
         case SQLITE_ROW:
            if (NULL == head_p) { 
               node_p = head_p = calloc(1, sizeof(struct WD_LIST_NODE));
            } else {
               node_p->next_p = calloc(1, sizeof(struct WD_LIST_NODE));
               node_p = node_p->next_p;
            }

            if (NULL == node_p) {
               syslog(LOG_ERR, "unable to calloc WD_LIST_NODE");
               error_file = fopen(error_path, "w");
               fprintf(error_file, "unable to calloc WD_LIST_NODE\n");
               fclose(error_file);
               exit(-1);
            }

            node_p->wd = sqlite3_column_int(statement_p, 0);
            break;
         default:
            error_file = fopen(error_path, "w");
            fprintf(error_file, "sqlite3_step %s\n", sqlite3_errmsg(sqlite3_p));
            fclose(error_file);
            syslog(LOG_ERR, "sqlite3_step %s", sqlite3_errmsg(sqlite3_p));
            sqlite3_close(sqlite3_p);
            exit(-1);
      } // switch
   } // while

   sqlite3_finalize(statement_p);

   return head_p;
  
} // find_children

//-----------------------------------------------------------------------------
WD_LIST_NODE_P prune_wd_directory(int wd) {
//-----------------------------------------------------------------------------
   WD_LIST_NODE_P head_p;
   WD_LIST_NODE_P current_p;
   WD_LIST_NODE_P tail_p;

   head_p = calloc(1, sizeof(struct WD_LIST_NODE));
   if (NULL == head_p) {
      syslog(LOG_ERR, "unable to calloc WD_LIST_NODE");
      error_file = fopen(error_path, "w");
      fprintf(error_file, "unable to calloc WD_LIST_NODE\n");
      fclose(error_file);
      exit(-1);
   } 
   head_p->wd = wd;  

   tail_p = head_p;
   for (current_p=head_p; current_p != NULL; current_p=current_p->next_p) {

      // point tail_p to the end of the list
      while (tail_p->next_p != NULL) {
         tail_p = tail_p->next_p;
      }

      // append the children of the current node to the tail
      tail_p->next_p = find_children(current_p->wd);

   } // for

   // now remove all wd_directory rows for the tree 
   for (current_p=head_p; current_p != NULL; current_p=current_p->next_p) {
      remove_wd_directory(current_p->wd);
   }

   return head_p;

} // prune_wd_directory

//-----------------------------------------------------------------------------
void release_wd_list(WD_LIST_NODE_P head_p) {
//-----------------------------------------------------------------------------
   WD_LIST_NODE_P node_p;
   WD_LIST_NODE_P next_p;

   for (node_p=head_p; node_p != NULL; node_p=next_p) {
      next_p = node_p->next_p;
      free(node_p);
   } // for

} // release_wd_list

