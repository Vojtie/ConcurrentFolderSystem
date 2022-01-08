#include "HashMap.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "sequential_big_random.h"
#include "Tree.h"

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
//    Tree *t = tree_new();
//    tree_create(t, "/a/");
//    tree_create(t, "/b/");
//    tree_create(t, "/a/b/");
//    tree_create(t, "/a/b/c/");
//    tree_create(t, "/a/b/d/");
//    tree_create(t, "/b/a/");
//    tree_create(t, "/b/a/d/");
//    printf("%s\n", tree_list(t, "/"));
//    printf("%s\n", tree_list(t, "/a/"));
//    printf("%s\n", tree_list(t, "/a/b/"));
//    printf("%s\n", tree_list(t, "/b/"));
//    printf("%s\n", tree_list(t, "/b/a/"));
//    printf("%d\n", tree_move(t, "/a/b/", "/b/x/"));
//    printf("%s\n", tree_list(t, "/"));
//    printf("%s\n", tree_list(t, "/b/"));
//    printf("%s\n", tree_list(t, "/b/a/"));
//    printf("%s\n", tree_list(t, "/b/x/"));

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
//    tree_create(t, "/a/b/");
//    tree_create(t, "/a/b/c/");
//    tree_create(t, "/a/b/d/");
//    tree_create(t, "/b/a/");
//    tree_create(t, "/b/a/d/");
//    free(tree_list(t, "/b/x/"));
//    free(tree_list(t, "/b/a/"));
//    tree_free(t);
    sequential_big_random();
    return 0;
}