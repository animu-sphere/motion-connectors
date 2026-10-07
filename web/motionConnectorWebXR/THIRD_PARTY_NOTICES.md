# Third-party notices

The runtime module bundles no third-party code and uses browser WebXR APIs.
The following pinned development packages are installed only for declaration
checks; they are not bundled in the npm artifact:

| Package | Version | License | Use |
| --- | --- | --- | --- |
| [TypeScript](https://github.com/microsoft/TypeScript) | 5.9.3 | Apache-2.0 | Compile the public package usage fixture |
| [@types/webxr](https://github.com/DefinitelyTyped/DefinitelyTyped/tree/master/types/webxr) | 0.5.24 | MIT | Check actual browser WebXR interfaces against public declarations |

The package lock records registry URLs and integrity hashes. Upstream licenses
are distributed with the installed development packages.
