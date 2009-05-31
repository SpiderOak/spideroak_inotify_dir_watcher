//-----------------------------------------------------------------------------
// Test wd_directory.c
//-----------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "wd_directory.h"

struct TEST_ENTRY {
   int wd;
   int parent_wd;
   const char * path_p;
};

static struct TEST_ENTRY single = {42, 43, "aaa"}; 

// small tree
//                                10-aaa
//                                  |
//                   |-----------------------------------|               
//              11-aaa/bbb                         12-aaa/ccc
//                   |                                   |
//         |-------------------|                 |-----------------|
//         |                   |                 |                 |        
//  13-aaa/bbb/ddd   14-aaa/bbb/eee      15-aaa/ccc/fff      16-aaa/ccc/ggg
                     
static struct TEST_ENTRY small_tree[] = {
   {10, NULL_WD, "aaa"},
   {11, 10, "aaa/bbb"},
   {12, 10, "aaa/ccc"},
   {13, 11, "aaa/bbb/ddd"},
   {14, 11, "aaa/bbb/eee"},
   {15, 12, "aaa/ccc/fff"},
   {16, 12, "aaa/ccc/ggg"}
}; //small_tree 

#define PATH_BUFFER_LEN 4096
static char path_buffer[PATH_BUFFER_LEN+1];

//-----------------------------------------------------------------------------
void test_single_directory(void) {
//-----------------------------------------------------------------------------
   int result;
   const char * result_path;
   WD_LIST_NODE_P node_p;

   wd_directory_initialize();

   fprintf(stdout, "test single directory\n");
   result = add_wd_directory(single.wd, single.parent_wd, single.path_p);
   assert(0 == result);

   result_path = find_wd_directory(single.wd, path_buffer, PATH_BUFFER_LEN);
   assert(result_path != NULL);
   assert(strcmp(result_path, single.path_p) == 0);

   result = find_directory_wd(single.path_p);
   assert(result == single.wd);

   result = find_wd_parent(single.wd);
   assert(result == single.parent_wd);

   result = remove_wd_directory(single.wd);
   assert(0 == result);

   result = find_directory_wd(single.path_p);
   assert(result == NULL_WD);

   result_path = find_wd_directory(single.wd, path_buffer, PATH_BUFFER_LEN);
   assert(NULL == result_path);

   result = find_wd_parent(single.wd);
   assert(result == NULL_WD);
   
   result = add_wd_directory(single.wd, single.parent_wd, single.path_p);
   assert(0 == result);

   node_p = prune_wd_directory(single.wd);
   assert(node_p != NULL);
   assert(node_p->wd == single.wd);
   assert(NULL == node_p->next_p);

   release_wd_list(node_p);

   wd_directory_close();

} // test_single_directory

//-----------------------------------------------------------------------------
void test_small_tree(void) {
//-----------------------------------------------------------------------------
   int tree_size;
   int i;
   int result;
   const char * result_path;
   WD_LIST_NODE_P head_p;
   WD_LIST_NODE_P node_p;

   wd_directory_initialize();

   fprintf(stdout, "test small tree\n");
   tree_size = sizeof small_tree / sizeof(struct TEST_ENTRY);

   for (i=0; i < tree_size; i++ ) {
      result = add_wd_directory(
         small_tree[i].wd, 
         small_tree[i].parent_wd, 
         small_tree[i].path_p
      );
      assert(0 == result);
   } 

   for (i=0; i < tree_size; i++ ) {

      result_path = find_wd_directory(
         small_tree[i].wd,
         path_buffer,
         PATH_BUFFER_LEN
      );
      assert(result_path != NULL);
      assert(strcmp(result_path, small_tree[i].path_p) == 0);

      result = find_directory_wd(small_tree[i].path_p);
      assert(result == small_tree[i].wd);

      result = find_wd_parent(small_tree[i].wd);
      assert(result == small_tree[i].parent_wd);

      result = remove_wd_directory(small_tree[i].wd);
      assert(0 == result);

      result = find_directory_wd(small_tree[i].path_p);
      assert(result == NULL_WD);

      result_path = find_wd_directory(
         small_tree[i].wd,
         path_buffer,
         PATH_BUFFER_LEN
      );
      assert(NULL == result_path);

      result = find_wd_parent(small_tree[i].wd);
      assert(result == NULL_WD);
   }

   for (i=0; i < tree_size; i++ ) {
      result = add_wd_directory(
         small_tree[i].wd, 
         small_tree[i].parent_wd, 
         small_tree[i].path_p
      );
      assert(0 == result);
   } 

   head_p = node_p = prune_wd_directory(12);
   assert(node_p != NULL);
   assert(node_p->wd == 12);
   assert(node_p->next_p != NULL);

   node_p = node_p->next_p;
   assert(node_p->wd == 15);
   assert(node_p->next_p != NULL);

   node_p = node_p->next_p;
   assert(node_p->wd == 16);
   assert(node_p->next_p == NULL);

   release_wd_list(head_p);

   wd_directory_close();

} // test_small_tree

//-----------------------------------------------------------------------------
int main(int argc, char **argv) {
//-----------------------------------------------------------------------------
   fprintf(stdout, "test starts\n");

   test_single_directory();
   test_small_tree();

   fprintf(stdout, "test completes normally\n");
   return 0;
} // main
