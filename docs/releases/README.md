# Release records

Each released version gets one record here: what it set out to do, what it
shipped, what it is compatible with, and what it does not do. A record is
history: once its version is released it is not rewritten, and a later
finding goes into a new record or a dated report. Incomplete work lives in
the [roadmap](../roadmap/README.md), with intended milestones in its
[scope table](../roadmap/README.md#status-at-a-glance).
Current release availability lives only in the
[capability matrix](../reference/CAPABILITY_MATRIX.md).

| Version | Record | Theme |
| --- | --- | --- |
| v0.1.0 | [v0.1.0.md](v0.1.0.md) | The first release: VMC, mocopi and VRChat OSC behind one connector contract, as seven digest-pinned libraries |

## How a release is cut

1. On a branch: set `VERSION` and every manifest that mirrors it, including
   the ranges the manifests require. `scripts/check_docs.py` fails until they
   agree. Move the changelog's `[Unreleased]` entries under
   `## [X.Y.Z] - YYYY-MM-DD`, write the record here, and take the shipped
   scope out of the [roadmap](../roadmap/README.md#status-at-a-glance). Promote
   only the shipped matrix rows from `unreleased` to `vX.Y.Z`; the docs check
   requires that release record and finalized changelog heading. Merge.
2. Run [release.yml](../../.github/workflows/release.yml) by hand on `main`
   (`gh workflow run release.yml`). The dry run builds, tests and packages
   everything a tag would, and uploads it as workflow artifacts.
3. Tag the merge commit `vX.Y.Z` and push the tag. The workflow checks the tag
   against `VERSION` and the changelog heading, repeats the dry run's lanes,
   pushes every library to `ghcr.io/animu-sphere/motion-connectors`, and
   creates a **draft** release.
4. Read the draft and publish it. Publishing is a human decision; nothing
   publishes automatically. The first push of a new GHCR package leaves it
   private, so it is made public once in the package's settings.

On each of Windows x86_64, macOS arm64 and Linux x86_64, the workflow builds
and tests the root tree, then builds, tests and packages each library with
`ost library`. Its runtime and `ost` pins mirror
[openstrata.ci.yaml](../../openstrata.ci.yaml) and are re-pinned with it;
`scripts/check_docs.py` fails when the `ost` pin drifts.
