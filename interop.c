#include "interop.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

yyjson_doc *read_json(const char *text, size_t length)
{
    yyjson_read_err error;
    /* Reject oversized integers during conversion instead of rounding to double. */
    yyjson_doc *document = yyjson_read_opts((char *)text, length,
        YYJSON_READ_BIGNUM_AS_RAW, NULL, &error);
    if (!document)
        fail(error.msg);
    return document;
}

char *write_json(yyjson_mut_doc *document)
{
    yyjson_write_err error;
    char *text = yyjson_mut_write_opts(document, 0, NULL, NULL, &error);
    if (!text)
        fail(error.msg);
    return text;
}

static yyjson_val *field(yyjson_val *object, const char *name)
{
    yyjson_val *found = NULL, *key, *value;
    size_t index, count;
    yyjson_obj_foreach(object, index, count, key, value) {
        if (yyjson_equals_str(key, name)) {
            if (found)
                fail("duplicate JSON field");
            found = value;
        }
    }
    return found;
}

static yyjson_mut_val *annotation(yyjson_mut_doc *document, const char *type,
                                  yyjson_mut_val *value)
{
    yyjson_mut_val *object = yyjson_mut_obj(document);
    if (!object || !value ||
        !yyjson_mut_obj_add_strcpy(document, object, "type", type) ||
        !yyjson_mut_obj_add_val(document, object, "value", value))
        fail("out of memory");
    return object;
}

static bool annotated(yyjson_val *value)
{
    return yyjson_is_obj(value) && yyjson_obj_size(value) == 2 &&
           field(value, "type") && field(value, "value");
}

static yyjson_mut_val *binary_value(yyjson_mut_doc *document, bslaw value)
{
    char *hex = slaw_to_hex(value);
    yyjson_mut_val *encoded = annotation(document, "slaw", yyjson_mut_strcpy(document, hex));
    free(hex);
    return encoded;
}

static yyjson_mut_val *encode(yyjson_mut_doc *document, bslaw value, unsigned depth)
{
    if (depth > 32)
        return binary_value(document, value);
    if (slaw_is_nil(value))
        return yyjson_mut_null(document);
    if (slaw_is_boolean(value))
        return yyjson_mut_bool(document, *slaw_boolean_emit(value));
    if (slaw_is_string(value)) {
        int64 length = slaw_string_emit_length(value);
        if (!slaw_string_is_valid_utf8(value) ||
            (int64)strlen(slaw_string_emit(value)) != length)
            return binary_value(document, value);
        return yyjson_mut_strncpy(document, slaw_string_emit(value), (size_t)length);
    }
    if (slaw_is_int64(value)) {
        int64 n = *slaw_int64_emit(value);
        if (n >= -INT64_C(9007199254740991) && n <= INT64_C(9007199254740991))
            return yyjson_mut_sint(document, n);
        char text[32];
        snprintf(text, sizeof text, "%" PRId64, (int64_t)n);
        return annotation(document, "int64", yyjson_mut_strcpy(document, text));
    }
    if (slaw_is_unt64(value)) {
        unt64 n = *slaw_unt64_emit(value);
        if (n <= UINT64_C(9007199254740991))
            return annotation(document, "unt64", yyjson_mut_uint(document, n));
        char text[32];
        snprintf(text, sizeof text, "%" PRIu64, (uint64_t)n);
        return annotation(document, "unt64", yyjson_mut_strcpy(document, text));
    }
#define EXPORT_INTEGER(type) \
    if (slaw_is_##type(value)) \
        return annotation(document, #type, yyjson_mut_sint(document, *slaw_##type##_emit(value)))
    EXPORT_INTEGER(int8);
    EXPORT_INTEGER(int16);
    EXPORT_INTEGER(int32);
    EXPORT_INTEGER(unt8);
    EXPORT_INTEGER(unt16);
    EXPORT_INTEGER(unt32);
#undef EXPORT_INTEGER
    if (slaw_is_float32(value) || slaw_is_float64(value)) {
        bool narrow = slaw_is_float32(value);
        double n = narrow ? *slaw_float32_emit(value) : *slaw_float64_emit(value);
        if (!isfinite(n))
            return binary_value(document, value);
        yyjson_mut_val *number = yyjson_mut_real(document, n);
        return narrow ? annotation(document, "float32", number) : number;
    }
    if (slaw_is_map(value)) {
        /* Check before allocating: some maps need native Slaw to retain meaning. */
        const char *previous_key = NULL;
        for (bslaw entry = slaw_list_emit_first(value); entry;
             entry = slaw_list_emit_next(value, entry)) {
            bslaw key = slaw_cons_emit_car(entry);
            if (!slaw_is_string(key) || !slaw_string_is_valid_utf8(key) ||
                (int64)strlen(slaw_string_emit(key)) != slaw_string_emit_length(key))
                return binary_value(document, value);
            const char *name = slaw_string_emit(key);
            if (previous_key && strcmp(previous_key, name) >= 0)
                return binary_value(document, value);
            previous_key = name;
        }
        if (slaw_map_count(value) == 2 && slaw_map_find_c(value, "type") &&
            slaw_map_find_c(value, "value"))
            return binary_value(document, value);
        yyjson_mut_val *object = yyjson_mut_obj(document);
        for (bslaw entry = slaw_list_emit_first(value); entry;
             entry = slaw_list_emit_next(value, entry)) {
            yyjson_mut_val *key = yyjson_mut_strcpy(document, slaw_string_emit(slaw_cons_emit_car(entry)));
            if (!yyjson_mut_obj_add(object, key,
                    encode(document, slaw_cons_emit_cdr(entry), depth + 1)))
                fail("out of memory");
        }
        return object;
    }
    if (slaw_is_list(value)) {
        yyjson_mut_val *array = yyjson_mut_arr(document);
        for (bslaw child = slaw_list_emit_first(value); child;
             child = slaw_list_emit_next(value, child)) {
            if (!yyjson_mut_arr_add_val(array, encode(document, child, depth + 1)))
                fail("out of memory");
        }
        return array;
    }
    return binary_value(document, value);
}

static const char *integer_string(yyjson_val *value)
{
    if (!yyjson_is_str(value))
        fail("integer value must be an integer or a decimal string");
    const char *text = yyjson_get_str(value);
    if (strlen(text) != yyjson_get_len(value))
        fail("invalid integer");
    const char *digit = text;
    if (*digit == '-')
        digit++;
    if (!*digit)
        fail("invalid integer");
    for (; *digit; digit++)
        if (*digit < '0' || *digit > '9')
            fail("invalid integer");
    return text;
}

static int64_t signed_integer(yyjson_val *value)
{
    if (yyjson_is_int(value)) {
        if (yyjson_is_uint(value) && yyjson_get_uint(value) > UINT64_C(9007199254740991))
            fail("use a type annotation with a decimal string for a large integer");
        int64_t n = yyjson_get_sint(value);
        if (n < -INT64_C(9007199254740991) || n > INT64_C(9007199254740991))
            fail("use a type annotation with a decimal string for a large integer");
        return n;
    }
    const char *text = integer_string(value);
    errno = 0;
    int64_t n = strtoll(text, NULL, 10);
    if (errno)
        fail("integer is outside the signed 64-bit range");
    return n;
}

static uint64_t unsigned_integer(yyjson_val *value)
{
    if (yyjson_is_int(value)) {
        if (yyjson_is_sint(value) && yyjson_get_sint(value) < 0)
            fail("unsigned integer cannot be negative");
        uint64_t n = yyjson_get_uint(value);
        if (n > UINT64_C(9007199254740991))
            fail("use a type annotation with a decimal string for a large integer");
        return n;
    }
    const char *text = integer_string(value);
    if (*text == '-')
        fail("unsigned integer cannot be negative");
    errno = 0;
    uint64_t n = strtoull(text, NULL, 10);
    if (errno)
        fail("integer is outside the unsigned 64-bit range");
    return n;
}

static slaw decode_annotation(yyjson_val *object)
{
    yyjson_val *type = field(object, "type");
    yyjson_val *value = field(object, "value");
    if (!yyjson_is_str(type) || strlen(yyjson_get_str(type)) != yyjson_get_len(type))
        fail("annotation type must be a string without NUL bytes");
    const char *name = yyjson_get_str(type);
    if (strcmp(name, "slaw") == 0) {
        if (!yyjson_is_str(value) || strlen(yyjson_get_str(value)) != yyjson_get_len(value))
            fail("binary Slaw value must be a hex string");
        return slaw_from_hex(yyjson_get_str(value));
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
        if (!yyjson_is_num(value))
            fail("float value must be a JSON number");
        double n = yyjson_get_num(value);
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
}

static slaw decode(yyjson_val *value, unsigned depth)
{
    if (depth > 64)
        fail("value nests too deeply");
    switch (yyjson_get_type(value)) {
    case YYJSON_TYPE_NULL:
        return slaw_nil();
    case YYJSON_TYPE_BOOL:
        return slaw_boolean(yyjson_get_bool(value));
    case YYJSON_TYPE_STR:
        return slaw_string_from_substring(yyjson_get_str(value), (int64)yyjson_get_len(value));
    case YYJSON_TYPE_NUM:
        return yyjson_is_real(value) ? slaw_float64(yyjson_get_real(value))
                                     : slaw_int64(signed_integer(value));
    case YYJSON_TYPE_ARR: {
        slabu *items = slabu_new();
        size_t index, count;
        yyjson_val *child;
        yyjson_arr_foreach(value, index, count, child)
            check(slabu_list_add_f(items, decode(child, depth + 1)));
        return slaw_list_f(items);
    }
    case YYJSON_TYPE_OBJ: {
        if (annotated(value))
            return decode_annotation(value);
        slabu *items = slabu_new();
        size_t index, count;
        yyjson_val *key, *child;
        yyjson_obj_foreach(value, index, count, key, child) {
            slaw native_key = slaw_string_from_substring(yyjson_get_str(key), (int64)yyjson_get_len(key));
            if (slabu_map_find(items, native_key, NULL))
                fail("duplicate JSON field");
            check(slabu_map_put_ff(items, native_key, decode(child, depth + 1)));
        }
        return slaw_map_f(items);
    }
    case YYJSON_TYPE_RAW:
        fail("number is too large; use a type annotation with a decimal string for integers");
    default:
        fail("unsupported JSON value");
    }
}

yyjson_mut_doc *export_protein(bprotein p)
{
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    if (!document)
        fail("out of memory");
    yyjson_mut_val *data = yyjson_mut_obj(document);
    int64 rude_length = 0;
    if (!protein_is_nonstandard(p))
        protein_rude(p, &rude_length);
    bslaw descrips = protein_is_nonstandard(p) ? NULL : protein_descrips(p);
    bslaw ingests = protein_is_nonstandard(p) ? NULL : protein_ingests(p);
    if (protein_is_nonstandard(p) || rude_length != 0 ||
        (descrips && !slaw_is_list(descrips)) ||
        (ingests && !slaw_is_list_or_map(ingests))) {
        if (!yyjson_mut_obj_add_val(document, data, "protein", binary_value(document, p)))
            fail("out of memory");
    } else {
        if (descrips && !yyjson_mut_obj_add_val(document, data, "descrips", encode(document, descrips, 0)))
            fail("out of memory");
        if (ingests && !yyjson_mut_obj_add_val(document, data, "ingests", encode(document, ingests, 0)))
            fail("out of memory");
    }
    yyjson_mut_val *message = yyjson_mut_obj(document);
    yyjson_mut_val *meta = yyjson_mut_obj(document);
    if (!yyjson_mut_obj_add_val(document, meta, "tags", yyjson_mut_arr(document)) ||
        !yyjson_mut_obj_add_val(document, message, "meta", meta) ||
        !yyjson_mut_obj_add_val(document, message, "data", data))
        fail("out of memory");
    yyjson_mut_doc_set_root(document, message);
    return document;
}

protein import_message(yyjson_val *message)
{
    yyjson_val *data = field(message, "data");
    if (!yyjson_is_obj(message) || !yyjson_is_obj(data))
        fail("expected a Plasmite message with an object data field");
    yyjson_val *value = field(data, "protein");
    if (value) {
        if (yyjson_obj_size(data) != 1 || !annotated(value))
            fail("data.protein must be the only data field and a Slaw annotation");
        slaw p = decode_annotation(value);
        if (!slaw_is_protein(p))
            fail("data.protein must contain a Protein");
        return p;
    }
    size_t index, count;
    yyjson_val *key, *child;
    yyjson_obj_foreach(data, index, count, key, child) {
        if (!yyjson_equals_str(key, "descrips") && !yyjson_equals_str(key, "ingests"))
            fail("data must contain only descrips and ingests");
    }
    value = field(data, "descrips");
    slaw descrips = value ? decode(value, 0) : NULL;
    value = field(data, "ingests");
    slaw ingests = value ? decode(value, 0) : NULL;
    if (descrips && !slaw_is_list(descrips))
        fail("descrips must be a list");
    if (ingests && !slaw_is_list_or_map(ingests))
        fail("ingests must be a list or map");
    protein p = protein_from_ff(descrips, ingests);
    if (!p)
        fail("cannot construct protein");
    return p;
}
