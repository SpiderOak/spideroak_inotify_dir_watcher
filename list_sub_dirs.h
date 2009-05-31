//-----------------------------------------------------------------------------
// list_sub_dirs.h
//
// list the directories immediately under a specfified path
//-----------------------------------------------------------------------------
#if !defined(__LIST_SUB_DIRS_H)
#define __LIST_SUB_DIRS_H

#define DIR_NAME_SIZE 256

struct SUB_DIR_NODE {
   struct SUB_DIR_NODE * next_p;
   char d_name[DIR_NAME_SIZE+1];
};

typedef struct SUB_DIR_NODE * SUB_DIR_NODE_P;

// return a singly linked list of NULL terminated directory names
// return NULL if there are no subdirectories
SUB_DIR_NODE_P list_sub_dirs(const char * path);

// release resources used by a sub dir list
// does nothing if head_p is NULL (so you can release an empty list)
// after return, the address pointed to by head_p is invalid
void release_sub_dir_list(SUB_DIR_NODE_P head_p);

#endif // !defined(__LIST_SUB_DIRS_H)
