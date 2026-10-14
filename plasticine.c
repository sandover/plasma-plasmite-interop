#include "interop.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void from_pool(pool_hose hose)
{
    /* A new hose already starts after the newest Protein. */
    for (;;) {
        protein p;
        check(pool_await_next(hose, POOL_WAIT_FOREVER, &p, NULL, NULL));
        yyjson_mut_doc *message = export_protein(p);
        char *line = write_json(message);
        if (puts(line) == EOF || fflush(stdout) == EOF)
            fail("cannot write stdout");
        free(line);
        yyjson_mut_doc_free(message);
        protein_free(p);
    }
}

static void to_pool(pool_hose hose)
{
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    while ((length = getline(&line, &capacity, stdin)) >= 0) {
        size_t start = 0;
        while (start < (size_t)length && isspace((unsigned char)line[start]))
            start++;
        if (start == (size_t)length)
            continue;
        yyjson_doc *message = read_json(line, (size_t)length);
        protein p = import_message(yyjson_doc_get_root(message));
        check(pool_deposit(hose, p, NULL));
        protein_free(p);
        yyjson_doc_free(message);
    }
    if (ferror(stdin))
        fail("cannot read stdin");
    free(line);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: plasticine from POOL | plasticine to POOL");
        return 0;
    }
    if (argc != 3 ||
        (strcmp(argv[1], "from") != 0 && strcmp(argv[1], "to") != 0)) {
        fprintf(stderr, "Usage: plasticine from POOL | plasticine to POOL\n");
        return 1;
    }
    pool_hose hose;
    check(pool_participate(argv[2], &hose, NULL));
    if (strcmp(argv[1], "from") == 0)
        from_pool(hose);
    else
        to_pool(hose);
    check(pool_withdraw(hose));
    return 0;
}
