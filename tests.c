#include "interop.h"
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned checks;
static unsigned numeric_types;

static void expect(int condition, const char *name, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s: %s\n", name, message);
        exit(1);
    }
}

static yyjson_doc *parse(const char *text)
{
    return read_json(text, strlen(text));
}

static yyjson_doc *through_json(yyjson_mut_doc *message)
{
    char *text = write_json(message);
    yyjson_doc *received = parse(text);
    free(text);
    return received;
}

static protein wrap_native(bslaw value)
{
    return protein_from_ff(slaw_list_inline_c("type-test", NULL),
                           slaw_map_inline_cl("v", value, NULL));
}

static yyjson_mut_doc *new_document(void)
{
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    expect(document != NULL, "JSON", "cannot allocate document");
    return document;
}

static void wrap_json(yyjson_mut_doc *document, yyjson_mut_val *value)
{
    yyjson_mut_val *message = yyjson_mut_obj(document);
    yyjson_mut_val *data = yyjson_mut_obj(document);
    yyjson_mut_val *descrips = yyjson_mut_arr(document);
    yyjson_mut_val *ingests = yyjson_mut_obj(document);
    expect(value && yyjson_mut_arr_add_str(document, descrips, "type-test") &&
           yyjson_mut_obj_add_val(document, ingests, "v", value) &&
           yyjson_mut_obj_add_val(document, data, "descrips", descrips) &&
           yyjson_mut_obj_add_val(document, data, "ingests", ingests) &&
           yyjson_mut_obj_add_val(document, message, "data", data),
           "JSON", "cannot construct message");
    yyjson_mut_doc_set_root(document, message);
}

/* Create input independently with libPlasma's file writer, not wire.c. */
static yyjson_mut_val *native_annotation(yyjson_mut_doc *document, bslaw value)
{
    FILE *file = tmpfile();
    expect(file != NULL, "binary fixture", "cannot create temporary file");
    slaw_output output;
    check(slaw_output_open_binary_z(file, &output));
    check(slaw_output_write(output, value));
    check(slaw_output_close(output));
    expect(fseek(file, 0, SEEK_END) == 0, "binary fixture", "cannot seek");
    long length = ftell(file);
    expect(length > 0, "binary fixture", "cannot get file length");
    rewind(file);
    char *hex = malloc((size_t)length * 2 + 1);
    expect(hex != NULL, "binary fixture", "cannot allocate hex string");
    for (long i = 0; i < length; i++) {
        int byte = fgetc(file);
        expect(byte != EOF, "binary fixture", "file ended early");
        snprintf(hex + i * 2, 3, "%02x", (unsigned)(unsigned char)byte);
    }
    expect(fclose(file) == 0, "binary fixture", "cannot close file");
    yyjson_mut_val *annotation = yyjson_mut_obj(document);
    expect(yyjson_mut_obj_add_str(document, annotation, "type", "slaw") &&
           yyjson_mut_obj_add_strcpy(document, annotation, "value", hex),
           "binary fixture", "cannot construct annotation");
    free(hex);
    return annotation;
}

static void compare_message(const char *name, bprotein expected,
                            yyjson_mut_doc *message)
{
    yyjson_doc *received = through_json(message);
    protein actual = import_message(yyjson_doc_get_root(received));
    expect(proteins_equal(expected, actual), name, "Plasma type or value changed");
    protein_free(actual);
    yyjson_doc_free(received);
    checks++;
}

static void native_case(const char *name, slaw value)
{
    expect(value != NULL, name, "cannot construct Plasma value");
    protein expected = wrap_native(value);
    expect(expected != NULL, name, "cannot construct Protein");

    /* Plasma -> JSON text -> Plasma, with exact native equality. */
    yyjson_mut_doc *exported = export_protein(expected);
    compare_message(name, expected, exported);
    yyjson_mut_doc_free(exported);

    /* Independently authored binary JSON -> Plasma -> JSON -> Plasma. */
    yyjson_mut_doc *input = new_document();
    wrap_json(input, native_annotation(input, value));
    yyjson_doc *received = through_json(input);
    protein imported = import_message(yyjson_doc_get_root(received));
    expect(proteins_equal(expected, imported), name, "binary JSON imported the wrong type or value");
    exported = export_protein(imported);
    compare_message(name, expected, exported);
    yyjson_mut_doc_free(exported);
    protein_free(imported);
    yyjson_doc_free(received);
    yyjson_mut_doc_free(input);
    protein_free(expected);
    slaw_free(value);
}

static void binary_case(const char *name, slaw value)
{
    protein source = wrap_native(value);
    yyjson_mut_doc *message = export_protein(source);
    yyjson_mut_val *data = yyjson_mut_obj_get(yyjson_mut_doc_get_root(message), "data");
    yyjson_mut_val *ingests = yyjson_mut_obj_get(data, "ingests");
    yyjson_mut_val *encoded = yyjson_mut_obj_get(ingests, "v");
    expect(encoded != NULL, name, "missing exported value");
    yyjson_mut_doc *reference = new_document();
    yyjson_mut_val *expected = native_annotation(reference, value);
    expect(yyjson_mut_equals(encoded, expected), name, "binary JSON differs from libPlasma's file format");
    yyjson_mut_doc_free(reference);
    yyjson_mut_doc_free(message);
    protein_free(source);
    checks++;
    native_case(name, value);
}

static void json_case(const char *name, const char *text, slaw value)
{
    protein expected = wrap_native(value);
    yyjson_doc *example = parse(text);
    yyjson_mut_doc *input = new_document();
    wrap_json(input, yyjson_val_mut_copy(input, yyjson_doc_get_root(example)));
    yyjson_doc_free(example);
    yyjson_doc *received = through_json(input);
    protein actual = import_message(yyjson_doc_get_root(received));
    expect(proteins_equal(expected, actual), name, "JSON imported the wrong type or value");
    checks++;
    yyjson_mut_doc *exported = export_protein(actual);
    compare_message(name, expected, exported);
    yyjson_mut_doc_free(exported);
    yyjson_doc_free(received);
    yyjson_mut_doc_free(input);
    protein_free(actual);
    protein_free(expected);
    slaw_free(value);
}

static void protein_case(const char *name, protein value)
{
    yyjson_mut_doc *exported = export_protein(value);
    compare_message(name, value, exported);
    yyjson_mut_doc_free(exported);
    yyjson_mut_doc *input = new_document();
    yyjson_mut_val *data = yyjson_mut_obj(input);
    yyjson_mut_val *message = yyjson_mut_obj(input);
    expect(yyjson_mut_obj_add_val(input, data, "protein", native_annotation(input, value)) &&
           yyjson_mut_obj_add_val(input, message, "data", data), name, "cannot construct message");
    yyjson_mut_doc_set_root(input, message);
    compare_message(name, value, input);
    yyjson_mut_doc_free(input);
    protein_free(value);
}

/* Every SLAWBEGET family in Plasma's public numeric header, including m5. */
#define NUMERIC_CASE(type, component) do { \
    type pair[2]; \
    component lanes[2 * sizeof(type) / sizeof(component)]; \
    _Static_assert(sizeof(lanes) == sizeof(pair), "numeric components must fill the value"); \
    for (size_t i = 0; i < sizeof(lanes) / sizeof(lanes[0]); i++) \
        lanes[i] = (component)(i + 1); \
    memcpy(pair, lanes, sizeof(pair)); \
    if (sizeof(type) == sizeof(component)) \
        native_case(#type, slaw_##type(pair[0])); \
    else \
        binary_case(#type, slaw_##type(pair[0])); \
    binary_case(#type " array", slaw_##type##_array(pair, 2)); \
    binary_case(#type " empty array", slaw_##type##_array_empty(0)); \
    numeric_types++; \
} while (0)

#define NUMERIC_FAMILIES(base) do { \
    NUMERIC_CASE(base, base); \
    NUMERIC_CASE(base##c, base); \
    NUMERIC_CASE(v2##base, base); \
    NUMERIC_CASE(v3##base, base); \
    NUMERIC_CASE(v4##base, base); \
    NUMERIC_CASE(v2##base##c, base); \
    NUMERIC_CASE(v3##base##c, base); \
    NUMERIC_CASE(v4##base##c, base); \
    NUMERIC_CASE(m2##base, base); \
    NUMERIC_CASE(m3##base, base); \
    NUMERIC_CASE(m4##base, base); \
    NUMERIC_CASE(m5##base, base); \
} while (0)

static void all_numeric_types(void)
{
    NUMERIC_FAMILIES(int8);
    NUMERIC_FAMILIES(unt8);
    NUMERIC_FAMILIES(int16);
    NUMERIC_FAMILIES(unt16);
    NUMERIC_FAMILIES(int32);
    NUMERIC_FAMILIES(unt32);
    NUMERIC_FAMILIES(int64);
    NUMERIC_FAMILIES(unt64);
    NUMERIC_FAMILIES(float32);
    NUMERIC_FAMILIES(float64);
    expect(numeric_types == 120, "numeric coverage", "unexpected number of numeric types");
}

static void plain_json_and_annotations(void)
{
    json_case("nil", "null", slaw_nil());
    json_case("false", "false", slaw_boolean(false));
    json_case("true", "true", slaw_boolean(true));
    json_case("string", "\"hello \\u03bb\"", slaw_string("hello \xce\xbb"));
    json_case("int64 default", "32", slaw_int64(32));
    json_case("float64 default", "1.25", slaw_float64(1.25));
    json_case("float64 exponent", "1e3", slaw_float64(1000));
    json_case("int8 annotation", "{\"type\":\"int8\",\"value\":-3}", slaw_int8(-3));
    json_case("unt8 annotation", "{\"type\":\"unt8\",\"value\":5}", slaw_unt8(5));
    json_case("int16 annotation", "{\"type\":\"int16\",\"value\":-300}", slaw_int16(-300));
    json_case("unt16 annotation", "{\"type\":\"unt16\",\"value\":60000}", slaw_unt16(60000));
    json_case("int32 annotation", "{\"type\":\"int32\",\"value\":-70000}", slaw_int32(-70000));
    json_case("unt32 annotation", "{\"type\":\"unt32\",\"value\":4000000000}", slaw_unt32(UINT32_C(4000000000)));
    json_case("int64 annotation", "{\"type\":\"int64\",\"value\":\"-9223372036854775808\"}", slaw_int64(INT64_MIN));
    json_case("unt64 annotation", "{\"type\":\"unt64\",\"value\":\"18446744073709551615\"}", slaw_unt64(UINT64_MAX));
    json_case("float32 annotation", "{\"type\":\"float32\",\"value\":1.25}", slaw_float32(1.25f));
    json_case("float64 annotation", "{\"type\":\"float64\",\"value\":1.25}", slaw_float64(1.25));
    json_case("empty list", "[]", slaw_list_empty());
    json_case("empty map", "{}", slaw_map_empty());
    json_case("list", "[null,true,32,\"text\"]",
              slaw_list_inline_f(slaw_nil(), slaw_boolean(true), slaw_int64(32), slaw_string("text"), NULL));
    json_case("map", "{\"a\":32,\"b\":true}",
              slaw_map_inline_cf("a", slaw_int64(32), "b", slaw_boolean(true), NULL));
}

static void special_values(void)
{
    native_case("int64 minimum", slaw_int64(INT64_MIN));
    native_case("int64 maximum", slaw_int64(INT64_MAX));
    native_case("unt64 maximum", slaw_unt64(UINT64_MAX));
    native_case("float32 maximum", slaw_float32(FLT_MAX));
    native_case("float64 maximum", slaw_float64(DBL_MAX));
    native_case("float32 negative zero", slaw_float32(-0.0f));
    native_case("float64 negative zero", slaw_float64(-0.0));
    binary_case("float32 NaN", slaw_float32(NAN));
    binary_case("float64 NaN", slaw_float64(NAN));
    binary_case("float32 infinity", slaw_float32(INFINITY));
    binary_case("float64 negative infinity", slaw_float64(-INFINITY));
    native_case("empty string", slaw_string(""));
    binary_case("NUL string", slaw_string_from_substring("a\0b", 3));
    binary_case("invalid UTF-8", slaw_string_from_substring("\xff", 1));
    binary_case("cons", slaw_cons_ff(slaw_string("left"), slaw_int8(-7)));
    binary_case("map with numeric key", slaw_map_inline_ff(slaw_unt8(5), slaw_string("five"), NULL));
    binary_case("map with NUL key", slaw_map_inline_ff(slaw_string_from_substring("a\0b", 3), slaw_int64(9), NULL));
    binary_case("unsorted map", slaw_map_inline_cf("z", slaw_int64(1), "a", slaw_int64(2), NULL));
    binary_case("literal annotation map", slaw_map_inline_cf("type", slaw_string("literal"), "value", slaw_int64(5), NULL));
    binary_case("nested Protein", protein_from_ff(slaw_list_inline_c("nested", NULL), slaw_map_empty()));
    slaw nested = slaw_int64(7);
    for (int i = 0; i < 80; i++)
        nested = slaw_list_inline_f(nested, NULL);
    native_case("deep nesting", nested);
    protein_case("empty Protein", protein_from(NULL, NULL));
    protein_case("absent descriptors", protein_from_ff(NULL, slaw_map_empty()));
    protein_case("absent ingests", protein_from_ff(slaw_list_empty(), NULL));
    const byte bytes[] = {0, 1, 2, 127, 128, 254, 255, 0, 9, 10, 11, 12};
    protein_case("inline rude data", protein_from_llr(NULL, NULL, bytes, 3));
    protein_case("out-of-line rude data", protein_from_llr(NULL, NULL, bytes, sizeof bytes));
}

static void rejected_bytes(const char *name, const char *text, size_t length)
{
    pid_t child = fork();
    expect(child >= 0, name, "cannot fork");
    if (child == 0) {
        if (!freopen("/dev/null", "w", stderr))
            _Exit(2);
        yyjson_doc *input = read_json(text, length);
        protein p = import_message(yyjson_doc_get_root(input));
        yyjson_doc_free(input);
        protein_free(p);
        _Exit(0);
    }
    int status;
    expect(waitpid(child, &status, 0) == child, name, "cannot wait for child");
    expect(WIFEXITED(status) && WEXITSTATUS(status) == 1, name, "invalid input was not rejected cleanly");
    checks++;
}

static void rejected_input(const char *name, const char *text)
{
    rejected_bytes(name, text, strlen(text));
}

static void document_ownership(void)
{
    slaw value = slaw_map_inline_cf("owned-key", slaw_string("owned-value"), NULL);
    protein source = wrap_native(value);
    slaw_free(value);
    protein expected = protein_dup(source);
    yyjson_mut_doc *message = export_protein(source);
    protein_free(source);
    compare_message("exported document owns its strings", expected, message);
    yyjson_mut_doc_free(message);
    protein_free(expected);
}

static void parser_cases(void)
{
    json_case("JSON NUL string", "\"a\\u0000b\"", slaw_string_from_substring("a\0b", 3));
    json_case("JSON NUL key", "{\"a\\u0000b\":9}",
              slaw_map_inline_ff(slaw_string_from_substring("a\0b", 3), slaw_int64(9), NULL));
    json_case("JSON surrogate pair", "\"\\ud83d\\ude00\"", slaw_string("\xf0\x9f\x98\x80"));
    json_case("literal NUL escape text", "\"\\\\u0000\"", slaw_string("\\u0000"));
    native_case("float32 smallest subnormal", slaw_float32(FLT_TRUE_MIN));
    native_case("float64 smallest subnormal", slaw_float64(DBL_TRUE_MIN));
    rejected_input("oversized integer literal", "{\"data\":{\"ingests\":{\"v\":18446744073709551616}}}");
    rejected_input("duplicate data", "{\"data\":{},\"data\":{}}");
    rejected_input("duplicate contents", "{\"data\":{\"ingests\":{},\"ingests\":{}}}");
    rejected_input("duplicate map key", "{\"data\":{\"ingests\":{\"v\":{\"a\":1,\"a\":2}}}}");
    rejected_input("NUL in field name", "{\"data\":{\"descrips\\u0000extra\":[]}}");
    rejected_input("NUL in annotation type", "{\"data\":{\"ingests\":{\"v\":{\"type\":\"unt8\\u0000extra\",\"value\":5}}}}");
    rejected_input("invalid UTF-8 JSON", "{\"data\":{\"ingests\":{\"v\":\"\xff\"}}}");
    rejected_input("invalid surrogate", "{\"data\":{\"ingests\":{\"v\":\"\\ud800\"}}}");
    rejected_input("two messages on one line", "{\"data\":{}} {\"data\":{}}");
    const char raw_nul[] = "{\"data\":{}}\0";
    rejected_bytes("raw NUL in JSON", raw_nul, sizeof raw_nul - 1);
    document_ownership();
}

int main(void)
{
    all_numeric_types();
    plain_json_and_annotations();
    special_values();
    parser_cases();
    rejected_input("unt8 overflow", "{\"data\":{\"ingests\":{\"v\":{\"type\":\"unt8\",\"value\":256}}}}");
    rejected_input("unsigned negative", "{\"data\":{\"ingests\":{\"v\":{\"type\":\"unt8\",\"value\":-1}}}}");
    rejected_input("int64 overflow", "{\"data\":{\"ingests\":{\"v\":{\"type\":\"int64\",\"value\":\"9223372036854775808\"}}}}");
    rejected_input("unt64 overflow", "{\"data\":{\"ingests\":{\"v\":{\"type\":\"unt64\",\"value\":\"18446744073709551616\"}}}}");
    rejected_input("unknown type", "{\"data\":{\"ingests\":{\"v\":{\"type\":\"unknown\",\"value\":5}}}}");
    rejected_input("malformed binary", "{\"data\":{\"protein\":{\"type\":\"slaw\",\"value\":\"not hex\"}}}");
    rejected_input("scalar descriptors", "{\"data\":{\"descrips\":32}}");
    printf("PASS: %u numeric types and their nonempty and empty arrays; %u checks in both directions.\n", numeric_types, checks);
    return 0;
}
