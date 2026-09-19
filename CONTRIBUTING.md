# Contributing

Thanks for taking an interest in `motion-connectors`. Small fixes are useful:
documentation, tests, build fixes and clear bug reports are all good places to
start. You do not need to ask for permission before opening a small pull
request.

## Before you start

- Search existing issues and pull requests first.
- For a substantial behavior change or a connector-contract or source-profile change, open an
  issue before doing a large implementation. A short proposal is enough.
- For a bug, use the [bug report template](.github/ISSUE_TEMPLATE/bug_report.md).
  For an idea, use the [feature request template](.github/ISSUE_TEMPLATE/feature_request.md).
- Do not commit a motion capture or a packet capture of a real session
  unless the person recorded and the source's terms allow redistribution.
  A generated corpus is the usual way to test a decoder here; a real
  session leaves a manifest row with no bytes.
- Report security problems privately through [SECURITY.md](SECURITY.md), not in
  a public issue.

## Working locally

The project targets OpenUSD 26.08. For documentation-only changes, run the
inexpensive checks:

```powershell
python scripts/check_docs.py --selftest
python scripts/check_docs.py
```

For code changes, build and test from the repository root, with `ost` or with
plain CMake and `CMakePresets.json`:

```powershell
ost build
ost test
```

If a check cannot be run locally, say so in the pull request rather than hiding
it. Format changed C/C++ files with the repository's `.clang-format` before
opening the pull request:

```powershell
clang-format -i --style=file path/to/changed.cpp
```

## Pull requests

Keep each pull request focused on one understandable change. The pull request
template is intentionally short; a useful description says:

- what changed and why;
- which checks or tests you ran, or why they were not available;
- which documentation or capability statement changed, when behavior changed.

Draft pull requests are welcome for early feedback. Reviews are a conversation:
questions and suggestions should explain the concern, and contributors should
have a clear path to address them. The [documentation guidelines](docs/contributing/documentation.md)
describe where repository facts belong.

## Community

Please read the [Code of Conduct](CODE_OF_CONDUCT.md). Questions and uncertain
ideas are welcome; a minimal reproduction or a generated fixture is more useful
than a large private asset.
