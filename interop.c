#include "interop.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct json_object json;

noreturn void fail(const char *message)
{
    fprintf(stderr, "plasticine: %s\n", message);
    exit(1);
}

void check(ob_retort result)
{
    if (result < 0)
        fail(ob_error_string(result));
}

static json *object(void)
{
    json *j = json_object_new_object();
    if (!j)
        fail("out of memory");
    return j;
}

static json *annotation(const char *type, json *value)
{
    json *j = object();
    json_object_object_add(j, "type", json_object_new_string(type));
    json_object_object_add(j, "value", value);
    return j;
}

static int annotated(json *j)
{
    json *type, *value;
    return json_object_is_type(j, json_type_object) &&
           json_object_object_length(j) == 2 &&
           json_object_object_get_ex(j, "type", &type) &&
           json_object_object_get_ex(j, "value", &value);
}

static json *binary_value(bslaw value)
{
    char *hex = slaw_to_hex(value);
    json *encoded = annotation("slaw", json_object_new_string(hex));
    free(hex);
    return encoded;
}

static json *encode(bslaw s, unsigned depth)
{
    if (depth > 32)
        return binary_value(s);
    if (slaw_is_nil(s))
        return NULL; /* json-c represents JSON null with a NULL pointer. */
    if (slaw_is_boolean(s))
        return json_object_new_boolean(*slaw_boolean_emit(s));
    if (slaw_is_string(s)) {
        int64 length = slaw_string_emit_length(s);
        if (!slaw_string_is_valid_utf8(s) ||
            (int64)strlen(slaw_string_emit(s)) != length)
            return binary_value(s);
        if (length > INT_MAX)
            fail("string is too large");
        return json_object_new_string_len(slaw_string_emit(s), (int)length);
    }
    if (slaw_is_int64(s)) {
        int64 n = *slaw_int64_emit(s);
        /* Keep large integers exact when a JavaScript program handles JSON. */
        if (n >= -INT64_C(9007199254740991) && n <= INT64_C(9007199254740991))
            return json_object_new_int64(n);
        char text[32];
        snprintf(text, sizeof text, "%" PRId64, (int64_t)n);
        return annotation("int64", json_object_new_string(text));
    }
    if (slaw_is_unt64(s)) {
        unt64 n = *slaw_unt64_emit(s);
        if (n <= UINT64_C(9007199254740991))
            return annotation("unt64", json_object_new_int64((int64_t)n));
        char text[32];
        snprintf(text, sizeof text, "%" PRIu64, (uint64_t)n);
        return annotation("unt64", json_object_new_string(text));
    }
#define EXPORT_INTEGER(type) \
    if (slaw_is_##type(s)) \
        return annotation(#type, json_object_new_int64(*slaw_##type##_emit(s)))
    EXPORT_INTEGER(int8);
    EXPORT_INTEGER(int16);
    EXPORT_INTEGER(int32);
    EXPORT_INTEGER(unt8);
    EXPORT_INTEGER(unt16);
    EXPORT_INTEGER(unt32);
#undef EXPORT_INTEGER
    if (slaw_is_float32(s) || slaw_is_float64(s)) {
        int narrow = slaw_is_float32(s);
        double n = narrow ? *slaw_float32_emit(s) : *slaw_float64_emit(s);
        if (!isfinite(n))
            return binary_value(s);
        json *value = json_object_new_double(n);
        return narrow ? annotation("float32", value) : value;
    }
    if (slaw_is_map(s)) {
        json *j = object();
        const char *previous_key = NULL;
        for (bslaw entry = slaw_list_emit_first(s); entry;
             entry = slaw_list_emit_next(s, entry)) {
            bslaw key = slaw_cons_emit_car(entry);
            if (!slaw_is_string(key) ||
                !slaw_string_is_valid_utf8(key) ||
                (int64)strlen(slaw_string_emit(key)) != slaw_string_emit_length(key)) {
                json_object_put(j);
                return binary_value(s);
            }
            const char *name = slaw_string_emit(key);
            /* Plasmite sorts JSON object keys. Preserve other map orders in Slaw. */
            if (previous_key && strcmp(previous_key, name) >= 0) {
                json_object_put(j);
                return binary_value(s);
            }
            previous_key = name;
            json_object_object_add(j, name,
                                   encode(slaw_cons_emit_cdr(entry), depth + 1));
        }
        if (annotated(j)) {
            json_object_put(j);
            return binary_value(s);
        }
        return j;
    }
    if (slaw_is_list(s)) {
        json *j = json_object_new_array();
        for (bslaw child = slaw_list_emit_first(s); child;
             child = slaw_list_emit_next(s, child))
            json_object_array_add(j, encode(child, depth + 1));
        return j;
    }
    return binary_value(s);
}

static const char *integer_text(json *value)
{
    if (!json_object_is_type(value, json_type_int) &&
        !json_object_is_type(value, json_type_string))
        fail("integer value must be an integer or a decimal string");
    const char *text = json_object_get_string(value);
    const char *digit = text;
    if (*digit == '-')
        digit++;
    if (!*digit)
        fail("invalid integer");
    for (; *digit; digit++)
        if (*digit < '0' || *digit > '9')
            fail("invalid integer");
    if (json_object_is_type(value, json_type_string) &&
        strlen(text) != (size_t)json_object_get_string_len(value))
        fail("invalid integer");
    if (json_object_is_type(value, json_type_int)) {
        errno = 0;
        long long n = strtoll(text, NULL, 10);
        if (errno || n < -INT64_C(9007199254740991) ||
            n > INT64_C(9007199254740991))
            fail("use a type annotation with a decimal string for a large integer");
    }
    return text;
}

static int64_t signed_integer(json *value)
{
    const char *text = integer_text(value);
    errno = 0;
    int64_t n = strtoll(text, NULL, 10);
    if (errno)
        fail("integer is outside the signed 64-bit range");
    return n;
}

static uint64_t unsigned_integer(json *value)
{
    const char *text = integer_text(value);
    if (*text == '-')
        fail("unsigned integer cannot be negative");
    errno = 0;
    uint64_t n = strtoull(text, NULL, 10);
    if (errno)
        fail("integer is outside the unsigned 64-bit range");
    return n;
}

static slaw decode_annotation(json *j)
{
    json *type, *value;
    json_object_object_get_ex(j, "type", &type);
    json_object_object_get_ex(j, "value", &value);
    if (!json_object_is_type(type, json_type_string))
        fail("annotation type must be a string");
    const char *name = json_object_get_string(type);
    if (strlen(name) != (size_t)json_object_get_string_len(type))
        fail("invalid annotation type");
    if (strcmp(name, "slaw") == 0) {
        if (!json_object_is_type(value, json_type_string) ||
            strlen(json_object_get_string(value)) != (size_t)json_object_get_string_len(value))
            fail("binary Slaw value must be a hex string");
        return slaw_from_hex(json_object_get_string(value));
    }
#define IMPORT_SIGNED(type, minimum, maximum) \
    if (strcmp(name, #type) == 0) { \
        int64_t n = signed_integer(value); \
        if (n < (minimum) || n > (maximum)) fail("value is outside " #type " range"); \
        return slaw_##type((type)n); \
    }
    IMPORT_SIGNED(int8, INT8_MIN, INT8_MAX);
    IMPORT_SIGNED(int16, INT16_MIN, INT16_MAX);
    IMPORT_SIGNED(int32, INT32_MIN, INT32_MAX);
    IMPORT_SIGNED(int64, INT64_MIN, INT64_MAX);
#undef IMPORT_SIGNED
#define IMPORT_UNSIGNED(type, maximum) \
    if (strcmp(name, #type) == 0) { \
        uint64_t n = unsigned_integer(value); \
        if (n > (maximum)) fail("value is outside " #type " range"); \
        return slaw_##type((type)n); \
    }
    IMPORT_UNSIGNED(unt8, UINT8_MAX);
    IMPORT_UNSIGNED(unt16, UINT16_MAX);
    IMPORT_UNSIGNED(unt32, UINT32_MAX);
    IMPORT_UNSIGNED(unt64, UINT64_MAX);
#undef IMPORT_UNSIGNED
    if (strcmp(name, "float32") == 0 || strcmp(name, "float64") == 0) {
        if (!json_object_is_type(value, json_type_double) &&
            !json_object_is_type(value, json_type_int))
            fail("float value must be a JSON number");
        double n = json_object_get_double(value);
        if (!isfinite(n))
            fail("float must be finite");
        if (strcmp(name, "float64") == 0)
            return slaw_float64(n);
        float f = (float)n;
        if (!isfinite(f) || (n != 0 && f == 0))
            fail("value is outside float32 range");
        return slaw_float32(f);
    }
    fail("unknown type annotation");
    return NULL;
}

static slaw decode(json *j, unsigned depth)
{
    if (depth > 64)
        fail("value nests too deeply");
    switch (json_object_get_type(j)) {
    case json_type_null:
        return slaw_nil();
    case json_type_boolean:
        return slaw_boolean(json_object_get_boolean(j));
    case json_type_string:
        return slaw_string_from_substring(json_object_get_string(j),
                                         json_object_get_string_len(j));
    case json_type_int:
        return slaw_int64(signed_integer(j));
    case json_type_double: {
        double n = json_object_get_double(j);
        if (!isfinite(n))
            fail("float must be finite");
        return slaw_float64(n);
    }
    case json_type_array: {
        slabu *items = slabu_new();
        for (size_t i = 0; i < json_object_array_length(j); i++)
            check(slabu_list_add_f(items, decode(json_object_array_get_idx(j, i), depth + 1)));
        return slaw_list_f(items);
    }
    case json_type_object: {
        if (annotated(j))
            return decode_annotation(j);
        slabu *items = slabu_new();
        json_object_object_foreach(j, key, value) {
            check(slabu_map_put_cf(items, key, decode(value, depth + 1)));
        }
        return slaw_map_f(items);
    }
    }
    fail("unsupported JSON value");
    return NULL;
}

json *export_protein(bprotein p)
{
    json *data = object();
    int64 rude_length = 0;
    if (!protein_is_nonstandard(p))
        protein_rude(p, &rude_length);
    bslaw descrips = protein_is_nonstandard(p) ? NULL : protein_descrips(p);
    bslaw ingests = protein_is_nonstandard(p) ? NULL : protein_ingests(p);
    if (protein_is_nonstandard(p) || rude_length != 0 ||
        (descrips && !slaw_is_list(descrips)) ||
        (ingests && !slaw_is_list_or_map(ingests))) {
        json_object_object_add(data, "protein", binary_value(p));
    } else {
        if (descrips)
            json_object_object_add(data, "descrips", encode(descrips, 0));
        if (ingests)
            json_object_object_add(data, "ingests", encode(ingests, 0));
    }
    json *message = object();
    json *meta = object();
    json_object_object_add(meta, "tags", json_object_new_array());
    json_object_object_add(message, "meta", meta);
    json_object_object_add(message, "data", data);
    return message;
}

protein import_message(json *message)
{
    json *data, *value;
    if (!json_object_is_type(message, json_type_object) ||
        !json_object_object_get_ex(message, "data", &data) ||
        !json_object_is_type(data, json_type_object))
        fail("expected a Plasmite message with an object data field");
    if (json_object_object_get_ex(data, "protein", &value)) {
        if (json_object_object_length(data) != 1 || !annotated(value))
            fail("data.protein must be the only data field and a Slaw annotation");
        slaw p = decode_annotation(value);
        if (!slaw_is_protein(p))
            fail("data.protein must contain a Protein");
        return p;
    }
    json_object_object_foreach(data, key, child) {
        (void)child;
        if (strcmp(key, "descrips") != 0 && strcmp(key, "ingests") != 0)
            fail("data must contain only descrips and ingests");
    }
    slaw descrips = json_object_object_get_ex(data, "descrips", &value)
        ? decode(value, 0) : NULL;
    slaw ingests = json_object_object_get_ex(data, "ingests", &value)
        ? decode(value, 0) : NULL;
    if (descrips && !slaw_is_list(descrips))
        fail("descrips must be a list");
    if (ingests && !slaw_is_list_or_map(ingests))
        fail("ingests must be a list or map");
    protein p = protein_from_ff(descrips, ingests);
    if (!p)
        fail("cannot construct protein");
    return p;
}
