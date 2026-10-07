// SPDX-License-Identifier: Apache-2.0
// Compile against actual browser WebXR declarations, not fixture-only types.
import { WebXRConnector, type ObservationFrame } from "@openstrata/motion-connector-webxr";

declare const session: XRSession;
declare const referenceSpace: XRReferenceSpace;
declare const xrFrame: XRFrame;
declare function consume(frame: ObservationFrame): void;

const connector = new WebXRConnector({ session, referenceSpace });
const status: boolean = connector.open({ bufferMode: "ordered", bufferCapacity: 4 }).succeeded;
if (status) {
  connector.acquire(xrFrame, 1234, performance.now() / 1000);
  const frame = connector.poll();
  if (frame) consume(frame);
}
connector.close();
