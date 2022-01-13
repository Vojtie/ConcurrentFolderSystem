#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include "Tree.h"
#include "HashMap.h"
#include "path_utils.h"
#include "err.h"
#include <stdio.h>
struct Tree {
    //char *f_name; // folder name
    HashMap *content; // zawartość folderu
    pthread_mutex_t lock;
    pthread_cond_t readers;
    pthread_cond_t writers;
    int rcount, wcount, rwait, wwait;
    int change;
    int threads_below;
    Tree *parent;
};

static int update_threads_below(Tree *tree);
static int reader_pp(Tree *tree);
static int reader_fp(Tree *tree);
static int writer_pp(Tree *tree);
static int writer_fp(Tree *tree);
static Tree *find_child(Tree *tree, const char *path);
static Tree *find_node_p(Tree *tree, int *res, const char *path, bool is_reader);
static Tree *find_node(Tree *tree, const char *path);
static Tree *find_and_lock_dest_h(Tree *tree, const char *dest, bool lock_first, Tree *bound);
static void update_threads_below_bounded(Tree *tree, Tree *bound);

Tree *tree_new() {

    Tree *new = malloc(sizeof(Tree));
    if (!new)
        exit(1);

//    new->f_name = malloc(2);
//    if (!new->f_name)
//        exit(1);
//
//    strcpy(new->f_name, "/");
    new->content = hmap_new();
    assert(new->content);
    int err;
    if ((err = pthread_mutex_init(&new->lock, NULL)) != 0)
        syserr("lock init failed");
    if ((err = pthread_cond_init(&new->readers, NULL)) != 0)
        syserr ("cond init failed");
    if ((err = pthread_cond_init(&new->writers, NULL)) != 0)
        syserr ("cond init failed");

    new->rcount = new->rwait = new->wcount = new->wwait = 0;
    new->change = 0;
    new->threads_below = 0;
    new->parent = NULL;
    return new;
}

void tree_free(Tree *tree) {

    assert(tree);
    int err;
    if ((err = pthread_cond_destroy(&tree->readers)) != 0)
        syserr ("cond destroy 1 failed");
    if ((err = pthread_cond_destroy(&tree->writers)) != 0)
        syserr ("cond destroy 2 failed");
    if ((err = pthread_mutex_destroy(&tree->lock)) != 0)
        syserr ("lock destroy failed");

    HashMapIterator it = hmap_iterator(tree->content);
    const char *key;
    void *value;
    while (hmap_next(tree->content, &it, &key, &value))
        tree_free(value);

    hmap_free(tree->content);
    //free(tree->f_name);
    free(tree);
}

static int reader_pp(Tree *tree) {

    int err;
    if ((err = pthread_mutex_lock(&tree->lock)) != 0)
        return err;
    if (tree->wcount > 0 || (tree->change == 1 && (tree->rcount > 0 || tree->wwait > 0))) {
        tree->rwait++;
        do {
            if ((err = pthread_cond_wait(&tree->readers, &tree->lock)) != 0)
                return err;
        } while (tree->wcount > 0 || (tree->rcount > 0 && tree->change == 1)
                 || (tree->wwait > 0 && tree->change == 1));
        tree->rwait--;
    } // while (tree->wcount > 0 || (tree->rcount > 0 && tree->change == 1))
    tree->rcount++;
    // obudzic nas moze tylko pisarz
    if (tree->rwait > 0) {
        tree->change = 0;
        if ((err = pthread_cond_signal(&tree->readers)) != 0)
            return err;
    } else if (tree->wwait > 0)
        // nie ma kogo budzic a czeka pisarz wiec zamykamy drzwi
        tree->change = 1;
    tree->threads_below++;
    if ((err = pthread_mutex_unlock(&tree->lock)) != 0)
        return err;

    return 0;
}

static int reader_fp(Tree *tree) {

    int err;
    if ((err = pthread_mutex_lock(&tree->lock)) != 0)
        return err;
    tree->rcount--;
    // jesli jestesmy ostatnim czytelnikiem to budzimy pisarza (drzwi sa zamknięte)
    if (tree->rcount == 0 && (err = pthread_cond_signal(&tree->writers)) != 0)
        syserr("reader_fp_signal");//return err;

    if ((err = pthread_mutex_unlock(&tree->lock)) != 0)
        syserr("reader_fp_unlock");//return err;

    return 0;
}

static int writer_pp(Tree *tree) {

    int err;
    if ((err = pthread_mutex_lock(&tree->lock)) != 0)
        return err;
    // czekamy jesli sala jest niepusta
    if (tree->wcount + tree->rcount + tree->threads_below > 0) {
        tree->wwait++;
        do {
            if ((err = pthread_cond_wait(&tree->writers, &tree->lock)) != 0)
                return err;
        } while (tree->wcount + tree->rcount + tree->threads_below > 0);
        tree->wwait--;
    }
    tree->wcount++;
    tree->threads_below++;
    if ((err = pthread_mutex_unlock(&tree->lock)) != 0)
        return err;

    return 0;
}

static int writer_fp(Tree *tree) {

    int err;
    if ((err = pthread_mutex_lock(&tree->lock)) != 0)
        syserr("writer_fp_unluck");//return err;
    tree->wcount--;
    tree->threads_below--;
    if (tree->rwait > 0) {
        // budzimy czytelnika
        tree->change = 0;
        if ((err = pthread_cond_signal(&tree->readers)) != 0) {
            syserr("writer_fp_signal");//return err;
        }
    } else if ((err = pthread_cond_signal(&tree->writers)) != 0)
        syserr("writer_fp_signal");//return err;

    if ((err = pthread_mutex_unlock(&tree->lock)) != 0)
        syserr("writer_fp_unlock");//return err;

    return 0;
}

static Tree *find_node_r(Tree *tree, const char *path) {

    assert(is_path_valid(path));

    reader_pp(tree);

    Tree *child = find_child(tree, path);

    if (child == tree)
        return tree;

    reader_fp(tree);
    if (!child) {
        update_threads_below_bounded(tree, NULL);
        return NULL;
    } else {
        return find_node_r(child, split_path(path, NULL));
    }
}

static Tree *find_child(Tree *tree, const char *path) {

    if (path && strlen(path) == 1 && *path == '/')
        return tree;

    HashMapIterator it = hmap_iterator(tree->content);
    const char *key;
    void *value;
    char component[MAX_FOLDER_NAME_LENGTH + 1];
    path = split_path(path, component); assert(path);

    while (hmap_next(tree->content, &it, &key, &value))
        if (!strcmp(component, key))
            return value;

    return NULL;
}

static int update_threads_below(Tree *tree) {

    if (!tree)
        return 0;

    int err;
    if ((err = pthread_mutex_lock(&tree->lock)) != 0)
        syserr("upd_threads_lock");//return err;

    tree->threads_below--;
    Tree *parent = tree->parent;

    if (tree->threads_below == 0 && tree->wwait > 0)
        if (pthread_cond_signal(&tree->writers) != 0)
            syserr("upd_threads_signal");//return err;

    if (pthread_mutex_unlock(&tree->lock) != 0)
        syserr("upd_threads_unlock");//return err;

    return update_threads_below(parent);
}

static void update_threads_below_bounded(Tree *tree, Tree *bound) {

    if (!tree || (bound && tree == bound))
        return;

    int err;
    if ((err = pthread_mutex_lock(&tree->lock)) != 0)
        syserr("upd_threads_lock");//return err;

    tree->threads_below--;
    Tree *parent = tree->parent;

    if (tree->threads_below == 0 && tree->wwait > 0)
        if (pthread_cond_signal(&tree->writers) != 0)
            syserr("upd_threads_signal");//return err;

    if (pthread_mutex_unlock(&tree->lock) != 0)
        syserr("upd_threads_unlock");//return err;

    return update_threads_below_bounded(parent, bound);
}

static Tree *find_node(Tree *tree, const char *path) {


    if (path && strlen(path) == 1 && *path == '/')
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

    int err;
    Tree *dest = find_node_r(tree, path);

    if (!dest)
        return NULL;

    char *res = make_map_contents_string(dest->content);
    reader_fp(dest);
    update_threads_below(dest);

    return res;
}
void list() {
//    if (reader_pp(tree) != 0)
//        syserr("pthread err");
//
//    if (!is_path_valid(path))
//        return NULL;
//
//    Tree *dest = find_node(tree, path);
//    if (!dest)
//        return NULL;
//    char *res = make_map_contents_string(dest->content);
//
//    if (reader_fp(tree) != 0)
//        syserr("pthread err");
//
//    return res;
}

int tree_create(Tree *tree, const char *path) {

    if (strlen(path) == 1 && *path == '/')
        return EEXIST;
    if (!is_path_valid(path))
        return EINVAL;

    int err = 0;
    char component[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_par = make_path_to_parent(path, component);
    Tree *parent = find_and_lock_dest_h(tree, path_to_par, false, NULL);
    free(path_to_par);

    if (!parent)
        return ENOENT;
    assert(parent->threads_below == 1);
    if (hmap_get(parent->content, component)) { // folder już istnieje
        writer_fp(parent);
        update_threads_below_bounded(parent->parent, NULL);
        return EEXIST;
    }
    Tree *new = malloc(sizeof(Tree));
    if (!new)
        syserr("allocation failed");

    if ((err = pthread_mutex_init(&new->lock, NULL)) != 0)
        syserr("lock init failed");
    if ((err = pthread_cond_init(&new->readers, NULL)) != 0)
        syserr ("cond init failed");
    if ((err = pthread_cond_init(&new->writers, NULL)) != 0)
        syserr ("cond init failed");

    new->rcount = new->rwait = new->wcount = new->wwait = 0;
    new->change = 0;
    new->threads_below = 0;
    new->content = hmap_new();
    new->parent = parent;

    assert(hmap_insert(parent->content, component, new));

    writer_fp(parent);
    update_threads_below(parent->parent);
    return 0;
}

Tree *find_and_lock_dest(Tree *tree, const char *dest) {

    assert(is_path_valid(dest));
    if (strlen(dest) == 1 && *dest == '/') {
        writer_pp(tree);
        return tree;
    }
    reader_pp(tree);
    Tree *child = find_child(tree, dest);
    reader_fp(tree);
    if (!child) {
        update_threads_below(tree);
        return NULL;
    }
    return find_and_lock_dest(child, split_path(dest, NULL));
}

Tree *find_and_lock_dest_h(Tree *tree, const char *dest, bool lock_first, Tree *bound) {

    assert(is_path_valid(dest));
    if (strlen(dest) == 1 && *dest == '/') {
        writer_pp(tree);
        return tree;
    }
    if (lock_first)
        reader_pp(tree);

    Tree *child = find_child(tree, dest);
    if (lock_first)
        reader_fp(tree);
    if (!child) {
        if (lock_first)
            update_threads_below_bounded(tree, bound);
        else if (tree != bound)
            update_threads_below_bounded(tree->parent, bound);
        return NULL;
    }
    return find_and_lock_dest_h(child, split_path(dest, NULL), true, bound);
}

int tree_remove(Tree *tree, const char *path) {

    if (!is_path_valid(path))
        return EINVAL;
    if (path && strlen(path) == 1 && *path == '/')
        return EBUSY;

    int err;
    char component[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_par = make_path_to_parent(path, component);

    Tree *dest_par = find_and_lock_dest(tree, path_to_par);
    free(path_to_par);

    if (!dest_par)
        return ENOENT;

    assert(dest_par->threads_below == 1);
    Tree *dest = hmap_get(dest_par->content, component);

    if (!dest) {
        writer_fp(dest_par);
        update_threads_below_bounded(dest_par->parent, NULL);
        return ENOENT;
    }

    if ((err = pthread_mutex_trylock(&dest->lock)) != 0)
        fatal("remove dest trylock");

    assert(dest->threads_below == 0);

    if (hmap_size(dest->content) > 0) {
        if ((err = pthread_mutex_unlock(&dest->lock)) != 0)
            fatal("remove dest unlock");//return err;
        writer_fp(dest_par);
        update_threads_below_bounded(dest_par->parent, NULL);
        return ENOTEMPTY;
    }

    assert(hmap_remove(dest_par->content, component));

    if ((err = pthread_mutex_unlock(&dest->lock)) != 0)
        fatal("remove dest unlock");

    tree_free(dest);

    writer_fp(dest_par);
    update_threads_below_bounded(dest_par->parent, NULL);
    return 0;
//    return call_fp_and_return(dest_par, 0, false);
//    if ((err = writer_fp(tree)) != 0) {
//        dest = malloc(sizeof(Tree));
//        if (!dest) exit(1);
//        dest->f_name = malloc(strlen(component) + 1);
//        if (!dest->f_name) exit(1);
//        strcpy(dest->f_name, component);
//        dest->content = hmap_new();
//        hmap_insert(dest_par->content, dest->f_name, dest);
//        return err;
//    }
    return 0;
}

static bool is_parent_to(const char *source, const char *poten_child) {

    if (!source || !poten_child)
        return false;
    assert(is_path_valid(source) && is_path_valid(poten_child));
    char *target = make_path_to_parent(poten_child, NULL);
    while (target) {
        if (!strcmp(target, source)) {
            free(target);
            return true;
        }
        char *temp = target;
        target = make_path_to_parent(target, NULL);
        free(temp);
    }
    return false;
}

char *make_path_to_lca(const char *path1, const char *path2) {

    assert(is_path_valid(path1) && is_path_valid(path2));
    if (!strcmp(path1, path2))
        return strdup(path1);

    char *parent1 = strdup(path1), *parent2 = strdup(path2), *temp;
    while (parent1 && parent2) {
        int par1_len = (int) strlen(parent1);
        int par2_len = (int) strlen(parent2);
        if (par1_len > par2_len) {
            temp = parent1;
            parent1 = make_path_to_parent(parent1, NULL);
            free(temp);
        } else if (par1_len < par2_len) {
            temp = parent2;
            parent2 = make_path_to_parent(parent2, NULL);
            free(temp);
        } else if (!strcmp(parent1, parent2)) {
            free(parent1);
            return parent2;
        } else {
            temp = parent1;
            parent1 = make_path_to_parent(parent1, NULL);
            free(temp);
            temp = parent2;
            parent2 = make_path_to_parent(parent2, NULL);
            free(temp);
        }
    }
    assert(false); // "/" is the ancestor of all paths
    free(parent1), free(parent2);
    return NULL;
}

static const char *normalize_path(const char *path_to_lca, const char *path_to_dest) {

    int *i;
    if (strlen(path_to_lca) > strlen(path_to_dest)) {
        fprintf(stderr, "normalize path\n"), free(i);
    }
    return path_to_dest + strlen(path_to_lca) - 1;
}
// pamietac ze jezeli zrobilismy update threads below na nizszym wierzcholku to nie wywolywac na wyzzszym
int tree_move(Tree *tree, const char *source, const char *target) {

    if (!is_path_valid(source) || !is_path_valid(target))
        return EINVAL;
    if (strlen(source) == 1 && *source == '/')
        return EBUSY;
    if (strlen(target) == 1 && *target == '/')
        return EEXIST;
    if (is_parent_to(source, target))
        return -9; // TODO opisać błąd

    char *path_to_lca = make_path_to_lca(source, target);
    assert(path_to_lca);
    Tree *lca = find_and_lock_dest_h(tree, path_to_lca, true, NULL);
    if (!lca)
        return ENOENT;
    assert(lca->threads_below == 1);
    if (!strcmp(source, path_to_lca)) { // source == lca
        writer_fp(lca);
        update_threads_below_bounded(lca->parent, NULL);
        if (!strcmp(source, target)) { // source == target
            return 0;
        } else {
            // target jest przodkiem source
            return -9;
        }
    }
    if (!strcmp(target, path_to_lca)) { // source jest przodkiem target
        writer_fp(lca);
        update_threads_below_bounded(lca->parent, NULL);
        return EEXIST;
    }
    // no threads below lca, lca blocked
    Tree *src = find_and_lock_dest_h(lca, normalize_path(path_to_lca, source), false, lca);
    if (!src) {
        writer_fp(lca);
        update_threads_below_bounded(lca->parent, NULL);
        return ENOENT;
    }
    Tree *src_par = src->parent; // lca lub jego potomek
    if (src_par != lca)
        writer_pp(src_par);

    assert(src_par);
    Tree *trg_par;
    char trg_component[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_trg_par = make_path_to_parent(target, trg_component);
    if (!strcmp(path_to_trg_par, path_to_lca)) { // target parent == lca
        trg_par = lca;
    } else {
        trg_par = find_and_lock_dest_h(lca, normalize_path(path_to_lca, path_to_trg_par), false, lca);
        if (!trg_par) {
            free(path_to_trg_par);
            writer_fp(src);
            update_threads_below_bounded(src_par->parent, lca);
            if (src_par != lca)
                writer_fp(src_par);
            update_threads_below_bounded(lca->parent, NULL);
            writer_fp(lca);
            return ENOENT;
        } else if (src_par != lca) {
            assert(src_par != trg_par); // wpp src_par == lca
            update_threads_below_bounded(src_par->parent, lca);
            update_threads_below_bounded(trg_par->parent, lca);
            writer_fp(lca);
            update_threads_below_bounded(lca->parent, NULL);
        }
    }
    free(path_to_trg_par);
    bool target_exists = true;
    if (!hmap_get(trg_par->content, trg_component)) {
        // trg nie istnieje
        target_exists = false;
        char src_component[MAX_FOLDER_NAME_LENGTH + 1];
        free(make_path_to_parent(source, src_component));
        assert(hmap_remove(src_par->content, src_component));
        assert(hmap_insert(trg_par->content, trg_component, src));
        src->parent = trg_par;
        update_threads_below(lca->parent);
        writer_fp(lca);
    }
    writer_fp(src);
    if (src_par == trg_par) {
        assert(src_par == lca);
        writer_fp(src_par);
        update_threads_below_bounded(src_par->parent, NULL);
    } else if (trg_par == lca) {
        writer_fp(trg_par);
        update_threads_below_bounded(trg_par->parent, src_par);
        writer_fp(src_par);
        update_threads_below_bounded(src_par->parent, NULL);
    } else if (src_par == lca) {
        writer_fp(trg_par);
        update_threads_below_bounded(trg_par->parent, src_par);
        writer_fp(src_par);
        update_threads_below_bounded(src_par->parent, NULL);
    } else {
        writer_fp(src_par);
        writer_fp(trg_par);
    }
    if (target_exists)
        return 0;
    else
        return EEXIST;
}
    //
//    char src_component[MAX_FOLDER_NAME_LENGTH + 1];
//    char *p_to_src_par = make_path_to_parent(source, src_component);
//    assert(p_to_src_par);
//    Tree *src_par = find_node_p(tree, &err, p_to_src_par, false);
//    free(p_to_src_par);
//
//    if (!src_par)
//        return ENOENT;
//
//    Tree *src = hmap_get(src_par->content, src_component);
//
//    if (!src) {
//        update_threads_below(src_par->parent);
//        writer_fp(src_par);
//        return ENOENT;
//    }
//        //return call_fp_and_return(src_par, ENOENT, false);
//    if ((err = pthread_mutex_trylock(&src->lock)) != 0)
//        fatal("move src trylock");
//
//    char trg_component[MAX_FOLDER_NAME_LENGTH + 1];
//    char *p_to_trg_par = make_path_to_parent(target, trg_component);
//
//    Tree *trg_par = find_node_p(tree, &err,  p_to_trg_par, false);
//    free(p_to_trg_par);
//
//    if (!trg_par) {
//        if ((err = pthread_mutex_unlock(&src->lock)) != 0)
//            syserr("move src unlock");
//        update_threads_below(src_par->parent);
//        writer_fp(src_par);
//        return ENOENT;
//    }
//    if (hmap_get(trg_par->content, trg_component)) {
//        if ((err = pthread_mutex_unlock(&src->lock)) != 0)
//            syserr("move src unlock");
//        update_threads_below(src_par->parent);
//        update_threads_below(trg_par->parent);
//        writer_fp(src_par);
//        writer_fp(trg_par);
//        return EEXIST;
//    }
//    src->parent = trg_par;
//    assert(hmap_remove(src_par->content, src_component));
//    assert(hmap_insert(trg_par->content, src_component, src));
//
//    if ((err = pthread_mutex_unlock(&src->lock)) != 0)
//        syserr("move src unlock");
//    update_threads_below(src_par->parent);
//    update_threads_below(trg_par->parent);
//    writer_fp(src_par);
//    writer_fp(trg_par);
//    return 0;
    //
//    Tree *src = find_node(tree, source);
//    char trg_component[MAX_FOLDER_NAME_LENGTH + 1];
//    char *p_to_trg_par = make_path_to_parent(target, trg_component);
//
//    Tree *trg_par = find_node(tree, p_to_trg_par);
//    free(p_to_trg_par);
//
//    if (!src)
//        return ENOENT;
//    if (!strcmp(source, target))
//        return 0;
//    if (find_node(tree, target))
//        return EEXIST;
//    if (!trg_par)
//        return ENOENT;
////    if (!trg_par || !src)
////        return ENOENT;
//
//    char src_component[MAX_FOLDER_NAME_LENGTH + 1];
//    char *p_to_src_par = make_path_to_parent(source, src_component);
//    assert(p_to_src_par);
//    Tree *src_par = find_node(tree, p_to_src_par);
//    assert(hmap_remove(src_par->content, src_component));
//    free(p_to_src_par);
//    src->f_name = realloc(src->f_name, strlen(trg_component) + 1);
//    if (!src->f_name)
//        exit(1);
//
//    strcpy(src->f_name, trg_component);
//    assert(hmap_insert(trg_par->content, src->f_name, src));
//    return 0;
//}