#ifndef INTEROP_H
#define INTEROP_H

#include <json-c/json.h>
#include <stdnoreturn.h>
#include "libPlasma/c/c-plasma.h"

noreturn void fail(const char *message);
void check(ob_retort result);
struct json_object *export_protein(bprotein p);
protein import_message(struct json_object *message);
char *slaw_to_hex(bslaw value);
slaw slaw_from_hex(const char *hex);

#endif
