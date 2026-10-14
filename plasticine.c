#include "interop.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void from_pool(pool_hose hose)
{
    /* A new hose already starts after the newest Protein. */
    for (;;) {
        protein p;
        check(pool_await_next(hose, POOL_WAIT_FOREVER, &p, NULL, NULL));
        struct json_object *message = export_protein(p);
        const char *line = json_object_to_json_string_ext(message,
                                                        JSON_C_TO_STRING_PLAIN);
        if (puts(line) == EOF || fflush(stdout) == EOF)
            fail("cannot write stdout");
        json_object_put(message);
        protein_free(p);
    }
}

static void reject_nul_escapes(const char *line)
{
    /* json-c truncates object keys at NUL; keep such values in binary Slaw. */
    for (const char *p = line; *p; p++) {
        if (*p == '\\' && p[1]) {
            if (strncmp(p + 1, "u0000", 5) == 0)
                fail("use a binary Slaw annotation for NUL bytes");
            p++;
        }
    }
}

static void to_pool(pool_hose hose)
{
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    struct json_tokener *parser = json_tokener_new_ex(64);
    if (!parser)
        fail("out of memory");
    json_tokener_set_flags(parser, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    while ((length = getline(&line, &capacity, stdin)) >= 0) {
        size_t start = 0;
        while (start < (size_t)length && isspace((unsigned char)line[start]))
            start++;
        if (start == (size_t)length)
            continue;
        if (length >= INT_MAX)
            fail("JSON line is too large");
        if (memchr(line, '\0', (size_t)length))
            fail("unexpected NUL byte in JSON");
        reject_nul_escapes(line);
        json_tokener_reset(parser);
        struct json_object *message = json_tokener_parse_ex(parser, line,
                                                           (int)length);
        if (json_tokener_get_error(parser) != json_tokener_success)
            fail(json_tokener_error_desc(json_tokener_get_error(parser)));
        size_t end = json_tokener_get_parse_end(parser);
        while (end < (size_t)length && isspace((unsigned char)line[end]))
            end++;
        if (end != (size_t)length)
            fail("expected one JSON message per line");
        protein p = import_message(message);
        check(pool_deposit(hose, p, NULL));
        protein_free(p);
        json_object_put(message);
    }
    if (ferror(stdin))
        fail("cannot read stdin");
    free(line);
    json_tokener_free(parser);
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
