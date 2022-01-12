#include "HashMap.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "Tree.h"
#include <assert.h>
#include <sys/errno.h>

void print_map(HashMap* map) {
    const char* key = NULL;
    void* value = NULL;
    printf("Size=%zd\n", hmap_size(map));
    HashMapIterator it = hmap_iterator(map);
    while (hmap_next(map, &it, &key, &value)) {
        printf("Key=%s Value=%p\n", key, value);
    }
    printf("\n");
}

int main(void)
{
//    char *p1 = "/a/b/c/d/e/g/h/", *p2 = "/x/";
//    printf("%s\n", make_path_to_lca(p1, p2));
    Tree *t = tree_new();
    tree_create(t, "/a/");
    tree_create(t, "/b/");
    tree_create(t, "/a/b/");
    tree_create(t, "/a/b/c/");
    tree_create(t, "/a/b/d/");
    tree_create(t, "/b/a/");
    tree_create(t, "/b/a/d/");
    assert(strcmp(tree_list(t, "/"), "a,b") == 0);
    assert(strcmp(tree_list(t, "/a/"), "b") == 0);
    assert(strcmp(tree_list(t, "/a/b/"), "c,d") == 0);
    assert(strcmp(tree_list(t, "/b/"), "a") == 0);
    assert(strcmp(tree_list(t, "/b/a/"), "d") == 0);
    assert(tree_move(t, "/a/b/", "/b/x/") == 0);
    assert(strcmp(tree_list(t, "/"), "a,b") == 0);
    assert(strcmp(tree_list(t, "/b/"), "a,x") == 0);
    assert(strcmp(tree_list(t, "/b/a/"), "d") == 0);
    assert(strcmp(tree_list(t, "/b/x/"), "c,d") == 0);
    assert(tree_move(t, "/b/x/", "/b/c/") == 0);
    assert(strcmp(tree_list(t, "/"), "a,b") == 0);
    assert(strcmp(tree_list(t, "/b/"), "a,c") == 0);
    assert(strcmp(tree_list(t, "/b/a/"), "d") == 0);
    assert(strcmp(tree_list(t, "/b/c/"), "c,d") == 0);
    assert(tree_move(t, "/b/c/", "/b/a/d/c/") == 0);
    assert(strcmp(tree_list(t, "/b/a/d/c/"), "c,d") == 0);
    assert(tree_move(t, "/b/a/", "/b/a/") == 0);
    assert(strcmp(tree_list(t, "/b/a/d/c/"), "c,d") == 0);
    assert(tree_move(t, "/b/a/d/", "/b/a/d/c/") == -9);
    assert(tree_move(t, "/b/a/d/c/", "/b/a/x/") == 0);
    assert(strcmp(tree_list(t, "/b/a/x/"), "c,d") == 0);
    assert(tree_move(t, "/b/x/", "/b/a/x/") == ENOENT);
    assert(tree_move(t, "/b/a/", "/b/a/x/") == -9);
    assert(tree_move(t, "/", "/b/a/") == EBUSY);
    assert(tree_move(t, "/b/a/x/", "/b/y/y/") == ENOENT);
    assert(tree_create(t, "/b/a/x/") == EEXIST);
    assert(tree_create(t, "/b/y/x/") == ENOENT);
    assert(tree_create(t, "/u/y/") == ENOENT);
    assert(tree_create(t, "/b/a/x/e/") == 0);
    assert(strcmp(tree_list(t, "/b/a/x/"), "c,d,e") == 0);
    assert(strcmp(tree_list(t, "/b/a/x/c/"), "") == 0);
    assert(tree_remove(t, "/b/a/x/") == ENOTEMPTY);
    assert(tree_remove(t, "/b/a/x/d/") == 0);
    assert(strcmp(tree_list(t, "/b/a/x/"), "c,e") == 0);
    tree_free(t);
    //printf("%s\n", tree_list(t, "/a/"));
    //printf("%s\n", tree_list(t, "/b/x/"));
//    HashMap* map = hmap_new();
//    hmap_insert(map, "a", hmap_new());
//    print_map(map);
//
//    HashMap* child = (HashMap*)hmap_get(map, "a");
//    hmap_free(child);
//    hmap_remove(map, "a");
//    print_map(map);
//
//    hmap_free(map);

//    Tree *t = tree_new();
//    tree_create(t, "/a/");
//    tree_create(t, "/b/");
//    printf("%s\n", tree_list(t, "/"));
//    tree_create(t, "/a/b/");
//    tree_create(t, "/a/b/c/");
//    tree_create(t, "/a/b/d/");
//    tree_create(t, "/b/a/");
//    tree_create(t, "/b/a/d/");
//    free(tree_list(t, "/b/x/"));
//    free(tree_list(t, "/b/a/"));
//    tree_free(t);
//    return 0;
}