// SPDX-License-Identifier: Apache-2.0
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { WebXRConnector, SOURCE_PROFILE, HAND_JOINTS } from "../src/index.js";
import { fixture, pose, hand, wireFixture } from "./fixtures.js";

const codes = connector => connector.getDiagnostics().map(record => record.code);
const trackers = connector => connector.poll().actors[0].trackers;

test("poll has no SDK calls; caller owns session, space and frame loop", () => {
  const f = fixture();
  assert.equal(f.connector.getState(), "disconnected");
  assert.equal(f.connector.acquire(f.frame, 1000, 1), false);
  assert.equal(f.connector.open().succeeded, true);
  assert.equal(f.connector.getState(), "connecting");
  assert.equal(f.connector.poll(), undefined);
  assert.deepEqual(f.calls, []);
  assert.equal(f.connector.acquire(f.frame, 1000, 1), true);
  assert.equal(f.connector.getState(), "connected");
  const frame = f.connector.poll();
  assert.equal(frame.frameNumber, "1");
  assert.deepEqual(frame.timing, { sourceTimestamp: 1, receiveTimestamp: 1, sourceClock: "localMonotonic" });
  assert.equal(frame.sourceProfile, SOURCE_PROFILE);
  assert.equal("pose" in frame.actors[0], false);
  assert.deepEqual(frame.actors[0].trackers[0].position, [-1, 2, -3]);
  assert.equal(f.calls.length, 1);
  assert.equal(f.calls[0][1], f.referenceSpace);
  f.connector.close();
  assert.equal(f.connector.poll(), undefined);
  assert.equal(f.connector.getState(), "disconnected");
  f.session.dispatchEvent(new Event("end"));
  assert.equal(f.connector.getState(), "disconnected");
});

// A physical vector check, independent of the implementation's quaternion formula.
function rotate([w, x, y, z], [a, b, c]) {
  const cross = [y * c - z * b, z * a - x * c, x * b - y * a];
  const twice = cross.map(v => 2 * v);
  return [a + w * twice[0] + y * twice[2] - z * twice[1],
    b + w * twice[1] + z * twice[0] - x * twice[2],
    c + w * twice[2] + x * twice[1] - y * twice[0]];
}
const basis = ([x, y, z]) => [-x, y, -z];

test("head, grip and every hand joint preserve physical yaw/pitch/roll in both directions", () => {
  for (let axis = 0; axis < 3; ++axis) for (const sign of [-1, 1]) {
    const f = fixture();
    const q = [Math.cos(0.31), 0, 0, 0];
    q[axis + 1] = sign * Math.sin(0.31);
    const p = pose([0.25, -1, 3], q);
    f.frame.getViewerPose = f.frame.getPose = f.frame.getJointPose = () => p;
    f.session.inputSources = [{ handedness: "left", gripSpace: {} }, hand("right")];
    f.connector.open();
    assert.ok(f.connector.acquire(f.frame, 1000, 1));
    const result = trackers(f.connector);
    assert.equal(result.length, 27);
    for (const tracker of result) {
      assert.deepEqual(tracker.position, [-0.25, -1, -3]);
      const expected = basis(rotate(q, [0.2, 0.5, -0.7]));
      const actual = rotate(tracker.rotation, basis([0.2, 0.5, -0.7]));
      actual.forEach((value, index) => assert.ok(Math.abs(value - expected[index]) < 1e-6));
      assert.equal("confidence" in tracker, false);
    }
  }
});

test("missing poses publish absent components and recover without stale values", () => {
  const f = fixture();
  f.connector.open();
  f.frame.getViewerPose = () => null;
  assert.ok(f.connector.acquire(f.frame, 1000, 1));
  assert.deepEqual(trackers(f.connector), [{ trackerId: "head" }]);
  assert.deepEqual(codes(f.connector), ["WEBXR_POSE_UNAVAILABLE"]);
  assert.equal(f.connector.getState(), "degraded");
  f.frame.getViewerPose = () => pose();
  assert.ok(f.connector.acquire(f.frame, 1001, 1));
  assert.equal(f.connector.getState(), "connected");
  assert.deepEqual(codes(f.connector), []);
});

test("components are validated independently and SDK quaternions are normalized", () => {
  const f = fixture();
  f.connector.open();
  f.frame.getViewerPose = () => pose([NaN, 1, 2], [2, 0, 0, 0]);
  f.connector.acquire(f.frame, 1000, 1);
  const first = trackers(f.connector)[0];
  assert.equal("position" in first, false);
  assert.deepEqual(first.rotation.map(v => v || 0), [1, 0, 0, 0]);
  f.frame.getViewerPose = () => pose([1, 2, 3], [0, 0, 0, 0]);
  f.connector.acquire(f.frame, 1001, 1);
  const second = trackers(f.connector)[0];
  assert.equal("rotation" in second, false);
  assert.deepEqual(second.position, [-1, 2, -3]);
  for (const value of [Infinity, -Infinity, 3.5e38, "1", undefined]) {
    f.frame.getViewerPose = () => pose([value, 1, 2], [NaN, 0, 0, 0]);
    f.connector.acquire(f.frame, 1002 + f.connector.getBufferStats().pushedFrames, 1);
    assert.deepEqual(trackers(f.connector), [{ trackerId: "head" }]);
    assert.deepEqual(codes(f.connector), ["WEBXR_TRANSFORM_INVALID", "WEBXR_TRANSFORM_INVALID"]);
  }
});

test("emulated positions remain observed API values with local diagnostics", () => {
  const f = fixture();
  f.connector.open();
  f.frame.getViewerPose = () => pose([1, 2, 3], [1, 0, 0, 0], true);
  f.connector.acquire(f.frame, 0, 0);
  const frame = f.connector.poll();
  assert.deepEqual(frame.actors[0].trackers[0].position, [-1, 2, -3]);
  assert.equal(f.connector.getState(), "degraded");
  assert.deepEqual(codes(f.connector), ["WEBXR_POSITION_EMULATED"]);
  assert.equal(JSON.stringify(frame).includes("confidence"), false);
  assert.equal(JSON.stringify(frame).includes("diagnostics"), false);
});

test("input identities remain stable for objects, distinct for repeated handedness and absent after removal", () => {
  const f = fixture({ viewer: false, hands: false });
  const left = { handedness: "left", gripSpace: {} };
  const other = { handedness: "left", gripSpace: {} };
  f.session.inputSources = [left, other, { handedness: "none", targetRaySpace: {} }];
  f.connector.open();
  f.connector.acquire(f.frame, 1000, 1);
  assert.deepEqual(trackers(f.connector).map(t => t.trackerId), ["controller:left:1", "controller:left:2"]);
  f.session.inputSources = [other, left];
  f.connector.acquire(f.frame, 1001, 1);
  assert.deepEqual(trackers(f.connector).map(t => t.trackerId), ["controller:left:2", "controller:left:1"]);
  f.session.inputSources = [];
  f.connector.acquire(f.frame, 1002, 1);
  assert.deepEqual(trackers(f.connector), []);
});

test("hands publish the 25 WebXR names, unavailable joints, and no controller inferred from a hand grip", () => {
  const f = fixture({ viewer: false });
  const input = hand();
  input.gripSpace = {};
  input.hand.delete("wrist");
  f.session.inputSources = [input];
  f.connector.open();
  f.connector.acquire(f.frame, 1000, 1);
  const result = trackers(f.connector);
  assert.equal(result.length, 25);
  assert.deepEqual(result[0], { trackerId: "hand:left:1:wrist" });
  assert.deepEqual(result.map(t => t.trackerId.split(":").at(-1)), HAND_JOINTS);
  assert.ok(f.calls.every(call => call[0] === "joint" && call[2] === f.referenceSpace));
  assert.deepEqual(f.connector.getCapabilities(), ["trackers", "source-timestamps", "controllers"]);
  assert.deepEqual(fixture({ controllers: false }).connector.getCapabilities(), ["trackers", "source-timestamps"]);
});

test("timestamp refusals consume no SDK call or frame number; same receive time is allowed", () => {
  const f = fixture();
  f.connector.open();
  f.connector.acquire(f.frame, 1000, 2);
  f.connector.poll();
  const before = f.calls.length;
  for (const [source, receive] of [[1000, 2], [999, 2], [1001, 1], [NaN, 2], [1001, Infinity], [-1, 2]]) {
    assert.equal(f.connector.acquire(f.frame, source, receive), false);
    assert.deepEqual(codes(f.connector), ["WEBXR_TIMESTAMP_INVALID"]);
    assert.equal(f.connector.poll(), undefined);
  }
  assert.equal(f.calls.length, before);
  assert.ok(f.connector.acquire(f.frame, 1001, 2));
  assert.equal(f.connector.poll().frameNumber, "2");
});

test("latest and ordered queues have bounded drop statistics", () => {
  for (const mode of ["latest", "ordered"]) {
    const f = fixture();
    f.connector.open({ bufferMode: mode, bufferCapacity: 2 });
    for (let i = 1; i <= 3; ++i) f.connector.acquire(f.frame, i, i);
    const numbers = [];
    for (let frame; (frame = f.connector.poll());) numbers.push(frame.frameNumber);
    assert.deepEqual(numbers, mode === "latest" ? ["3"] : ["2", "3"]);
    assert.equal(f.connector.getBufferStats().droppedFrames, mode === "latest" ? 2 : 1);
    assert.equal(f.connector.getBufferStats().skippedFrames, mode === "latest" ? 2 : 1);
  }
});

test("lossless rejects before SDK calls and permits a retry of the same sample after polling", () => {
  const f = fixture();
  f.connector.open({ bufferMode: "lossless" });
  f.connector.acquire(f.frame, 1, 1);
  const before = f.calls.length;
  assert.equal(f.connector.acquire(f.frame, 2, 2), false);
  assert.equal(f.calls.length, before);
  assert.deepEqual(codes(f.connector), ["WEBXR_BUFFER_FULL"]);
  assert.equal(f.connector.getBufferStats().rejectedFrames, 1);
  assert.equal(f.connector.poll().frameNumber, "1");
  assert.ok(f.connector.acquire(f.frame, 2, 2));
  assert.equal(f.connector.poll().frameNumber, "2");
});

test("an API exception discards partial acquisition, retains prior frames and requires reopen", () => {
  const f = fixture();
  f.connector.open({ bufferMode: "ordered", bufferCapacity: 2 });
  f.connector.acquire(f.frame, 1, 1);
  f.session.inputSources = [{ handedness: "left", gripSpace: {} }];
  f.frame.getPose = () => { throw new Error("inactive frame"); };
  assert.equal(f.connector.acquire(f.frame, 2, 2), false);
  assert.equal(f.connector.getState(), "error");
  assert.deepEqual(codes(f.connector), ["WEBXR_API_FAILED"]);
  assert.equal(f.connector.poll().frameNumber, "1");
  assert.equal(f.connector.poll(), undefined);
  assert.equal(f.connector.acquire(f.frame, 3, 3), false);
  f.connector.open();
  f.frame.getPose = () => pose();
  assert.ok(f.connector.acquire(f.frame, 0, 0));
  assert.equal(f.connector.poll().frameNumber, "1");
  assert.equal(f.connector.getBufferStats().pushedFrames, 1);
});

test("wrong sessions are refused and more than eight inputs never publish partial output", () => {
  const f = fixture();
  f.connector.open();
  assert.equal(f.connector.acquire({ ...f.frame, session: {} }, 1, 1), false);
  assert.equal(f.calls.length, 0);
  f.connector.open();
  f.session.inputSources = Array.from({ length: 9 }, () => ({ handedness: "none", gripSpace: {} }));
  assert.equal(f.connector.acquire(f.frame, 1, 1), false);
  assert.equal(f.connector.poll(), undefined);
  assert.deepEqual(codes(f.connector), ["WEBXR_INPUT_LIMIT"]);
  f.session.inputSources.pop();
  assert.ok(f.connector.acquire(f.frame, 1, 1));
  assert.equal(f.connector.poll().frameNumber, "1");
});

test("reference reset forces a fresh epoch; session end cannot be reopened", () => {
  const f = fixture();
  f.connector.open();
  f.connector.acquire(f.frame, 1, 1);
  f.referenceSpace.dispatchEvent(new Event("reset"));
  assert.equal(f.connector.getState(), "error");
  assert.deepEqual(codes(f.connector), ["WEBXR_REFERENCE_SPACE_RESET"]);
  assert.equal(f.connector.acquire(f.frame, 2, 2), false);
  assert.equal(f.connector.poll().frameNumber, "1");
  assert.ok(f.connector.open().succeeded);
  f.connector.acquire(f.frame, 1, 1);
  assert.equal(f.connector.poll().frameNumber, "1");
  f.session.dispatchEvent(new Event("end"));
  assert.deepEqual(codes(f.connector), ["WEBXR_SESSION_ENDED"]);
  assert.equal(f.connector.getDiagnostics()[0].recoverable, false);
  assert.equal(f.connector.open().succeeded, false);
});

test("invalid config fails without browser queries or listeners", () => {
  for (const config of [{ sourceProfile: "webxr.hand.v1" }, { coordinateConversion: "identity" },
    { bufferMode: "unbounded" }, { bufferCapacity: 0 }, { bufferCapacity: 4097 }, { bufferCapacity: 1.5 }]) {
    const f = fixture();
    assert.equal(f.connector.open(config).succeeded, false);
    assert.equal(f.connector.getState(), "error");
    assert.deepEqual(codes(f.connector), ["WEBXR_CONFIG_INVALID"]);
    assert.deepEqual(f.calls, []);
    f.referenceSpace.dispatchEvent(new Event("reset"));
    assert.deepEqual(codes(f.connector), ["WEBXR_CONFIG_INVALID"]);
  }
  assert.equal(new WebXRConnector().open().succeeded, false);
  assert.equal(fixture({ viewer: false, hands: false, controllers: false }).connector.open().succeeded, false);
});

test("diagnostics and stats are snapshots", () => {
  const f = fixture();
  f.connector.open();
  f.connector.acquire(f.frame, NaN, 1);
  const copy = f.connector.getDiagnostics();
  copy[0].code = "changed";
  assert.deepEqual(codes(f.connector), ["WEBXR_TIMESTAMP_INVALID"]);
  f.connector.getBufferStats().pushedFrames = 42;
  assert.equal(f.connector.getBufferStats().pushedFrames, 0);
});

test("generated wire output matches the independent native decoder fixture and profile", () => {
  const frame = wireFixture();
  const golden = JSON.parse(readFileSync(new URL("./wire/webxr.frame.json", import.meta.url), "utf8"));
  assert.deepEqual(JSON.parse(JSON.stringify(frame)), golden);
  const profile = JSON.parse(readFileSync(new URL("../profiles/webxr.observations.v1.json", import.meta.url), "utf8"));
  assert.equal(profile.id, frame.sourceProfile);
  assert.equal(profile.basis.forwardAxis, "-Z");
  assert.equal(profile.observationKind, "trackers");
  assert.deepEqual(profile.capabilities, fixture().connector.getCapabilities());
  assert.equal(profile.jointSet.length, 0);
});
