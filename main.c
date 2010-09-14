//-----------------------------------------------------------------------------
//spideroak_inotify_dir_watcher
//
//main.c
//
//-----------------------------------------------------------------------------
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/stat.h>


#include "iterate_inotify_events.h"
#include "list_sub_dirs.h"
#include "wd_directory.h"

#if defined(DEBUG)
   #define LOG_MASK_PRIORITY LOG_DEBUG
#else
   #define LOG_MASK_PRIORITY LOG_NOTICE
#endif

#define POLL_TIMEOUT 1
#define MAX_POLL_FDS 1
#define MAX_EXCLUDES 64
#define MAX_PATH_LEN 4096

char error_path[MAX_PATH_LEN+1];
FILE * error_file = NULL;

static int alive = 1;
static int error; // holder for errno
static int inotify_fd = -1;
static char temp_path_buffer[MAX_PATH_LEN];
static int notification_count = 0;
static uint32_t watch_mask =
      IN_CLOSE_WRITE 
    | IN_CREATE 
    | IN_DELETE 
    | IN_MOVED_FROM 
    | IN_MOVED_TO 
    | IN_DELETE_SELF 
    | IN_MOVE_SELF;

static uint32_t create_dir_mask = IN_CREATE | IN_ISDIR;

struct EVENT_NAME_LOOKUP_ENTRY {
   uint32_t       event_id;
   const char  *  event_name; 
};

static struct EVENT_NAME_LOOKUP_ENTRY event_name_lookup[] = {
   {IN_CLOSE_WRITE,  "IN_CLOSE_WRITE"},
   {IN_CREATE,       "IN_CREATE"},
   {IN_DELETE,       "IN_DELETE "},
   {IN_MOVED_FROM,   "IN_MOVED_FROM"},
   {IN_MOVED_TO,     "IN_MOVED_TO"},
   {IN_DELETE_SELF,  "IN_DELETE_SELF"},
   {IN_MOVE_SELF,    "IN_MOVE_SELF"},
   {0,               "*unknown*"}
}; // event_name_lookup

struct EXCLUDE_ENTRY {
   const char  * path_p;
   size_t        path_len;
}; 
static struct EXCLUDE_ENTRY excludes[MAX_EXCLUDES];
static int exclude_count = 0;
static uint32_t prev_cookie = 0;
static char parent_path_buffer[MAX_PATH_LEN+1];

//-----------------------------------------------------------------------------
static void sigterm_handler(int signal_num) {
//-----------------------------------------------------------------------------
   if (SIGTERM == signal_num) {
      alive = 0;
   }
} // sigterm_handler

//-----------------------------------------------------------------------------
static const char * event_name(uint32_t event_mask) {
//-----------------------------------------------------------------------------
   int i;

   for (i=0; 1; i++) {
      if (0 == event_name_lookup[i].event_id) {
         return event_name_lookup[i].event_name;
      }
      if (event_name_lookup[i].event_id & event_mask) {
         return event_name_lookup[i].event_name;
      } 
   } // for

} // event_name

//-----------------------------------------------------------------------------
static void initialize_error_path(const char * notify_dir_p) {
//-----------------------------------------------------------------------------
   int bytes_written;

   bytes_written = snprintf(
      error_path, 
      sizeof error_path,
      "%s/error.txt",
      notify_dir_p
   );
   if (sizeof error_path == bytes_written) {
      syslog(LOG_ERR, "error path overflow %s", notify_dir_p);
      exit(1);
   }

} // initialize_temp_path

//-----------------------------------------------------------------------------
static void initialize_temp_path(const char * notify_dir_p) {
//-----------------------------------------------------------------------------
   int bytes_written;

   bytes_written = snprintf(
      temp_path_buffer, 
      sizeof temp_path_buffer,
      "%s/temp",
      notify_dir_p
   );
   if (sizeof temp_path_buffer == bytes_written) {
      syslog(LOG_ERR, "temp path overflow %s", notify_dir_p);
      error_file = fopen(error_path, "w");
      fprintf(error_file, "temp path overflow %s\n", notify_dir_p);
      fclose(error_file);
      exit(1);
   }

} // initialize_temp_path

//-----------------------------------------------------------------------------
static void remove_pruned_wds(WD_LIST_NODE_P wd_list_p) {
//-----------------------------------------------------------------------------
   WD_LIST_NODE_P node_p;

   for (node_p=wd_list_p; node_p != NULL; node_p=node_p->next_p) {
      if (-1 == inotify_rm_watch(inotify_fd, node_p->wd)) {
         error = errno;
         if (EINVAL == error) {
            // this one is already gone
            continue;
         }
         syslog(
            LOG_ERR, 
            "inotify_rm_watch failed %d (%d) %s",
            node_p->wd, 
            error, 
            strerror(error)
         );
         error_file = fopen(error_path, "w");
         fprintf(
            error_file, 
            "inotify_rm_watch failed %d (%d) %s\n",
            node_p->wd, 
            error, 
            strerror(error)
         );
         fclose(error_file);
         exit(15);
      } 
   }

} // remove_pruned_wds

//-----------------------------------------------------------------------------
static void prune_wd_and_clean_up(int wd) {
//-----------------------------------------------------------------------------
   WD_LIST_NODE_P wd_list_p;

   // We prune the whole tree (if any) below this directory, because
   // the paths are no longer right. We assume that we will build new 
   // entries when we get IN_MOVED_TO
   wd_list_p = prune_wd_directory(wd);
   remove_pruned_wds(wd_list_p);
   release_wd_list(wd_list_p);

} // prune_wd_and_clean_up

//-----------------------------------------------------------------------------
static int add_watch(int parent_wd, const char * path) {
//-----------------------------------------------------------------------------
   int i;
   int watch_descriptor;
   SUB_DIR_NODE_P head_p;
   SUB_DIR_NODE_P node_p;
   char path_buffer[MAX_PATH_LEN];
   int chars_stored;

   for (i=0; i < exclude_count; i++) {
      if (0 == strncmp(path, excludes[i].path_p, excludes[i].path_len)) {
         syslog(
            LOG_NOTICE, 
            "excluding path %s (%s)",
            path,
            excludes[i].path_p
         );
         return -1; 
      }
   }

   watch_descriptor = find_directory_wd(path);
   if (watch_descriptor != NULL_WD) {
      syslog(LOG_NOTICE, "Already watching %s wd=%d", path, watch_descriptor);
      return -1;
   }

   syslog(LOG_DEBUG, "watching %s", path);
   watch_descriptor = inotify_add_watch(inotify_fd, path, watch_mask);
   if (-1 == watch_descriptor) {
      error = errno;

      // latency: the directory may no longer be there by the time we get
      // around to adding this watch. So we ignore errno 2, and hope
      // we pick him up in another event, like move
      if (ENOENT == error) {
         syslog(
            LOG_NOTICE, 
            "inotify_add_watch ignoring missing directory %s", 
            path 
         );
         return -1;
      }

      if (EACCES == error) {
         syslog(
            LOG_NOTICE, 
            "inotify_add_watch ignoring directory (access denied) %s", 
            path 
         );
         return -1;
      }

      syslog(
         LOG_ERR, 
         "inotify_add_watch %s %d %s", 
         path, 
         error, 
         strerror(error)
      );
      error_file = fopen(error_path, "w");
      fprintf(
         error_file, 
         "inotify_add_watch %s %d %s\n", 
         path, 
         error, 
         strerror(error)
      );
      fclose(error_file);
      exit(2);
   }

   // 2020-07-06 dougfort -- In some cases, such as a top level directory 
   // being moved, we may already have a watch on the old directory.
   // In this case, inotify_add_watch returns the existing wd, which we
   // want to get rid of.
   if (wd_directory_exists(watch_descriptor)) {
      syslog(
         LOG_WARNING, "wd exists for new watch, pruning it %d, %s", 
         watch_descriptor,
         path
      );
      prune_wd_and_clean_up(watch_descriptor);
   }

   if (0 != add_wd_directory(watch_descriptor, parent_wd, path)) {
      syslog(
         LOG_ERR, 
         "Unable to add wd_directory %d %s",
         watch_descriptor,
         path
      );
      error_file = fopen(error_path, "w");
      fprintf(
         error_file, 
         "Unable to add wd_directory %d %s\n",
         watch_descriptor,
         path
      );
      fclose(error_file);
      exit(3);
   }

   // now recursively add a watch for every directory below this path
   // we're counting on the filesystem to limit the depth
   head_p = list_sub_dirs(path);
   for (node_p = head_p; node_p != NULL; node_p = node_p->next_p) {
      chars_stored = snprintf(
         path_buffer, 
         sizeof path_buffer,
         "%s/%s",
         path,
         node_p->d_name
      );
      if (chars_stored >= sizeof path_buffer) {
         syslog(LOG_ERR, "path buffer overlow %s", path_buffer);
         error_file = fopen(error_path, "w");
         fprintf(error_file, "path buffer overlow %s\n", path_buffer);
         fclose(error_file);
         exit(4);
      }
      add_watch(watch_descriptor, path_buffer);
   } // for

   release_sub_dir_list(head_p);

   return 0; //success

} // add_watch

//-----------------------------------------------------------------------------
static void load_excludes(const char *exclude_path) {
//-----------------------------------------------------------------------------
   FILE * exclude_file_p;
   char read_buffer[MAX_PATH_LEN];
   char * char_p;

   exclude_file_p = fopen(exclude_path, "r");
   if (NULL == exclude_file_p) {
      error = errno;
      syslog(LOG_ERR, "fopen %s %d %s", exclude_path, error, strerror(error));
      error_file = fopen(error_path, "w");
      fprintf(
         error_file, "fopen %s %d %s\n", exclude_path, error, strerror(error)
      );
      fclose(error_file);
      exit(5);
   }

   while (1) {
      fgets(read_buffer, MAX_PATH_LEN, exclude_file_p);

      if (ferror(exclude_file_p)) {
         error = errno;
         syslog(
            LOG_ERR, "fgets %s %d %s", exclude_path, error, strerror(error)
         );
         error_file = fopen(error_path, "w");
         fprintf(
            error_file, "fgets %s %d %s\n", exclude_path, error, strerror(error)
         );
         fclose(error_file);
         exit(6);
      }

      if (feof(exclude_file_p)) {
         break;
      }

      if (exclude_count >= MAX_EXCLUDES) {
         syslog(LOG_ERR, "Too many excludes");
         error_file = fopen(error_path, "w");
         fprintf(error_file, "Too many excludes\n");
         fclose(error_file);
         exit(7);
      }

      char_p = strchr(read_buffer, '\n');
      if (char_p != NULL) {
         *char_p = '\0';
      }

      syslog(LOG_INFO, "exclude path: '%s'", read_buffer);

      excludes[exclude_count].path_len = strlen(read_buffer);
      excludes[exclude_count].path_p = calloc(
         excludes[exclude_count].path_len+1, sizeof(char)
      );

      if (NULL == excludes[exclude_count].path_p) {
         syslog(LOG_ERR, "calloc failed");
         error_file = fopen(error_path, "w");
         fprintf(error_file, "calloc failed\n");
         fclose(error_file);
         exit(8);
      }

      strcpy((char *) excludes[exclude_count].path_p, read_buffer);
      exclude_count++;

   } // while
   
   fclose(exclude_file_p);   

} // load_excludes

//-----------------------------------------------------------------------------
static void load_top_level_paths(const char *config_path) {
//-----------------------------------------------------------------------------
   FILE * config_file_p;
   char read_buffer[MAX_PATH_LEN];
   char * char_p;

   config_file_p = fopen(config_path, "r");
   if (NULL == config_file_p) {
      error = errno;
      syslog(LOG_ERR, "fopen %s %d %s", config_path, error, strerror(error));
      error_file = fopen(error_path, "w");
      fprintf(
         error_file, "fopen %s %d %s\n", config_path, error, strerror(error)
      );
      fclose(error_file);
      exit(9);
   }

   while (1) {
      fgets(read_buffer, MAX_PATH_LEN, config_file_p);

      if (ferror(config_file_p)) {
         error = errno;
         syslog(LOG_ERR, "fgets %s %d %s", config_path, error, strerror(error));
         error_file = fopen(error_path, "w");
         fprintf(
            error_file, "fgets %s %d %s\n", config_path, error, strerror(error)
         );
         fclose(error_file);
         exit(10);
      }

      if (feof(config_file_p)) {
         break;
      }

      char_p = strchr(read_buffer, '\n');
      if (char_p != NULL) {
         *char_p = '\0';
      }

      syslog(LOG_INFO, "top level path: '%s'", read_buffer);

      if (add_watch(NULL_WD, read_buffer) != 0) {
         syslog(LOG_WARNING, "Can't watch toplevel path %s", read_buffer);
      }

   } // while
   
   fclose(config_file_p);   

} // load_top_level_paths

//-----------------------------------------------------------------------------
static FILE * open_temp_file(void) {
//-----------------------------------------------------------------------------
   FILE * temp_file_p;

   temp_file_p = fopen(temp_path_buffer, "w");
   if (NULL == temp_file_p) {
      error = errno;
      syslog(
         LOG_ERR, 
         "open(temp_file %s %d %s", 
         temp_path_buffer, 
         error, 
         strerror(error)
      );
      error_file = fopen(error_path, "w");
      fprintf(
         error_file, 
         "open(temp_file %s %d %s\n", 
         temp_path_buffer, 
         error, 
         strerror(error)
      );
      fclose(error_file);
      exit(11);
   }

   return temp_file_p;

} // open_temp_file

//-----------------------------------------------------------------------------
static void rename_temp_file(const char * notify_dir_p) {
//-----------------------------------------------------------------------------
   char notification_path_buffer[MAX_PATH_LEN];
   int bytes_written;

   notification_count++;
   bytes_written = snprintf(
      notification_path_buffer, 
      sizeof notification_path_buffer,
      "%s/%08d.txt",
      notify_dir_p,
      notification_count
   );
   if (sizeof notification_path_buffer == bytes_written) {
      syslog(
         LOG_ERR, 
         "notification path overflow %s", 
         notification_path_buffer
      );
      error_file = fopen(error_path, "w");
      fprintf(
         error_file, 
         "notification path overflow %s\n", 
         notification_path_buffer
      );
      fclose(error_file);
      exit(12);
   }

   if (-1 == rename(temp_path_buffer, notification_path_buffer)) {
      error = errno;
      syslog(
         LOG_ERR, 
         "rename(temp_file %s %s %d %s", 
         temp_path_buffer, 
         notification_path_buffer,
         error, 
         strerror(error)
      );
      error_file = fopen(error_path, "w");
      fprintf(
         error_file, 
         "rename(temp_file %s %s %d %s\n", 
         temp_path_buffer, 
         notification_path_buffer,
         error, 
         strerror(error)
      );
      fclose(error_file);
      exit(13);
   }

} // rename_temp_file

//-----------------------------------------------------------------------------
static int watch_new_directory(
   int parent_wd, 
   const char * parent, 
   const char * name
) {
//-----------------------------------------------------------------------------
   char new_dir_path_buffer[MAX_PATH_LEN];
   int bytes_written;

   bytes_written = snprintf(
      new_dir_path_buffer, 
      sizeof new_dir_path_buffer,
      "%s/%s",
      parent,
      name
   );

   if (sizeof new_dir_path_buffer == bytes_written) {
      syslog(
         LOG_ERR, 
         "new dir path overflow %s", 
         new_dir_path_buffer
      );
      error_file = fopen(error_path, "w");
      fprintf(
         error_file, 
         "new dir path overflow %s\n", 
         new_dir_path_buffer
      );
      fclose(error_file);
      exit(14);
   }

   return add_watch(parent_wd, new_dir_path_buffer);

} // watch_new_directory

//-----------------------------------------------------------------------------
static void prune_moved_directory(
   const char * parent_dir_p, 
   const char * dir_name_p
) {
//-----------------------------------------------------------------------------
   char path_buffer[MAX_PATH_LEN];
   int chars_stored;
   int moved_dir_wd;

   chars_stored = snprintf(
      path_buffer, 
      sizeof path_buffer,
      "%s/%s",
      parent_dir_p,
      dir_name_p
   );
   if (chars_stored >= sizeof path_buffer) {
      syslog(LOG_ERR, "path buffer overlow %s", path_buffer);
      error_file = fopen(error_path, "w");
      fprintf(error_file, "path buffer overlow %s\n", path_buffer);
      fclose(error_file);
      exit(4);
   }

   moved_dir_wd = find_directory_wd(path_buffer);

   // 2010-09-14 dougfort -- don't treat not finding the wd as an error
   // We assume that the directory was created and then renamed before
   // we had a chance to create watch descriptors.
   if (NULL_WD != moved_dir_wd) {
      prune_wd_and_clean_up(moved_dir_wd);
   }

} // prune_moved_directory

//-----------------------------------------------------------------------------
static void process_inotify_events(const char * notify_dir_p) {
//-----------------------------------------------------------------------------
   const struct inotify_event * event_p;
   const char * parent_dir_p;
   FILE * temp_file_p;
   int prev_wd;

   parent_dir_p = NULL;
   temp_file_p = NULL;
   prev_wd = NULL_WD;
   for (
      event_p=start_iter_inotify(inotify_fd); 
      event_p != NULL; 
      event_p=next_iter_inotify(inotify_fd)
   ) {
      
      // slightly memoize the path lookup
      if (event_p->wd != prev_wd) {
        memset(parent_path_buffer, '\0', sizeof parent_path_buffer);
        parent_dir_p = find_wd_directory(
           event_p->wd,
           parent_path_buffer,
           MAX_PATH_LEN
        );
        prev_wd = event_p->wd;
      }

      syslog(
         LOG_DEBUG, 
         "%05d event 0x%08X %s %d at %s parent %s",
         event_p->wd, 
         event_p->mask,
         event_name(event_p->mask),
         event_p->cookie,
         event_p->len > 0 ? event_p->name : "*noname*", 
         NULL == parent_dir_p ? "*none*" : parent_dir_p
      );

      if (event_p->mask & IN_Q_OVERFLOW) {
         syslog(LOG_ERR, "Inotify queue overflow");
         error_file = fopen(error_path, "w");
         fprintf(error_file, "Inotify queue overflow\n");
         fclose(error_file);

         // we abort because this means we have lost some events,
         // we need to force Monitor to make a ful pass
         exit(16);

      } else if (create_dir_mask == (event_p->mask & create_dir_mask)) {

         // due to latency, we may not be able to watch this directory; 
         // for example it may have moved by the time we get this event
         if (
            watch_new_directory(event_p->wd, parent_dir_p, event_p->name) != 0
         ) {
            continue;
         } 

      } else if (event_p->mask & IN_DELETE_SELF) {

         // This should be picked up by its parent. 
         // If they delete the whole top level directory,
         // we will miss it.
         continue;

      } else if (event_p->mask & IN_MOVE_SELF) {

         // this event should already be reported after IN_MOVE_FROM
         // and IN_MOVE_TO
         continue;

      } else if (event_p->mask & IN_MOVED_FROM) {
         
         // we assume that IN_MOVED_FROM always hits before IN_MOVED_TO
         // this may not be valid so we check the cookie
         if (event_p->cookie == prev_cookie) {
            syslog(LOG_ERR, "cookie %d from IN_MOVED_TO present", prev_cookie);
            error_file = fopen(error_path, "w");
            fprintf(
               error_file, "cookie %d from IN_MOVED_TO present\n", prev_cookie
            );
            fclose(error_file);
            exit(17);
         }
         prev_cookie = event_p->cookie;

         if (event_p->mask & IN_ISDIR) {
            prune_moved_directory(parent_dir_p, event_p->name);
         }

      } else if (event_p->mask & IN_MOVED_TO) {
         
         // we assume that IN_MOVED_FROM always hits before IN_MOVED_TO
         // this may not be valid so we check the cookie
         // 2009-03-25 dougfort -- we accept the missing cookie, because
         // we may be moving in from somewhere we're not watching
         if (event_p->cookie != prev_cookie) {
            syslog(
               LOG_NOTICE, 
               "cookie %d from IN_MOVED_FROM absent %d %s %s",
               event_p->cookie, 
               prev_cookie,
               parent_dir_p,
               event_p->name
            );
         }
         prev_cookie = event_p->cookie;

         if (event_p->mask & IN_ISDIR) {
            // We treat this as an add, create a whole new watch structure.
            // We assume the old one was cleared out when we got IN_MOVED_FROM
            watch_new_directory(event_p->wd, parent_dir_p, event_p->name);
         }

      } else if (event_p->mask & IN_IGNORED) {

         // we get this event after kernel has removed a watch descriptor
         // we need to make sure we do not keep carrying it around
         syslog(LOG_DEBUG, "ignored event removing wd");
         remove_wd_directory(event_p->wd);

         continue;
      }      

      if (NULL == parent_dir_p) {
         syslog(
            LOG_ERR, 
            "unable to find parent %05d event 0x%08X %s %d at %s",
            event_p->wd, 
            event_p->mask,
            event_name(event_p->mask),
            event_p->cookie,
            event_p->len > 0 ? event_p->name : "*noname*" 
         );
         error_file = fopen(error_path, "w");
         fprintf(
            error_file, 
            "unable to find parent %05d event 0x%08X %s %d at %s",
            event_p->wd, 
            event_p->mask,
            event_name(event_p->mask),
            event_p->cookie,
            event_p->len > 0 ? event_p->name : "*noname*" 
         );
         fclose(error_file);
         exit(19);
      }

      if (NULL == temp_file_p) {
         temp_file_p = open_temp_file();
      } 

      fprintf(temp_file_p, "%s\n", parent_dir_p);
      if (ferror(temp_file_p)) {
         error = errno;
         syslog(
            LOG_ERR, 
            "fprintf(temp_file %s %d %s", 
            temp_path_buffer, 
            error, 
            strerror(error)
         );
         error_file = fopen(error_path, "w");
         fprintf(
            error_file, 
            "fprintf(temp_file %s %d %s\n", 
            temp_path_buffer, 
            error, 
            strerror(error)
         );
         fclose(error_file);
         exit(20);
      }

   } // for

   if (temp_file_p != NULL) {
      fclose(temp_file_p);
      rename_temp_file(notify_dir_p);
   }

} // process_inotify_events

//-----------------------------------------------------------------------------
// arguments:
// argv[1] - parent PID (not used)
// argv[2] - config file path
// argv[3] - exclude file path
// argv[4] - notification directory
int main(int argc, char **argv) {
//-----------------------------------------------------------------------------
   struct pollfd poll_fds[MAX_POLL_FDS];
   int poll_fd_count;
   int poll_result;
   const char * config_file_path;
   const char * exclude_file_path;
   const char * notification_path;

   umask(0077);

   openlog("spideroak_inotify", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_USER);

   setlogmask(LOG_UPTO(LOG_MASK_PRIORITY));
   syslog(LOG_NOTICE, "Program started");

   if (argc != 5) {
      syslog(LOG_ERR, "Invalid number of arguments %d, expected %d", argc, 5 );
      exit(21);      
   }
   config_file_path     = argv[2];
   exclude_file_path    = argv[3];
   notification_path     = argv[4];

   initialize_error_path(notification_path);

   if (signal(SIGTERM, sigterm_handler) == SIG_ERR) {
      error = errno;
      syslog(LOG_ERR, "signal(SIGTERM %d %s", error, strerror(error));
      error_file = fopen(error_path, "w");
      fprintf(error_file, "signal(SIGTERM %d %s\n", error, strerror(error));
      fclose(error_file);
      exit(22);
   }

   wd_directory_initialize();

   initialize_temp_path(notification_path);

   inotify_fd = inotify_init();
   if (-1 == inotify_fd) {
      error = errno;
      syslog(LOG_ERR, "inotify_init %d %s", error, strerror(error));
      error_file = fopen(error_path, "w");
      fprintf(error_file, "inotify_init %d %s\n", error, strerror(error));
      fclose(error_file);
      exit(23);
   }

   load_excludes(exclude_file_path);
   load_top_level_paths(config_file_path);

   poll_fds[0].fd = inotify_fd;
   poll_fds[0].events = POLLIN;
   poll_fd_count = 1;

   syslog(LOG_DEBUG, "start poll loop");
   while (alive) {
      poll_result = poll(poll_fds, poll_fd_count, POLL_TIMEOUT * 1000);

      switch (poll_result) {
         case -1: // error
            error = errno;
            if (EINTR == error) {
               syslog(LOG_NOTICE, "poll interrupted, assuming SIGTERM");
               alive = 0;
            } else {
               syslog(LOG_ERR, "poll %d %s", error, strerror(error));
               error_file = fopen(error_path, "w");
               fprintf(error_file, "poll %d %s\n", error, strerror(error));
               fclose(error_file);
               exit(24);
            }
            break;
         case 0: // timeout
            if (1 == getppid()) {
               syslog(LOG_NOTICE, "Parent process gone: stopping");
               alive = 0;
            }
            break;
         default:
            if (1 == getppid()) {
               syslog(LOG_NOTICE, "Parent process gone: stopping");
               alive = 0;
            } else if (poll_fds[0].revents & POLLIN) {
               process_inotify_events(notification_path);
            }
      } // switch

   } // while (alive)
   syslog(LOG_DEBUG, "end poll loop");

   close(inotify_fd);
   wd_directory_close();
   syslog(LOG_NOTICE, "Program terminates normally");
   closelog();
   return 0;

} // main
