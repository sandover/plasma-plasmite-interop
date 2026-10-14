# Plasticine

A small C tool for bridging live messages between
[Plasma](https://github.com/plasma-hamper/plasma) and
[Plasmite](https://github.com/sandover/plasmite).

Uses libPlasma to read, construct, and deposit Proteins, and json-c to
read and write JSON. 

Requires Plasmite 1.2 or newer.

## Build

Install Plasma's C libraries, json-c, a C compiler, Make, and pkg-config.
Then:

```bash
make
```

For json-c and pkg-config on macOS: `brew install json-c pkgconf`.
On Debian/Ubuntu: `sudo apt install build-essential pkg-config libjson-c-dev`.
Follow [Plasma's build instructions](https://github.com/plasma-hamper/plasma#building-the-thing)
for libPlasma; its C++ libraries are optional.

If Plasma lives outside the usual search paths:

```bash
PKG_CONFIG_PATH=/path/to/plasma/lib/pkgconfig make
```

## Pipes

To relay a Plasma message stream into a Plasmite pool:

```bash
plasmite pool create plasmite-destination
plasticine from plasma-source | plasmite feed plasmite-destination
```

To relay a Plasmite message stream into a Plasma pool:

```bash
plasmite follow plasmite-source | plasticine to plasma-destination
```

`from` waits for new Proteins and writes one complete Plasmite JSON message
per line, flushing each line. `to` reads one message per line from stdin
and deposits each Protein in Plasma. It writes no stdout. Errors go to stderr.
Each destination pool assigns its own position and time.

Avoid connecting the same two pools in a loop: Plasticine does not deduplicate
messages or suppress echoes.

## What the JSON means

A bridge message keeps Plasma's descriptors and contents inside `data`:

```json
{"meta":{"tags":[]},"data":{"descrips":["example"],"ingests":{"foo":32,"bar":{"type":"unt8","value":5}}}}
```

Here `foo` becomes `slaw_int64(32)` and `bar` becomes `slaw_unt8(5)`.
You can create that message in Plasmite and send it to Plasma:

```bash
plasmite feed relay --data '{"descrips":["example"],"ingests":{"foo":32,"bar":{"type":"unt8","value":5}}}' |
  plasticine to plasma-destination
```

`plasticine to` expects this bridge shape. It rejects other fields in `data`.
It ignores Plasmite's tags, position, and time; Plasma descriptors live in
`data.descrips`. An omitted `descrips` or `ingests` means an absent Plasma
field. Present descriptors must decode to a list; present contents must
decode to a list or map. JSON null represents Slaw nil inside those values.

| JSON | Plasma |
| --- | --- |
| null, boolean, string | nil, boolean, string |
| array | list |
| object | map with string keys |
| integer | int64 |
| number with a decimal point or exponent | float64 |
| `{"type":"unt8","value":5}` | unt8 |

Annotations support `int8`, `int16`, `int32`, `int64`, `unt8`, `unt16`,
`unt32`, `unt64`, `float32`, and `float64`. Integer annotations also accept
decimal strings. Integers outside JavaScript's exact integer range
(-9007199254740991 through 9007199254740991) require a string annotation,
for example:

```json
{"type":"unt64","value":"18446744073709551615"}
```

`plasticine to` checks integer ranges before calling Plasma's constructors.
Float annotations accept JSON numbers; float32 follows C's rounding when
converting from a double. `plasticine from` preserves scalar number types using
annotations where the plain JSON defaults would change the type.

## All Plasma types

An object containing exactly `type` and `value` means an annotation.
For every other Plasma type, `plasticine from` uses:

```json
{"type":"slaw","value":"...hex bytes..."}
```

The hex contains a complete single-value Plasma binary file, including
Plasma's version and byte-order header. libPlasma writes and reads it.
This covers vectors, multivectors, numeric arrays, complex numbers, cons
cells, nested Proteins, maps with arbitrary keys, NaN, infinity, strings
with NUL bytes or invalid UTF-8, and deeply nested values. A literal map
with exactly `type` and `value` also uses this form, so it keeps its meaning.
Plasmite sorts JSON object keys. A Plasma map whose entries follow a
different order uses binary Slaw to preserve that order.

A Protein with rude data or a nonstandard layout travels as a whole:

```json
{"meta":{"tags":[]},"data":{"protein":{"type":"slaw","value":"...hex bytes..."}}}
```

`data.protein` must contain a Protein and must be the only field in `data`.
You can also place a binary Slaw annotation wherever a value belongs in
`descrips` or `ingests`. This carries every type the linked libPlasma can
write and read. The readable JSON form preserves values and types; the
binary form delegates preservation to Plasma's own format.

## Sketch boundaries

`plasticine to` rejects unknown annotations, out-of-range numbers, invalid
JSON, and NUL escapes in JSON strings. Use binary Slaw for NUL bytes;
json-c would otherwise truncate object keys. Plasticine stops at the first
error; earlier deposits remain. It has no retry service or test suite.

The source stays small: one command file, a readable JSON converter, a
binary Slaw helper, and a Makefile. The intended next step is to try it
together and agree on the conversion rules.
