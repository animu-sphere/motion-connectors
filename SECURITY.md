# Security Policy

Please do not report a suspected vulnerability in a public issue or pull
request. Every datagram a
connector receives is untrusted input from the network, so decoder crashes,
out-of-bounds reads, unbounded allocation, a listener that binds more widely
than documented, and unsafe build or package behavior are all worth
reporting.

## Report privately

Use GitHub's private [Report a vulnerability](https://github.com/animu-sphere/motion-connectors/security/advisories/new)
form. Include only what is needed to reproduce the problem. A useful report
usually contains:

- the affected commit, release or component;
- the impact you observed;
- reproduction steps or a small generated input;
- the operating system, OpenUSD version and build mode.

Do not upload a motion capture, packet capture, avatar model, credential or private file unless you have
permission to share it. If private vulnerability reporting is unavailable, open
a minimal issue asking for a private contact and do not include technical
details.

Maintainers will acknowledge reports when able, investigate the impact, and
coordinate a fix or release as appropriate. Credit is given when requested and
when it does not create a safety or privacy concern.

## Scope

The repository's connectors, decoders, transports, tools, bindings, build
scripts, packaging and CI are in scope. The network-exposure rules are
[CONNECTOR_CONTRACT.md §10](docs/design/CONNECTOR_CONTRACT.md#10-untrusted-input):
a connector binds loopback unless asked otherwise, bounds what it buffers,
and refuses a malformed packet with a diagnostic rather than a crash. Issues that belong to OpenUSD, a device vendor's software or another
external project may also need to be reported there; mentioning the upstream
report in this one is helpful once disclosure is safe.
