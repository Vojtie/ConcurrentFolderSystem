#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include "Tree.h"
#include "HashMap.h"
#include "path_utils.h"
#include "err.h"
/**
 * Wojciech Kuzebski
 * Wykorzystuję schemat pisarzy i czytelników, gdzie
 * pisarze to wyołania funkcji create, move i remove a czytelnicy
 * funkcji list. W każdy wierzchołku drzewa zliczam wątki,
 * znajdujące się w tym wierzchołku lub jego poddrzewie.
 * Każdy pisarz czeka przed wejściem do wierzchołka, w którym
 * musi coś zmienić aż wątki w jego poddrzewie się skończą.
 */
struct Tree {
    HashMap *content; // zawartość folderu
    pthread_mutex_t lock;
    pthread_cond_t readers;
    pthread_cond_t writers;
    int rcount, wcount, rwait, wwait;
    int change;
    int no_threads;
    Tree *parent;
};

static Tree *find_node_r(Tree *tree, const char *path);
static Tree *find_child(Tree *tree, const char *path);
static Tree *find_node_w(Tree *tree, const char *dest, bool lock_first, Tree *bound);
static void update_no_threads(Tree *tree, Tree *bound);

Tree *tree_new() {

    Tree *new = malloc(sizeof(Tree));
    if (!new)
        exit(1);

    new->content = hmap_new();
    assert(new->content);
    
    if (pthread_mutex_init(&new->lock, NULL) != 0)
        syserr("lock init failed");
    if (pthread_cond_init(&new->readers, NULL) != 0)
        syserr ("cond init failed");
    if (pthread_cond_init(&new->writers, NULL) != 0)
        syserr ("cond init failed");

    new->rcount = new->rwait = new->wcount = new->wwait = 0;
    new->change = 0;
    new->no_threads = 0;
    new->parent = NULL;
    return new;
}

void tree_free(Tree *tree) {

    assert(tree);
    
    if (pthread_cond_destroy(&tree->readers) != 0)
        syserr ("cond destroy 1 failed");
    if (pthread_cond_destroy(&tree->writers) != 0)
        syserr ("cond destroy 2 failed");
    if (pthread_mutex_destroy(&tree->lock) != 0)
        syserr ("lock destroy failed");

    HashMapIterator it = hmap_iterator(tree->content);
    const char *key;
    void *value;
    while (hmap_next(tree->content, &it, &key, &value))
        tree_free(value);

    hmap_free(tree->content);
    free(tree);
}
/**
 * protokół początkowy czytelników
 */
static void reader_pp(Tree *tree) {

    
    if (pthread_mutex_lock(&tree->lock) != 0)
        syserr("mutex lock failed");
    if (tree->wcount > 0 || (tree->change == 1 && (tree->rcount > 0 || tree->wwait > 0))) {
        tree->rwait++;
        do {
            if (pthread_cond_wait(&tree->readers, &tree->lock) != 0)
                syserr("cond wait failed");
        } while (tree->wcount > 0 || (tree->rcount > 0 && tree->change == 1)
                 || (tree->wwait > 0 && tree->change == 1));
        tree->rwait--;
    }
    tree->rcount++;
    if (tree->rwait > 0) {
        tree->change = 0;
        if (pthread_cond_signal(&tree->readers) != 0)
            syserr("cond signal failed");
    } else if (tree->wwait > 0)
        tree->change = 1;
    tree->no_threads++;
    if (pthread_mutex_unlock(&tree->lock) != 0)
        syserr("");
}

/**
 * protokół końcowy czytelników
 */
static void reader_fp(Tree *tree) {

    
    if (pthread_mutex_lock(&tree->lock) != 0)
        syserr("mutex lock failed");
    tree->rcount--;
    // jesli jestesmy ostatnim czytelnikiem to budzimy pisarza (drzwi sa zamknięte)
    if (tree->rcount == 0 && pthread_cond_signal(&tree->writers) != 0)
        syserr("reader_fp_signal");//return err;

    if (pthread_mutex_unlock(&tree->lock) != 0)
        syserr("reader_fp_unlock");//return err;
}

/**
 * protokół początkowy pisarzy
 */
static void writer_pp(Tree *tree) {

    
    if (pthread_mutex_lock(&tree->lock) != 0)
        syserr("");
    // czekamy jesli sala jest niepusta
    if (tree->wcount + tree->rcount + tree->no_threads > 0) {
        tree->wwait++;
        do {
            if (pthread_cond_wait(&tree->writers, &tree->lock) != 0)
                syserr("");
        } while (tree->wcount + tree->rcount + tree->no_threads > 0);
        tree->wwait--;
    }
    assert(tree->wcount == 0);
    assert(tree->no_threads == 0);
    tree->wcount++;
    tree->no_threads++;
    if (pthread_mutex_unlock(&tree->lock) != 0)
        syserr("");
}

/**
 * protokół końcowy pisarzy
 */
static void writer_fp(Tree *tree) {

    
    if (pthread_mutex_lock(&tree->lock) != 0)
        syserr("mutex lock failed");
    tree->wcount--;
    tree->no_threads--;
    if (tree->rwait > 0) {
        tree->change = 0;
        if (pthread_cond_signal(&tree->readers) != 0) {
            syserr("cond signal failed");//return err;
        }
    } else if (pthread_cond_signal(&tree->writers) != 0)
        syserr("cond signal failed");//return err;

    if (pthread_mutex_unlock(&tree->lock) != 0)
        syserr("mutex unlock failed");
}

/**
 * znajduje wierzchołek schodząc po drzewie jako czytelnik
 */
static Tree *find_node_r(Tree *tree, const char *path) {

    assert(is_path_valid(path));

    reader_pp(tree);

    Tree *child = find_child(tree, path);

    if (child == tree)
        return tree;

    reader_fp(tree);
    if (!child) {
        update_no_threads(tree, NULL);
        return NULL;
    } else {
        return find_node_r(child, split_path(path, NULL));
    }
}

/**
 * jeśli istnieje w tree dziecko o podanej
 * ścieżce zwraca na nie wskaźnik, wpp NULL.
 */
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

/**
 * aktualizuje atrybut zliczający liczbę wątków w poddrzewie
 * zmniejszając go o 1 na ścieżce w górę od tree do bound lub
 * do korzenia drzewa jeśli bound == NULL
 */
static void update_no_threads(Tree *tree, Tree *bound) {

    if (tree && tree != bound) {
        if (pthread_mutex_lock(&tree->lock) != 0)
            syserr("mutex lock failed");
        
        tree->no_threads--;
        assert(tree->no_threads >= 0);
        Tree *parent = tree->parent;
        if (tree->no_threads + tree->rwait + tree->rcount + tree->wcount + tree->change == 0
            && tree->wwait > 0 && pthread_cond_signal(&tree->writers) != 0)
                syserr("cond signal failed");
        
        if (pthread_mutex_unlock(&tree->lock) != 0)
            syserr("mutex unlock failed");
        
        return update_no_threads(parent, bound);
    }
}

char *tree_list(Tree *tree, const char *path) {

    if (!is_path_valid(path))
        return NULL;

    Tree *dest = find_node_r(tree, path);

    if (!dest)
        return NULL;

    char *res = make_map_contents_string(dest->content);
    reader_fp(dest);
    update_no_threads(dest, NULL);

    return res;
}

int tree_create(Tree *tree, const char *path) {

    if (strlen(path) == 1 && *path == '/')
        return EEXIST;
    if (!is_path_valid(path))
        return EINVAL;

    
    char component[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_par = make_path_to_parent(path, component);
    Tree *parent = find_node_w(tree, path_to_par, true, NULL);
    free(path_to_par);

    if (!parent)
        return ENOENT;
    assert(parent->no_threads == 1);
    if (hmap_get(parent->content, component)) { // folder już istnieje
        writer_fp(parent);
        update_no_threads(parent->parent, NULL);
        return EEXIST;
    }
    Tree *new = malloc(sizeof(Tree));
    if (!new)
        syserr("allocation failed");

    if (pthread_mutex_init(&new->lock, NULL) != 0)
        syserr("lock init failed");
    if (pthread_cond_init(&new->readers, NULL) != 0)
        syserr ("cond init failed");
    if (pthread_cond_init(&new->writers, NULL) != 0)
        syserr ("cond init failed");

    new->rcount = new->rwait = new->wcount = new->wwait = 0;
    new->change = 0;
    new->no_threads = 0;
    new->content = hmap_new();
    new->parent = parent;

    assert(hmap_insert(parent->content, component, new));

    writer_fp(parent);
    update_no_threads(parent->parent, NULL);
    return 0;
}

/**
 * schodzi po drzewie jako czytelnik szukając wierzchołka dest,
 * jeśli go znajdzie to blokuje go jako pisarz, wpp zwraca NULL
 */
Tree *find_node_w(Tree *tree, const char *dest, bool lock_first, Tree *bound) {

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
            update_no_threads(tree, bound);
        else if (tree != bound)
            update_no_threads(tree->parent, bound);
        return NULL;
    }
    return find_node_w(child, split_path(dest, NULL), true, bound);
}

int tree_remove(Tree *tree, const char *path) {

    if (!is_path_valid(path))
        return EINVAL;
    if (path && strlen(path) == 1 && *path == '/')
        return EBUSY;

    
    char component[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_par = make_path_to_parent(path, component);

    Tree *dest_par = find_node_w(tree, path_to_par, true, NULL);
    free(path_to_par);

    if (!dest_par)
        return ENOENT;
    assert(dest_par->no_threads == 1);
    Tree *dest = hmap_get(dest_par->content, component);

    if (!dest) {
        writer_fp(dest_par);
        update_no_threads(dest_par->parent, NULL);
        return ENOENT;
    }

    if (pthread_mutex_trylock(&dest->lock) != 0)
        fatal("mutex lock failed");

    assert(dest->no_threads == 0);

    if (hmap_size(dest->content) > 0) {
        if (pthread_mutex_unlock(&dest->lock) != 0)
            fatal("mutex unlock failed");//return err;
        writer_fp(dest_par);
        update_no_threads(dest_par->parent, NULL);
        return ENOTEMPTY;
    }

    assert(hmap_remove(dest_par->content, component));

    if (pthread_mutex_unlock(&dest->lock) != 0)
        fatal("mutex unlock failed");

    tree_free(dest);

    writer_fp(dest_par);
    update_no_threads(dest_par->parent, NULL);
    return 0;
}

/**
 * sprawdza czy poten_child jest podfolderem source
 */
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

/**
 * zwraca ścieżkę do najniższego wspólnego przodka path1 i path2
 */
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
    assert(false);
}

/**
 * obcina ścieżkę dest o przedrostek parent
 */
static const char *cut_path(const char *path1, const char *path2) {
    
    assert(strlen(path1) <= strlen(path2));
    return path2 + strlen(path1) - 1;
}

int tree_move(Tree *tree, const char *source, const char *target) {

         if (!is_path_valid(source) || !is_path_valid(target))
        return EINVAL;
    if (strlen(source) == 1 && *source == '/')
        return EBUSY;
    if (strlen(target) == 1 && *target == '/')
        return EEXIST;
    if (is_parent_to(source, target))
        return -9; // target jest potomkiem source

    char *path_to_lca = make_path_to_lca(source, target);
    assert(path_to_lca);
    Tree *lca = find_node_w(tree, path_to_lca, true, NULL);
    if (!lca)
        return ENOENT;
    assert(lca->no_threads == 1);
    if (!strcmp(source, path_to_lca)) { // source == lca
        writer_fp(lca);
        update_no_threads(lca->parent, NULL);
        if (!strcmp(source, target)) { // source == target
            return 0;
        }
    }
    if (!strcmp(target, path_to_lca)) { // source jest przodkiem target
        writer_fp(lca);
        update_no_threads(lca->parent, NULL);
        return EEXIST;
    }
    char comp[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_src_par = make_path_to_parent(source, comp);
    Tree *src_par;
    if (!strcmp(path_to_lca, path_to_src_par))
        src_par = lca;
    else
        src_par = find_node_w(lca, cut_path(path_to_lca, path_to_src_par), false, lca);
    if (!src_par) {
        writer_fp(lca);
        update_no_threads(lca->parent, NULL);
        free(path_to_src_par);
        return ENOENT;
    }
    Tree *src = find_child(src_par, cut_path(path_to_src_par, source));
    if (!src) {
        free(path_to_src_par);
        if (src_par != lca) {
            writer_fp(src_par);
            update_no_threads(src_par->parent, lca);
        }
        writer_fp(lca);
        update_no_threads(lca->parent, NULL);
        return ENOENT;
    }
    writer_pp(src);
    Tree *trg_par;
    char trg_component[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_trg_par = make_path_to_parent(target, trg_component);
    if (!strcmp(path_to_trg_par, path_to_lca)) { // target parent == lca
        trg_par = lca;
    } else {
        trg_par = find_node_w(lca, cut_path(path_to_lca, path_to_trg_par), false, lca);
        if (!trg_par) {
            free(path_to_src_par);
            free(path_to_trg_par);
            writer_fp(src);
            update_no_threads(src_par->parent, lca);
            if (src_par != lca)
                writer_fp(src_par);
            update_no_threads(lca->parent, NULL);
            writer_fp(lca);
            return ENOENT;
        }
    }
    if (src_par != lca && lca != trg_par) {
        if (is_parent_to(path_to_src_par, path_to_trg_par)) {
            update_no_threads(src_par->parent, lca);
        } else if (is_parent_to(path_to_trg_par, path_to_src_par)) {
            update_no_threads(trg_par->parent, lca);
        } else {
            update_no_threads(src_par->parent, lca);
            update_no_threads(trg_par->parent, lca);
        }
        writer_fp(lca);
        update_no_threads(lca->parent, NULL);
    }
    bool target_exists = true;
    if (!hmap_get(trg_par->content, trg_component)) {
        // trg nie istnieje
        target_exists = false;
        char src_component[MAX_FOLDER_NAME_LENGTH + 1];
        free(make_path_to_parent(source, src_component));
        assert(hmap_remove(src_par->content, src_component));
        assert(hmap_insert(trg_par->content, trg_component, src));
        src->parent = trg_par;
    }
    writer_fp(src);
    if (src_par == trg_par) {
        writer_fp(lca);
        update_no_threads(lca->parent, NULL);
    } else if (src_par == lca) {
        writer_fp(trg_par);
        update_no_threads(trg_par->parent, src_par);
        writer_fp(src_par);
        update_no_threads(src_par->parent, NULL);
    } else if (trg_par == lca) {
        writer_fp(src_par);
        update_no_threads(src_par->parent, trg_par);
        writer_fp(trg_par);
        update_no_threads(trg_par->parent, NULL);
    } else if (is_parent_to(path_to_src_par, path_to_trg_par)) {
        writer_fp(trg_par);
        update_no_threads(trg_par->parent, src_par);
        writer_fp(src_par);
    } else if (is_parent_to(path_to_trg_par, path_to_src_par)) {
        writer_fp(src_par);
        update_no_threads(src_par->parent, trg_par);
        writer_fp(trg_par);
    } else {
        writer_fp(src_par);
        writer_fp(trg_par);
    }
    free(path_to_src_par);
    free(path_to_trg_par);
    if (!target_exists)
        return 0;
    else
        return EEXIST;
}
