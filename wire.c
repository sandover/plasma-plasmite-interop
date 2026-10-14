#include "interop.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Let libPlasma write and read its own versioned binary file format in memory. */
struct buffer {
    byte *data;
    size_t length;
    size_t position;
};

static ob_retort write_bytes(void *cookie, const byte *data, size_t length)
{
    struct buffer *buffer = cookie;
    if (length > SIZE_MAX - buffer->length)
        return OB_NO_MEM;
    byte *grown = realloc(buffer->data, buffer->length + length);
    if (!grown)
        return OB_NO_MEM;
    buffer->data = grown;
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    return OB_OK;
}

static ob_retort read_bytes(void *cookie, byte *data, size_t wanted, size_t *read)
{
    struct buffer *buffer = cookie;
    size_t available = buffer->length - buffer->position;
    *read = wanted < available ? wanted : available;
    memcpy(data, buffer->data + buffer->position, *read);
    buffer->position += *read;
    return OB_OK;
}

static ob_retort finish(void *cookie)
{
    (void)cookie;
    return OB_OK; /* The caller owns the buffer. */
}

char *slaw_to_hex(bslaw value)
{
    struct buffer buffer = {0};
    slaw_write_handler handler = {write_bytes, finish, finish, &buffer};
    slaw_output output;
    check(slaw_output_open_binary_handler(handler, NULL, &output));
    check(slaw_output_write(output, value));
    check(slaw_output_close(output));

    if (buffer.length > (SIZE_MAX - 1) / 2)
        fail("binary value is too large");
    char *hex = malloc(2 * buffer.length + 1);
    if (!hex)
        fail("out of memory");
    const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < buffer.length; i++) {
        hex[2 * i] = digits[buffer.data[i] >> 4];
        hex[2 * i + 1] = digits[buffer.data[i] & 15];
    }
    hex[2 * buffer.length] = '\0';
    free(buffer.data);
    return hex;
}

static unsigned hex_digit(char c)
{
    if (c >= '0' && c <= '9') return (unsigned)(c - '0');
    if (c >= 'a' && c <= 'f') return (unsigned)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (unsigned)(c - 'A') + 10;
    fail("invalid hex digit in binary Slaw");
    return 0;
}

slaw slaw_from_hex(const char *hex)
{
    size_t length = strlen(hex);
    if (length < 16 || length % 2 != 0)
        fail("expected a hex-encoded Plasma binary file");
    struct buffer buffer = {.length = length / 2};
    buffer.data = malloc(buffer.length);
    if (!buffer.data)
        fail("out of memory");
    for (size_t i = 0; i < buffer.length; i++)
        buffer.data[i] = (byte)((hex_digit(hex[2 * i]) << 4) | hex_digit(hex[2 * i + 1]));

    slaw_read_handler handler = {read_bytes, finish, &buffer};
    slaw_input input;
    check(slaw_input_open_binary_handler(handler, &input));
    slaw value;
    check(slaw_input_read(input, &value));
    if (buffer.position != buffer.length)
        fail("binary annotation must contain exactly one Slaw");
    check(slaw_input_close(input));
    free(buffer.data);
    return value;
}
