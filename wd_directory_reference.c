//-----------------------------------------------------------------------------
// wd_directory.c
//
// connect inotify watch descriptor (wd) to the directory it is watching
//-----------------------------------------------------------------------------
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "wd_directory.h"
#include "error_text.h"

#define ARRAY_SIZE_INCREMENT 1000

struct WD_DIRECTORY {
   int     wd;
   int     parent_wd;
   char  * path_p;
};

static struct WD_DIRECTORY * wd_directories = NULL;
static int wd_directory_array_size = 0;
static int wd_directory_count = 0;

//-----------------------------------------------------------------------------
int add_wd_directory(int wd, int parent_wd, const char * path_p) {
//-----------------------------------------------------------------------------

   if (wd_directory_count == wd_directory_array_size) {
      wd_directory_array_size += ARRAY_SIZE_INCREMENT;
      wd_directories = realloc(
         wd_directories, 
         wd_directory_array_size * sizeof(struct WD_DIRECTORY)
      );
      if (NULL == wd_directories) {
         return -1;
      }    
   }

   wd_directories[wd_directory_count].wd = wd;
   wd_directories[wd_directory_count].parent_wd = parent_wd;
   wd_directories[wd_directory_count].path_p =
      calloc(strlen(path_p)+1, sizeof(char));
   strcpy(wd_directories[wd_directory_count].path_p, path_p);

   wd_directory_count++;

   return 0;
} // add_wd_directory

//-----------------------------------------------------------------------------
const char * find_wd_directory(int wd) {
//-----------------------------------------------------------------------------
   int i;

   if (NULL_WD == wd) {
      return NULL;
   }

   for (i=0; i < wd_directory_count; i++) {
      if (wd_directories[i].wd == wd) {
         return wd_directories[i].path_p;
      }
   }

   return NULL;

} // find_wd_directory

//-----------------------------------------------------------------------------
int find_directory_wd(const char * path_p) {
//-----------------------------------------------------------------------------
   int i;

   if (NULL == path_p) {
      return NULL_WD;
   }

   for (i=0; i < wd_directory_count; i++) {
      if (NULL_WD == wd_directories[i].wd) {
         continue;
      }
      if (0 == strcmp(wd_directories[i].path_p, path_p)) {
         return wd_directories[i].wd;
      }
   }

   return NULL_WD;

} // find_directory_wd

//-----------------------------------------------------------------------------
int find_wd_parent(int wd) {
//-----------------------------------------------------------------------------
   int i;

   if (NULL_WD == wd) {
      return NULL_WD;
   }

   for (i=0; i < wd_directory_count; i++) {
      if (wd_directories[i].wd == wd) {
         return wd_directories[i].parent_wd;
      }
   }

   return NULL_WD;

} // find_wd_parent

//-----------------------------------------------------------------------------
int remove_wd_directory(int wd) {
//-----------------------------------------------------------------------------
   int i;

   if (NULL_WD == wd) {
      return -1;
   }

   for (i=0; i < wd_directory_count; i++) {
      if (wd_directories[i].wd == wd) {
         wd_directories[i].wd = NULL_WD;
         wd_directories[i].parent_wd = NULL_WD;
         free(wd_directories[i].path_p);
         wd_directories[i].path_p = NULL;
         return 0;
      }
   }

   return -1;

} // remove_wd_directory

//-----------------------------------------------------------------------------
// recursively find the wds that have the parent wd as a parent
static WD_LIST_NODE_P find_children(int wd) {
//-----------------------------------------------------------------------------
   int i;
   WD_LIST_NODE_P head_p;
   WD_LIST_NODE_P node_p;

   head_p = NULL;
   for (i=0; i < wd_directory_count; i++) {
      if (wd_directories[i].parent_wd == wd) {
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

         node_p->wd = wd_directories[i].wd;
         node_p->next_p = find_children(node_p->wd);
      }
   }

   return head_p;
  
} // find_children

//-----------------------------------------------------------------------------
WD_LIST_NODE_P prune_wd_directory(int wd) {
//-----------------------------------------------------------------------------
   WD_LIST_NODE_P head_p;
   WD_LIST_NODE_P node_p;

   head_p = calloc(1, sizeof(struct WD_LIST_NODE));
   if (NULL == head_p) {
      syslog(LOG_ERR, "unable to calloc WD_LIST_NODE");
      error_file = fopen(error_path, "w");
      fprintf(error_file, "unable to calloc WD_LIST_NODE\n");
      fclose(error_file);
      exit(-1);
   } 
   head_p->wd = wd;
   head_p->next_p = find_children(head_p->wd);

   // this is a painfully expensive process in the current crude module
   for (node_p=head_p; node_p != NULL; node_p=node_p->next_p) {
      remove_wd_directory(node_p->wd);
   }

   return head_p;

} // prune_wd_directory

//-----------------------------------------------------------------------------
void release_wd_list(WD_LIST_NODE_P head_p) {
//-----------------------------------------------------------------------------
   WD_LIST_NODE_P node_p=head_p;
   WD_LIST_NODE_P next_p;

   while (1) {
      if (NULL == node_p) {
         break;
      }
      next_p = node_p->next_p;
      free(node_p);
      node_p = next_p;
   } // while
} // release_wd_list

