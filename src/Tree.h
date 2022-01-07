#pragma once

typedef struct Tree Tree; // Let "Tree" mean the same as "struct Tree".

Tree* tree_new();

void tree_free(Tree*);

/**
 * Wymienia zawartość danego folderu, zwracając
 * nowy napis postaci "foo,bar,baz" (wszystkie
 * nazwy podfolderów; tylko bezpośrednich podfolderów,
 * czyli bez wchodzenia wgłąb; w dowolnej kolejności,
 * oddzielone przecinkami, zakończone znakiem zerowym).
 * (Zwolnienie pamięci napisu jest odpowiedzialnością
 * wołającego tree_list).
 */
char* tree_list(Tree* tree, const char* path);

int tree_create(Tree* tree, const char* path);

int tree_remove(Tree* tree, const char* path);

int tree_move(Tree* tree, const char* source, const char* target);
