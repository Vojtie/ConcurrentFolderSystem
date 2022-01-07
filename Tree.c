#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "Tree.h"
#include "HashMap.h"
#include "path_utils.h"
#include "err.h"

struct Tree {
    char *f_name; // folder name
    HashMap *content; // zawartość folderu
};

Tree *tree_new() {

    Tree *new = malloc(sizeof(Tree));
    if (!new)
        exit(1);

    new->f_name = malloc(2);
    if (!new->f_name)
        exit(1);

    new->f_name = "/";
    new->content = hmap_new();
    assert(new->content);
    return new;
}

void tree_free(Tree *tree) {

    assert(tree);
    free(tree->f_name);
    HashMapIterator it = hmap_iterator(tree->content);
    const char *key;
    void *value;
    while (hmap_next(tree->content, &it, &key, &value))
        tree_free(value);

    hmap_free(tree->content);
}

Tree *find_node(Tree *tree, const char *path) {

    if (path && strlen(path) == 1 && *path == '/')
        return tree;
    if (!strcmp(tree->f_name, path))
        return tree;

    HashMapIterator it = hmap_iterator(tree->content);
    const char *key;
    void *value;
    char component[MAX_FOLDER_NAME_LENGTH + 1];
    path = split_path(path, component); assert(path);

    while (hmap_next(tree->content, &it, &key, &value))
        if (!strcmp(component, key))
            return find_node((Tree *) value, path);

    return NULL;
}

char *tree_list(Tree *tree, const char *path) {

    if (!is_path_valid(path))
        return NULL;

    Tree *dest = (Tree *) find_node(tree, path);
    if (!dest)
        return NULL;

    return make_map_contents_string(dest->content);
}

int tree_create(Tree *tree, const char *path) {

    if (!is_path_valid(path))
        return EINVAL;

    char component[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_par = make_path_to_parent(path, component);
    Tree *parent = find_node(tree, path_to_par);
    free(path_to_par);
    
    if (!parent) 
        return ENOENT;
    if (hmap_get(parent->content, component)) // folder już istnieje
        return EEXIST;

    Tree *new = malloc(sizeof(Tree));
    new->f_name = malloc(strlen(component) + 1);
    if (!new->f_name)
        exit(1);

    strcpy(new->f_name, component);
    new->content = hmap_new();
    assert(hmap_insert(parent->content, new->f_name, new));
    return 0;
}

int tree_remove(Tree *tree, const char *path) {

    if (!is_path_valid(path))
        return EINVAL;
    if (path && strlen(path) == 1 && *path == '/')
        return EBUSY;

    Tree *dest = find_node(tree, path);

    if (!dest)
        return ENOENT;
    if (hmap_size(dest->content) > 0)
        return ENOTEMPTY;

    char component[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_par = make_path_to_parent(path, component);
    Tree *dest_par = find_node(tree, path_to_par);
    free(path_to_par);
    assert(hmap_remove(dest_par->content, component));
    tree_free(dest);
    return 0;
}

int tree_move(Tree *tree, const char *source, const char *target) {

    if (strlen(source) == 1 && *source == '/')
        return EBUSY;
    if (find_node(tree, target))
        return EEXIST;

    Tree *src = find_node(tree, source);
    char trg_component[MAX_FOLDER_NAME_LENGTH + 1];
    char *p_to_trg_par = make_path_to_parent(target, trg_component);

    Tree *trg_par = find_node(tree, p_to_trg_par);
    free(p_to_trg_par);

    if (!src || !trg_par)
        return ENOENT;

    char src_component[MAX_FOLDER_NAME_LENGTH + 1];
    char *p_to_src_par = make_path_to_parent(source, src_component);
    assert(p_to_src_par);
    Tree *src_par = find_node(tree, p_to_src_par);
    assert(hmap_remove(src_par->content, src_component));
    free(p_to_src_par);
    src->f_name = realloc(src->f_name, strlen(trg_component) + 1);
    if (!src->f_name)
        exit(1);

    strcpy(src->f_name, trg_component);
    assert(hmap_insert(trg_par->content, src->f_name, src));
    return 0;
}
