#include "interop.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    int follow = 1;
    if (argc == 3 && strcmp(argv[2], "--no-follow") == 0)
        follow = 0;
    else if (argc != 2) {
        fprintf(stderr, "Usage: plasma-export POOL [--no-follow]\n");
        return 1;
    }

    pool_hose hose;
    check(pool_participate(argv[1], &hose, NULL));
    int64 last = 0;
    if (!follow) {
        ob_retort result = pool_newest_index(hose, &last);
        if (result == POOL_NO_SUCH_PROTEIN) {
            check(pool_withdraw(hose));
            return 0;
        }
        check(result);
    }
    check(pool_rewind(hose));
    for (;;) {
        protein p;
        int64 index;
        ob_retort result = pool_await_next(hose,
            follow ? POOL_WAIT_FOREVER : POOL_NO_WAIT, &p, NULL, &index);
        if (!follow && (result == POOL_AWAIT_TIMEDOUT ||
                        result == POOL_NO_SUCH_PROTEIN))
            break;
        check(result);
        if (!follow && index > last) {
            protein_free(p);
            break;
        }
        struct json_object *message = export_protein(p);
        const char *line = json_object_to_json_string_ext(message,
                                                        JSON_C_TO_STRING_PLAIN);
        if (puts(line) == EOF || fflush(stdout) == EOF)
            fail("cannot write stdout");
        json_object_put(message);
        protein_free(p);
        if (!follow && index == last)
            break;
    }
    check(pool_withdraw(hose));
    return 0;
}
