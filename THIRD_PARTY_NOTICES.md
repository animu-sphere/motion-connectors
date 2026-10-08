# Third-party notices

`motion-connectors` bundles no third-party code today, and its build links none
beyond OpenUSD (through `usd-motion-plugins`' `motionCore`) and the operating
system's sockets in the default native build. The optional OpenXR loader is
documented in [its component notices](libs/motionConnectorOpenXR/THIRD_PARTY_NOTICES.md).
Browser runtime acquisition bundles no dependencies; WebXR declaration-check
tooling is documented in [its component notices](web/motionConnectorWebXR/THIRD_PARTY_NOTICES.md).

Each third-party dependency a connector adds is recorded here in the change
that adds it: its name, version, license, and what it is used for
([DEPENDENCIES.md §4](docs/architecture/DEPENDENCIES.md#4-per-connector-dependencies)).
A vendor SDK is declared in its connector's manifest, never discovered at build
time.
