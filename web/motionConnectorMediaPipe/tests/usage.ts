// SPDX-License-Identifier: Apache-2.0
// Actual Tasks result declarations must remain assignable to the browser boundary.
import type { PoseLandmarkerResult, HandLandmarkerResult, FaceLandmarkerResult } from "@mediapipe/tasks-vision";
import { MediaPipeConnector, type ObservationFrame } from "@openstrata/motion-connector-mediapipe";
declare const pose: PoseLandmarkerResult;
declare const hands: HandLandmarkerResult;
declare const face: FaceLandmarkerResult;
declare function consume(frame: ObservationFrame): void;
for (const [part, result] of [["body", pose], ["hands", hands], ["face", face]] as const) {
  const connector = new MediaPipeConnector({ part, actorId: "performer:0" });
  if (connector.open({ bufferMode: "ordered", bufferCapacity: 4 }).succeeded) {
    connector.acquire(result, 1234, performance.now() / 1000);
    const frame = connector.poll();
    if (frame) consume(frame);
  }
  connector.close();
}
