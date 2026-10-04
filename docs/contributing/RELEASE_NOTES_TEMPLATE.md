# motion-connectors {tag}

How external motion enters the OpenUSD ecosystem: live connectors that decode
a source's own protocol into `usd-motion-plugins`' motion values behind one
connector contract. Every consumer reaches this repository only through
installed packages.

- **Contracts:** [CONNECTOR_CONTRACT.md](https://github.com/animu-sphere/motion-connectors/blob/{tag}/docs/design/CONNECTOR_CONTRACT.md)
  · [COORDINATE_SYSTEMS.md](https://github.com/animu-sphere/motion-connectors/blob/{tag}/docs/design/COORDINATE_SYSTEMS.md)
  · [SOURCE_PROFILES.md](https://github.com/animu-sphere/motion-connectors/blob/{tag}/docs/design/SOURCE_PROFILES.md)
- **Capability matrix:** [CAPABILITY_MATRIX.md](https://github.com/animu-sphere/motion-connectors/blob/{tag}/docs/reference/CAPABILITY_MATRIX.md)
- **Workspace layout:** [WORKSPACE.md](https://github.com/animu-sphere/motion-connectors/blob/{tag}/docs/architecture/WORKSPACE.md)
- **Building:** [building.md](https://github.com/animu-sphere/motion-connectors/blob/{tag}/docs/guides/building.md)

{changelog}

## Consuming a library from another repository

Every library is published twice: as a `.tar.zst` asset below, and as a
digest-pinned OpenStrata artifact in one OCI repository,
`ghcr.io/animu-sphere/motion-connectors`, tagged `<library>-{version}-<target>`.
A consumer declares the dependency under `requires.libraries` in its own
descriptor and pins the archive digest per target. The table below is
generated from this release's published artifacts, so it can be pasted as it
stands:

{pins}

`ost library pull --target <platform> --profile <profile>` then materializes
a library, and generated CI runs that pull before it builds. The archive must
match the consumer's target **and** the exact OpenUSD runtime identity:
everything here is built against the one OpenUSD release the ecosystem pins
([DEPENDENCIES.md](https://github.com/animu-sphere/motion-connectors/blob/{tag}/docs/architecture/DEPENDENCIES.md)).

## Artifacts

| artifact | contents |
| --- | --- |
| `<library>-{version}-<target>.tar.zst` | one installed library package for `<target>`: `motionConnectorTransport`, `motionConnectorOsc`, `motionConnectorCore`, `motionConnectorTracking`, `motionConnectorVmc`, `motionConnectorMocopi`, `motionConnectorVrchatOsc`; a connector's source profile is inside its archive |
| `<library>-{version}-<target>.manifest.json` | OpenStrata manifest sidecar per library, carrying its dependency closure, checksums and SBOM |
| `motion-connectors-{version}-src.tar.gz` | source archive at this tag, including the `motion_connect`, `vmc_record`, `mocopi_record` and `vrchat_osc_record` CLIs |
| `SHA256SUMS` | SHA-256 checksums of every file above |
| `external-library-pins.md` / `.json` | the pin table above, as a file |

The CLIs are not yet published as artifacts: `ost` packages a workspace tool
only alongside a plugin bundle, and this repository has none
([WORKSPACE.md §4.1](https://github.com/animu-sphere/motion-connectors/blob/{tag}/docs/architecture/WORKSPACE.md#41-distribution)).

## SHA-256 checksums

```text
{checksums}
```
