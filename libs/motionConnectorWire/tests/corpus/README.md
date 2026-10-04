# `openstrata.motion.frame/v1` message corpus

One message per `*.frame.json` file, written by
[`tools/generate_messages.py`](../../tools/generate_messages.py). Nothing here is
captured from a sender, so there is no redistribution gate. Do not edit a
message by hand: change the generator and rerun it.
`motionConnectorWire_messageGen` fails when a committed file no longer matches
the generator.

[`expectations.txt`](expectations.txt) has one line per message, with the
reason it exists on the comment line above it:

| Kind | Files | `motionConnectorWire_corpus` checks |
| --- | --- | --- |
| `canonical` | `<name>.frame.json` | it decodes, and re-encoding writes the same bytes: the C++ writer and the generator agree on §4's spelling |
| `accepted` | `<name>.frame.json` | it decodes; the writer would spell it differently (key order, whitespace, escapes, exponents) |
| `refused` | `refused-<name>.frame.json` | the reader refuses it with that code and that subject |

Every refused message is a canonical one with exactly one thing wrong. Together
they cover each refusal in
[FRAME_WIRE_FORMAT §6](../../../../docs/design/FRAME_WIRE_FORMAT.md#6-what-a-reader-refuses).
Some are deliberately not UTF-8, so `.gitattributes` marks `*.frame.json`
binary.

The size bound is not here: a message over the default 1 MiB would be a
mebibyte of fixture. `motionConnectorWire_frameWire` checks it against a
bound the test chooses.
