# motionConnectorCore

The shared connector contract around `usd-motion-plugins`' `MotionPose`:
`IMotionConnector`, frame timing, actor envelopes, tracker observations,
capabilities and the bounded `Latest`, `Ordered` and `Lossless` frame buffer.

The core has no transport, protocol, device or avatar dependency. It is a
plain static library and links only `motionCore` for the shared pose and its
OpenUSD value types.