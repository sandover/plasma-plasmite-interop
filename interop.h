#ifndef INTEROP_H
#define INTEROP_H

#include "vendor/yyjson/yyjson.h"
#include <stdnoreturn.h>
#include "libPlasma/c/c-plasma.h"

noreturn void fail(const char *message);
void check(ob_retort result);
yyjson_doc *read_json(const char *text, size_t length);
char *write_json(yyjson_mut_doc *document);
yyjson_mut_doc *export_protein(bprotein p);
protein import_message(yyjson_val *message);
char *slaw_to_hex(bslaw value);
slaw slaw_from_hex(const char *hex);

#endif
