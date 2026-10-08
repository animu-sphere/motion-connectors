// SPDX-License-Identifier: Apache-2.0
import { WebXRConnector, HAND_JOINTS } from "../src/index.js";

export function pose(position = [1, 2, 3], rotation = [1, 0, 0, 0], emulatedPosition = false) {
  return { transform: {
    position: { x: position[0], y: position[1], z: position[2] },
    orientation: { w: rotation[0], x: rotation[1], y: rotation[2], z: rotation[3] },
  }, emulatedPosition };
}

export function fixture(options = {}) {
  const session = new EventTarget();
  session.inputSources = [];
  const referenceSpace = new EventTarget();
  const calls = [];
  const frame = { session,
    getViewerPose(space) { calls.push(["viewer", space]); return pose(); },
    getPose(space, base) { calls.push(["grip", space, base]); return pose(); },
    getJointPose(space, base) { calls.push(["joint", space, base]); return pose(); },
  };
  const connector = new WebXRConnector({ session, referenceSpace, ...options });
  return { session, referenceSpace, frame, connector, calls };
}

export function hand(side = "left") {
  return { handedness: side, hand: new Map(HAND_JOINTS.map(name => [name, { name }])) };
}

export function wireFixture() {
  const f = fixture();
  f.session.inputSources = [{ handedness: "left", gripSpace: {} }, hand("right")];
  f.connector.open();
  f.connector.acquire(f.frame, 1250, 2.5);
  return f.connector.poll();
}
