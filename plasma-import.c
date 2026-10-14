#include "interop.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

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

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: plasma-import POOL\n");
        return 1;
    }
    pool_hose hose;
    check(pool_participate(argv[1], &hose, NULL));
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
    check(pool_withdraw(hose));
    return 0;
}
