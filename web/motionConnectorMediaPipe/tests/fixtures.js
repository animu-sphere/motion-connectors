// SPDX-License-Identifier: Apache-2.0
import { MediaPipeConnector } from "../src/index.js";

export const points = count => Array.from({ length: count }, (_, index) =>
  ({ x: index / 32, y: -0.5, z: -0.25, visibility: 0.75 }));
export const body = () => ({ landmarks: [points(33)], worldLandmarks: [points(33)] });
export const hands = () => ({ landmarks: [points(21), points(21)], worldLandmarks: [points(21), points(21)],
  handedness: [[{ categoryName: "Left", score: 0.9 }], [{ categoryName: "Right", score: 0.8 }]] });
export const face = () => ({ faceLandmarks: [points(478)], faceBlendshapes: [{ categories: [
  { categoryName: "jawOpen", score: 0.5 }, { categoryName: "eyeBlinkLeft", score: 0.25 },
] }] });
export function wireFixtures() {
  const frames = {};
  for (const [part, result] of Object.entries({ body: body(), hands: hands(), face: face() })) {
    const connector = new MediaPipeConnector({ part });
    connector.open();
    connector.acquire(result, 1250, 2);
    frames[part] = connector.poll();
  }
  return frames;
}
