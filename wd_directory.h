//-----------------------------------------------------------------------------
// wd_directory.h
//
// connect inotify watch descriptor (wd) to the directory it is watching
//-----------------------------------------------------------------------------
#if !defined(__WD_DIRECTORY_H)
#define __WD_DIRECTORY_H

#define NULL_WD 0

// a list of wds 
struct WD_LIST_NODE {
   struct WD_LIST_NODE * next_p;
   int                  wd;
};

typedef struct WD_LIST_NODE * WD_LIST_NODE_P;

// initialize the module
// returns 0 on success
int wd_directory_initialize(void);

// finalize the module at shutdown
void wd_directory_close(void);

// add a watch decriptor <-> directory connection
// parent_wd is the wd watching the parent direcory 
// top level directories have NULL_WD
// return 0 for success, nonzero for failure
int add_wd_directory(int wd, int parent_wd, const char * path_p);

// find the directory associated with a watch descriptor
// returns 0 for no directory, nonzero if we already have
// a directory for this watch descriptor
int wd_directory_exists(int wd);

// find the directory associated with a watch descriptor
// returns NULL for failure, dest_p for success
// The caller is responsible for allocating memory for the string,
// this module will strncpy up to max_len characters to dest_p
const char * find_wd_directory(int wd, char * dest_p, size_t max_len);

// the watch descriptor associated with a direcory
// returns NULL_WD if there is None
// The caller owns the string memory
int find_directory_wd(const char * path_p);

// find the wd of the parent directory
// This function will return NULL_WD if the wd does not exist or if the
// directory is top level: i.e. we are not watching its parent
int find_wd_parent(int wd);

// remove a watch descriptor <-> directory connection
// return 0 for succes, nonzero for failure 
int remove_wd_directory(int wd);

// remove a watch descriptor <-> directory connection
// and all its children.
// Return NULL on failure
// On success return a pointer to a list of the wd's that were removed
// these should be removed with inotify_rm_watch.
// The caller owns this list and should call release_wd_list when when
// done wiht it.
WD_LIST_NODE_P prune_wd_directory(int wd);

// clear a wd list, given a pointer to the head of the list
void release_wd_list(WD_LIST_NODE_P head_p);

#endif // !defined(__WD_DIRECTORY_H)
