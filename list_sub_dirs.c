//-----------------------------------------------------------------------------
// list_sub_dirs.c
//
// list the directories immediately under a specfified path
//-----------------------------------------------------------------------------
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <syslog.h>

#include "error_text.h"
#include "list_sub_dirs.h"

static int error; // holder for errno

//-----------------------------------------------------------------------------
SUB_DIR_NODE_P list_sub_dirs(const char * path) {
//-----------------------------------------------------------------------------
   struct dirent * dir_entry_p;
   DIR * dir_stream_p;
   SUB_DIR_NODE_P head_p;
   SUB_DIR_NODE_P node_p = NULL;

   head_p = NULL;
   dir_stream_p = opendir(path);
   if (NULL == dir_stream_p) {
      error = errno;

      // this directory might have been moved or deleted out from under us
      if (ENOENT == error) {
         syslog(LOG_NOTICE, "Ignoring missing directory %s", path);
         return NULL;
      }

      // don't abort if we stumble into something that's not a directory
      if (ENOTDIR == error) {
         syslog(LOG_NOTICE, "list_sub dirs: not a directory %s", path);
         return NULL;
      }

      syslog(
         LOG_ERR, 
         "opendir %s %d %s", 
         path, 
         error, 
         strerror(error)
      );
      error_file = fopen(error_path, "w");
      fprintf(
         error_file, 
         "opendir %s %d %s\n", 
         path, 
         error, 
         strerror(error)
      );
      fclose(error_file);
      exit(-1);
   }

   while (1) {

      dir_entry_p = readdir(dir_stream_p);
      if (NULL == dir_entry_p) {
         error = errno;
         if (error != EBADF) { // EOF
            break;
         }

         syslog(
            LOG_ERR, 
            "readdir %s %d %s", 
            path, 
            error, 
            strerror(error)
         );
         error_file = fopen(error_path, "w");
         fprintf(
            error_file, 
            "readdir %s %d %s\n", 
            path, 
            error, 
            strerror(error)
         );
         fclose(error_file);
         exit(-1);
      }

      // 2009-12-23 dougfort -- a patch supplied by SpiderOak user gnemesure:
      // "The problem is the assumption that the directory types returned by 
      // readdir  are single bits, which is not the case. In fact DT_DIR has 
      // value of 4, and  DT_SOCKET has a value of 12, so the type must be 
      // tested for equality, not  masked".  
      if (dir_entry_p->d_type == DT_DIR) {
         
         if (0 == strcmp(dir_entry_p->d_name, ".")) {
            continue;
         }

         if (0 == strcmp(dir_entry_p->d_name, "..")) {
            continue;
         }

         if (NULL == head_p) {
            head_p = calloc(1, sizeof(struct SUB_DIR_NODE));
            node_p = head_p;
         } else {
            node_p->next_p = calloc(1, sizeof(struct SUB_DIR_NODE));
            node_p = node_p->next_p;
         }

         if (NULL == node_p) {
            syslog(LOG_ERR, "node_p is NULL");
            error_file = fopen(error_path, "w");
            fprintf(error_file, "node_p is NULL\n");
            fclose(error_file);
            exit(-1);
         }

         strncpy(node_p->d_name, dir_entry_p->d_name, DIR_NAME_SIZE); 

      }
   }

   closedir(dir_stream_p);

   return head_p;
} // list_sub_dirs

//-----------------------------------------------------------------------------
void release_sub_dir_list(SUB_DIR_NODE_P head_p) {
//-----------------------------------------------------------------------------
   SUB_DIR_NODE_P node_p;
   SUB_DIR_NODE_P next_p;

   for (node_p=head_p; node_p != NULL; node_p=next_p) {
      next_p = node_p->next_p;
      free(node_p);
   } // while
} // release_sub_dir_list

