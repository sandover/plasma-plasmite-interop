# Plasticine

Plasticine bridges live message streams between
[Plasma](https://github.com/plasma-hamper/plasma) pools and
[Plasmite](https://github.com/sandover/plasmite) pools.

It preserves Plasma's types, including binary values.

## Usage

Relay live messages from a Plasma pool into a Plasmite pool:

```bash
# Create the receiving pool and start the bridge.
plasmite pool create relay
plasticine from plasma-source | plasmite feed relay
```

The reverse...

```bash
# Create the receiving pool and start the bridge.
p-create plasma-relay
plasmite follow plasmite-source | plasticine to plasma-relay
```


- `from` waits for new Plasma messages and writes them to stdout. (It acts like plasma `peek`.)
- `to` reads Plasticine-format JSON messages from stdin and deposits them in a Plasma pool as they arrive (It acts like plasma `poke`.) Neither will create a pool.


## Installation

You need:

- Plasmite **1.2.1 or newer**, for the CLI commands in these examples.
- Plasma's C libraries, **libPlasma** and **libLoam**. Follow the
  [Plasma build instructions](https://github.com/plasma-hamper/plasma#building-the-thing);
  you can disable its C++ libraries.
- The **json-c** library and development headers.
- A POSIX system with a C11 compiler, Make, Git, and pkg-config.

Install json-c and pkg-config on macOS:

```bash
brew install json-c pkgconf
```

On Debian or Ubuntu:

```bash
sudo apt install build-essential git pkg-config libjson-c-dev
```

After installing the Plasma C libraries, build Plasticine:

```bash
git clone https://github.com/sandover/plasma-plasmite-interop.git
cd plasma-plasmite-interop
make
./plasticine --help
```

Run `./plasticine` from this directory, or copy the executable into a
directory on your PATH. For example, if `~/.local/bin` is on your PATH:

```bash
mkdir -p ~/.local/bin
install -m 755 plasticine ~/.local/bin/plasticine
```

If pkg-config cannot find Plasma in a custom installation:

```bash
PKG_CONFIG_PATH=/path/to/plasma/lib/pkgconfig make
```

The build links libPlasma, libLoam, and json-c. It does not need Plasmite's
C library.

## Message format

Plasticine writes one complete Plasmite JSON message per line and flushes
each line. `to` accepts that same format. Keep each input message on one
line; when using jq in a bridge, use `jq -c`.

Here is a message with its fields expanded for reading:

```json
{
  "meta": {"tags": []},
  "data": {
    "descrips": ["example"],
    "ingests": {
      "bar": {"type": "unt8", "value": 5},
      "foo": 32
    }
  }
}
```

`data.descrips` holds Plasma's descriptors; `data.ingests` holds its
contents. Descriptors must decode to a Plasma list, and contents must
decode to a list or map. Omit either field to leave it absent.
`to` rejects other fields inside `data`, apart from the whole-Protein
form below. It expects this format rather than arbitrary application JSON.

To create a message with Plasmite's default feed syntax and send it to Plasma:

```bash
plasmite feed events '{"descrips":["example"],"ingests":{"bar":{"type":"unt8","value":5},"foo":32}}' |
  plasticine to plasma-relay
```

Use an existing `events` pool and `plasma-relay` pool for this example.
`foo` becomes `slaw_int64(32)` and `bar` becomes `slaw_unt8(5)`.

Plasticine emits empty Plasmite tag lists. When importing, it ignores
Plasmite's tags, sequence, and timestamp. Plasma descriptors stay in
`data.descrips`; each destination pool assigns a new sequence and time.

## Type preservation

| JSON value | Plasma type |
| --- | --- |
| null, boolean, string | nil, boolean, string |
| array | list |
| object | map with string keys |
| integer | int64 |
| number with a decimal point or exponent | float64 |
| `{"type":"unt8","value":5}` | unt8 |

An object containing exactly `type` and `value` specifies a Plasma type.
Number annotations accept `int8`, `int16`, `int32`, `int64`, `unt8`,
`unt16`, `unt32`, `unt64`, `float32`, and `float64`. Plasticine checks
integer ranges before calling Plasma's constructors. Float annotations
accept finite JSON numbers; float32 follows C's rounding from a double.

Integers outside -9007199254740991 through 9007199254740991 require a
decimal string in a type annotation. This keeps them exact when JSON passes
through programs that use JavaScript's number representation:

```json
{"type":"unt64","value":"18446744073709551615"}
```

For other types, Plasticine uses a hex-encoded Plasma binary file:

```json
{"type":"slaw","value":"...hex bytes..."}
```

libPlasma writes and reads the file, including its version and byte-order
header. This carries every type the linked libPlasma can write and read:
vectors, multivectors, numeric arrays, complex numbers, cons cells, nested
Proteins, arbitrary map keys, NaN, infinity, and strings with NUL bytes or
invalid UTF-8. Plasticine also uses binary Slaw for deeply nested values,
maps whose entry order JSON would change, and literal maps with exactly
`type` and `value` as keys.

A Protein with a binary payload (Plasma's "rude data") or a nonstandard
layout travels as a whole:

```json
{"meta":{"tags":[]},"data":{"protein":{"type":"slaw","value":"...hex bytes..."}}}
```

`data.protein` must be the only field in `data` and must contain a Protein.
You can also use a binary Slaw annotation for any value inside `descrips`
or `ingests`. Keep these annotations intact when editing a stream.

## Delivery and errors

`from` keeps waiting for new messages. `to` exits successfully when stdin
closes. Errors go to stderr; a reported fatal error exits with status 1.
Successful completion and `--help` exit with status 0.

Plasticine stops at the first error. Earlier deposits remain in the
destination pool. It keeps no saved read position and performs no retries;
restarting a live bridge begins with new messages. Messages that arrive
while the bridge is stopped will not replay on restart.

Use separate source and destination streams for each direction. Connecting
the same two pools in both directions creates a feedback loop: Plasticine
does not suppress repeated messages.

Each message must fit its destination pool. Hex uses two characters per
binary byte, so it increases message size. Plasticine's stdin and stdout
format is JSON; use Plasmite's default JSON output when piping into `to`.

In Bash scripts, use `set -o pipefail` to catch a failure anywhere in a
pipeline.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| pkg-config cannot find libPlasma or libLoam | Install both C libraries and set `PKG_CONFIG_PATH` to their `lib/pkgconfig` directory. |
| `from` produces no output | It waits for new messages. Publish after starting the bridge. |
| `to` rejects a message | Check that `data` contains descriptors and contents, or a whole Protein, and keep one complete JSON message per line. |

`to` rejects unknown annotations, non-finite JSON numbers, invalid JSON,
and NUL escapes. Carry NUL bytes inside binary Slaw; json-c otherwise
truncates object keys at NUL.

[Report an issue](https://github.com/sandover/plasma-plasmite-interop/issues)
with the command, error output, Plasmite version, and an example message.

## Testing

```bash
make test
```

The C tests check both conversion directions for all 120 numeric value
kinds and their 120 array kinds, including empty arrays. They also check
booleans, nil, strings, lists, maps, cons cells, and Proteins, plus large
integers, special float values, map order, unusual strings, binary payloads,
and rejected inputs.

The tests compare native Plasma values and types, check independently
written JSON examples, and use libPlasma's file writer as the reference for
binary annotations. They need the build dependencies above, with no running
pools or extra test framework.

## License

MIT. See [LICENSE](LICENSE).
