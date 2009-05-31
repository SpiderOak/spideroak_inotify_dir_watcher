//-----------------------------------------------------------------------------
// iterate_inotify_events.h
//
// read through the events returned by inotify
//
//-----------------------------------------------------------------------------
#if !defined(__ITERATE_INOTIFY_EVENTS_H__)
#define __ITERATE_INOTIFY_EVENTS_H__

// Initialize reading a set of inotify events, and return a pointer to the first event, if any
// Return NULL for no event 
const struct inotify_event * start_iter_inotify(int inotify_fd);

// Return a pointer to the next event, if any
// Return NULL for no event
const struct inotify_event * next_iter_inotify(int inotify_fd);

#endif // !defined(__ITERATE_INOTIFY_EVENTS_H__)

