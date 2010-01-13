//-----------------------------------------------------------------------------
// iterate_inotify_events.c
//
// read through the events returned by inotify
//
//-----------------------------------------------------------------------------
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <syslog.h>
#include <unistd.h>

#include "iterate_inotify_events.h"
#include "error_text.h"

#define INOTIFY_EVENT_SIZE  (sizeof (struct inotify_event))
#define INOTIFY_EVENT_BUFFER_LEN 64 * 1024

static int error; // holder for errno
static char inotify_event_buffer[INOTIFY_EVENT_BUFFER_LEN];
static int start_unused_buffer;
static int event_start_index;
static int event_size;

//-----------------------------------------------------------------------------
const struct inotify_event * start_iter_inotify(int inotify_fd) {
//-----------------------------------------------------------------------------
   int bytes_read;
   struct inotify_event * event_p;

   start_unused_buffer = 0;
   event_start_index = 0;

   bytes_read = read(
      inotify_fd, 
      &inotify_event_buffer[start_unused_buffer], 
      INOTIFY_EVENT_BUFFER_LEN - start_unused_buffer
   );

   if (-1 == bytes_read) {
      error = errno;
      syslog(LOG_ERR, "read(inotify_fd %d %s", error, strerror(error));
      error_file = fopen(error_path, "w");
      fprintf(error_file, "read(inotify_fd %d %s\n", error, strerror(error));
      fclose(error_file);
      exit(-1);
   }

   start_unused_buffer += bytes_read;

   // if we didn't event get a full event, just give up   
   if (start_unused_buffer < INOTIFY_EVENT_SIZE) {
      start_unused_buffer = 0;
      syslog(LOG_WARNING, "short read (1) on inotify");
      return NULL;
   }

   event_p = (struct inotify_event *) &inotify_event_buffer[event_start_index];

   // if we didn't event get a full event, just give up   
   if (start_unused_buffer < (INOTIFY_EVENT_SIZE + event_p->len)) {
      start_unused_buffer = 0;
      syslog(LOG_WARNING, "short read (2) on inotify");
      return NULL;
   }

   event_size = INOTIFY_EVENT_SIZE + event_p->len;

   return event_p;

} // start_iter_inotify

//-----------------------------------------------------------------------------
const struct inotify_event * next_iter_inotify(int inotify_fd) {
//-----------------------------------------------------------------------------
   int next_event_start_index;
   int next_event_size;
   struct inotify_event * event_p;

   next_event_start_index = event_start_index + event_size;

   // 2009-03-15 dougfort -- it looks like inotify only hands off
   // complete events. No need to worry about a residue in the buffer.

   if (next_event_start_index == start_unused_buffer) { 
      return NULL;
   }      

   // 2009-03-16 dougfort -- an event wiht a zero length string is legal
   if (((next_event_start_index+INOTIFY_EVENT_SIZE) > start_unused_buffer)) { 
      syslog(
         LOG_ERR, 
         "invalid event structure (1) %d %lu %d",
         next_event_start_index,
         next_event_start_index+INOTIFY_EVENT_SIZE,
         start_unused_buffer
      );
      error_file = fopen(error_path, "w");
      fprintf(
         error_file, 
         "invalid event structure (1) %d %lu %d\n",
         next_event_start_index,
         next_event_start_index+INOTIFY_EVENT_SIZE,
         start_unused_buffer
      );
      fclose(error_file);
      exit(-1);   
   }

   event_p = 
      (struct inotify_event *) &inotify_event_buffer[next_event_start_index];
   
   next_event_size = INOTIFY_EVENT_SIZE + event_p->len;

   if ((next_event_start_index+next_event_size) > start_unused_buffer) { 
      syslog(
         LOG_ERR, 
         "invalid event structure (2) %d %d %d %d",
         next_event_start_index,
         next_event_size,
         next_event_start_index+next_event_size,
         start_unused_buffer
      );
      error_file = fopen(error_path, "w");
      fprintf(
         error_file, 
         "invalid event structure (2) %d %d %d %d",
         next_event_start_index,
         next_event_size,
         next_event_start_index+next_event_size,
         start_unused_buffer
      );
      fclose(error_file);
      exit(-1);   
   }

   // if we get here, we should have a full event in the buffer
   event_start_index = next_event_start_index;
   event_size = next_event_size;

   return event_p;

} // next_iter_inotify

