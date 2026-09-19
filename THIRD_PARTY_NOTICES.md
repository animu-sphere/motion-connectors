# Third-party notices

`motion-connectors` bundles no third-party code today, and its build links none
beyond OpenUSD (through `usd-motion-plugins`' `motionCore`) and the operating
system's sockets.

Each third-party dependency a connector adds is recorded here in the change
that adds it: its name, version, license, and what it is used for
([DEPENDENCIES.md §4](docs/architecture/DEPENDENCIES.md#4-per-connector-dependencies)).
A vendor SDK is declared in its connector's manifest, never discovered at build
time.
