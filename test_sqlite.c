//----------------------------------------------------------------------------
// test_sqlite.c
//----------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

static const char * schema =
"CREATE TABLE wd_directory ("
"   wd          INTEGER PRIMARY KEY,"
"   parent_wd   INTEGER,"
"   path        TEXT"
");"
;

static const char * add_entry =
"INSERT INTO wd_directory VALUES(%i, %i, %Q);";

static const char * fetch_parent = 
"SELECT parent_wd from wd_directory where wd = %i;";

static sqlite3 * sqlite3_p = NULL;

//----------------------------------------------------------------------------
static void execute_sql(const char * sql_p, ...) {
//----------------------------------------------------------------------------
   va_list ap;
   char * statement_p;
   char * error_message_p;
   int result;

   va_start(ap, sql_p);
   statement_p = sqlite3_vmprintf(sql_p, ap);
   va_end(ap);

   error_message_p = NULL;
   result = sqlite3_exec(sqlite3_p, statement_p, NULL, NULL, &error_message_p); 
   if (result != SQLITE_OK) {
      fprintf(stderr, "%s %s\n", statement_p, sqlite3_errmsg(sqlite3_p));
      if (error_message_p != NULL) {
         fprintf(stderr, "SQL error %s\n", error_message_p);
         sqlite3_free(error_message_p);
         error_message_p = NULL;
      }
      exit(-1);
   }

   sqlite3_free(statement_p);

} // execute_sql

//----------------------------------------------------------------------------
int main(int argc, char **argv) {
//----------------------------------------------------------------------------
   int result;
   char * sql_p;
   sqlite3_stmt * statement_p;
   const char * tail_p; 
   int parent_wd;
   
   result = sqlite3_open(":memory:", &sqlite3_p);
   if (result != 0) {
      fprintf(stderr, "sqlite3_open %s\n", sqlite3_errmsg(sqlite3_p));
      sqlite3_close(sqlite3_p);
      exit(-1);
   }

   execute_sql(schema); 

   execute_sql(add_entry, 1, 42, "foop");

   sql_p = sqlite3_mprintf(fetch_parent, 1);
   result = sqlite3_prepare(
      sqlite3_p, 
      sql_p, 
      strlen(sql_p), 
      &statement_p,
      &tail_p
   );    
   sqlite3_free(sql_p);

   if (result != SQLITE_OK) {
      fprintf(stderr, "sqlite3_prepare %s\n", sqlite3_errmsg(sqlite3_p));
      sqlite3_close(sqlite3_p);
      exit(-1);
   }

   result = sqlite3_step(statement_p);
   if (result != SQLITE_ROW) {
      fprintf(stderr, "sqlite3_step %s\n", sqlite3_errmsg(sqlite3_p));
      sqlite3_close(sqlite3_p);
      exit(-1);
   }

   parent_wd =  sqlite3_column_int(statement_p, 0);

   sqlite3_finalize(statement_p);

   fprintf(stdout, "parent wd = %d\n", parent_wd);

   sqlite3_close(sqlite3_p);

   return 0;
} // main
