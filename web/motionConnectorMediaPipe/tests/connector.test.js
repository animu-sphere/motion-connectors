// SPDX-License-Identifier: Apache-2.0
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { MediaPipeConnector, SOURCE_PROFILES } from "../src/index.js";
import { body, hands, face, wireFixtures } from "./fixtures.js";
const codes = c => c.getDiagnostics().map(d => d.code);
const opened = (part = "body", config = {}) => {
  const connector = new MediaPipeConnector({ part });
  assert.ok(connector.open(config).succeeded);
  return connector;
};

test("body landmarks preserve metric positions and visibility without a semantic pose", () => {
  const c = opened();
  const input = body();
  // Image coordinates must never become metres.
  input.landmarks[0][0] = { x: 123, y: 456, z: 789 };
  assert.ok(c.acquire(input, 1500, 2));
  input.worldLandmarks[0][0].x = 999;
  const frame = c.poll();
  assert.equal(frame.frameNumber, "1");
  assert.deepEqual(frame.timing, { sourceTimestamp: 1.5, receiveTimestamp: 2, sourceClock: "localMonotonic" });
  assert.equal(frame.actors[0].trackers.length, 33);
  assert.deepEqual(frame.actors[0].trackers[0], { trackerId: "body:0", position: [0, 0.5, 0.25], confidence: 0.75 });
  assert.equal(frame.actors[0].pose, undefined);
  assert.equal(c.getState(), "connected");
  assert.deepEqual(c.getCapabilities(), ["trackers", "source-timestamps", "confidence"]);
});

test("physical right/up/towards-camera axes and opposite directions reach the canonical basis", () => {
  for (const [raw, expected] of [
    [[1, 0, 0], [1, 0, 0]], [[0, -1, 0], [0, 1, 0]], [[0, 0, -1], [0, 0, 1]],
  ]) for (const sign of [1, -1]) {
    const input = body();
    [input.worldLandmarks[0][0].x, input.worldLandmarks[0][0].y, input.worldLandmarks[0][0].z] = raw.map(v => v * sign);
    const c = opened();
    c.acquire(input, 1, 1);
    assert.deepEqual(c.poll().actors[0].trackers[0].position.map(v => v || 0), expected.map(v => v * sign || 0));
  }
});

test("hands preserve handedness and source index under result reordering, with no inferred confidence", () => {
  const c = opened("hands", { bufferMode: "ordered", bufferCapacity: 2 });
  const input = hands();
  c.acquire(input, 1, 1);
  for (const key of ["worldLandmarks", "landmarks", "handedness"]) input[key].reverse();
  c.acquire(input, 2, 2);
  const first = c.poll().actors[0].trackers;
  const second = c.poll().actors[0].trackers;
  assert.equal(first.length, 42);
  assert.equal(first[0].trackerId, "hand:left:0");
  assert.equal(first[21].trackerId, "hand:right:0");
  assert.deepEqual(second.slice(0, 21), first.slice(21));
  assert.equal(first[0].confidence, undefined);
  assert.equal(first[0].rotation, undefined);
});

test("face categories become namespaced sparse channels, including zero weights", () => {
  const c = opened("face");
  const input = face();
  input.faceBlendshapes[0].categories.push({ categoryName: "_neutral", score: 0 });
  c.acquire(input, 1000, 2);
  const actor = c.poll().actors[0];
  assert.deepEqual(actor.pose.channels, { "mediapipe:jawOpen": 0.5, "mediapipe:eyeBlinkLeft": 0.25, "mediapipe:_neutral": 0 });
  assert.deepEqual(actor.pose.joints, {});
  assert.deepEqual(actor.pose.root, {});
  assert.deepEqual(actor.trackers, []);
  assert.equal(actor.pose.metadata.sourceTimestamp, 1);
});

test("invalid components stay unavailable; bad visibility is omitted and recovery is immediate", () => {
  const c = opened();
  const input = body();
  input.worldLandmarks[0][0].x = NaN;
  input.worldLandmarks[0][1].z = 1e39;
  input.worldLandmarks[0][2].visibility = -0.1;
  delete input.worldLandmarks[0][3].visibility;
  input.worldLandmarks[0][4] = undefined;
  c.acquire(input, 1, 1);
  const trackers = c.poll().actors[0].trackers;
  assert.equal(trackers[0].position, undefined);
  assert.equal(trackers[1].position, undefined);
  assert.equal(trackers[2].confidence, undefined);
  assert.equal(trackers[3].confidence, undefined);
  assert.deepEqual(trackers[4], { trackerId: "body:4" });
  assert.equal(c.getState(), "degraded");
  assert.ok(codes(c).includes("MEDIAPIPE_CONFIDENCE_INVALID"));
  assert.ok(c.acquire(body(), 2, 2));
  assert.equal(c.getState(), "connected");
});

test("empty detections and disabled face blendshapes never retain old observations", () => {
  for (const [part, detected, empty] of [
    ["body", body(), { landmarks: [], worldLandmarks: [] }],
    ["hands", hands(), { landmarks: [], worldLandmarks: [], handedness: [] }],
    ["face", face(), { faceLandmarks: [], faceBlendshapes: [] }],
  ]) {
    const c = opened(part);
    c.acquire(detected, 1, 1);
    c.acquire(empty, 2, 2);
    assert.deepEqual(c.poll().actors, []);
    assert.deepEqual(codes(c), ["MEDIAPIPE_NO_DETECTION"]);
  }
  const c = opened("face");
  c.acquire({ faceLandmarks: [[]], faceBlendshapes: [] }, 1, 1);
  assert.deepEqual(c.poll().actors, [{ actor: "mediapipe:0", trackers: [] }]);
  assert.deepEqual(codes(c), ["MEDIAPIPE_BLENDSHAPES_UNAVAILABLE"]);
});

test("malformed result, normalized-only coordinates and multiple people reject atomically", () => {
  const invalidBody = [null, {}, { landmarks: [body().landmarks[0]] },
    { landmarks: [[], []], worldLandmarks: [body().worldLandmarks[0], body().worldLandmarks[0]] },
    { landmarks: [[]], worldLandmarks: [[]] }, { landmarks: [], worldLandmarks: body().worldLandmarks }];
  const invalidHands = [];
  for (const mutation of [
    r => r.handedness.pop(), r => r.handedness[0][0].categoryName = "Unknown",
    r => r.handedness[0][0].score = Infinity, r => r.handedness[1] = r.handedness[0],
    r => r.handedness[0] = [{ categoryName: "Left", score: 0.5 }, { categoryName: "Right", score: 0.5 }],
  ]) { const input = hands(); mutation(input); invalidHands.push(input); }
  const invalidFace = [];
  for (const mutation of [
    r => r.faceLandmarks.push([]), r => r.faceBlendshapes[0].categories.push(r.faceBlendshapes[0].categories[0]),
    r => r.faceBlendshapes[0].categories[0].score = 2,
    r => r.faceBlendshapes[0].categories[0].categoryName = "\ud800",
    r => r.faceBlendshapes[0].categories = Array(65).fill({ categoryName: "x", score: 0 }),
  ]) { const input = face(); mutation(input); invalidFace.push(input); }
  for (const [part, inputs, valid] of [["body", invalidBody, body], ["hands", invalidHands, hands], ["face", invalidFace, face]]) {
    for (const input of inputs) {
      const c = opened(part);
      assert.equal(c.acquire(input, 1, 1), false);
      assert.equal(c.poll(), undefined);
      assert.deepEqual(codes(c), ["MEDIAPIPE_RESULT_INVALID"]);
      assert.ok(c.acquire(valid(), 1, 1));
      assert.equal(c.poll().frameNumber, "1");
    }
  }
});

test("timestamps reject regressions, allow equal receive times and reset on reopen", () => {
  const c = opened();
  for (const [source, receive] of [[NaN, 1], [Infinity, 1], [-1, 1], [1, NaN], [1, -1]]) {
    assert.equal(c.acquire(body(), source, receive), false);
    assert.deepEqual(codes(c), ["MEDIAPIPE_TIMESTAMP_INVALID"]);
  }
  assert.ok(c.acquire(body(), 1000, 2)); c.poll();
  assert.equal(c.acquire(body(), 1000, 2), false);
  assert.equal(c.acquire(body(), 1001, 1), false);
  assert.ok(c.acquire(body(), 1001, 2));
  c.open();
  assert.equal(c.getState(), "connecting");
  assert.ok(c.acquire(body(), 0, 0));
  assert.equal(c.poll().frameNumber, "1");
  c.close();
  assert.equal(c.acquire(body(), 1, 1), false);
});

test("atomic rejection preserves already queued frames and their clocks", () => {
  const c = opened("hands", { bufferMode: "ordered", bufferCapacity: 2 });
  assert.ok(c.acquire(hands(), 1, 1));
  const malformed = hands();
  malformed.worldLandmarks[0][0].x = NaN;
  malformed.handedness[1][0].categoryName = "Unknown";
  assert.equal(c.acquire(malformed, 2, 2), false);
  assert.deepEqual(codes(c), ["MEDIAPIPE_RESULT_INVALID"]);
  assert.equal(c.poll().frameNumber, "1");
  assert.ok(c.acquire(hands(), 2, 2));
  assert.equal(c.poll().frameNumber, "2");
});

test("bounded latest, ordered and lossless queues preserve accepted numbering and allow retry", () => {
  for (const mode of ["latest", "ordered", "lossless"]) {
    const c = opened("body", { bufferMode: mode, bufferCapacity: 2 });
    c.acquire(body(), 1, 1); c.acquire(body(), 2, 2);
    assert.equal(c.acquire(body(), 3, 3), mode !== "lossless");
    assert.equal(c.poll().frameNumber, mode === "latest" ? "3" : mode === "ordered" ? "2" : "1");
    if (mode === "lossless") {
      assert.deepEqual(codes(c), ["MEDIAPIPE_BUFFER_FULL"]);
      assert.ok(c.acquire(body(), 3, 3));
      assert.equal(c.poll().frameNumber, "2");
      assert.equal(c.poll().frameNumber, "3");
    }
    const stats = c.getBufferStats();
    assert.equal(stats.pushedFrames, 3);
    assert.equal(stats.droppedFrames, mode === "latest" ? 2 : mode === "ordered" ? 1 : 0);
    assert.equal(stats.skippedFrames, stats.droppedFrames);
    assert.equal(stats.rejectedFrames, mode === "lossless" ? 1 : 0);
  }
});

test("configuration and diagnostic snapshots", () => {
  for (const config of [{ sourceProfile: "webxr.observations.v1" }, { coordinateConversion: "identity" },
    { bufferMode: "unbounded" }, { bufferCapacity: 0 }, { bufferCapacity: 4097 }, { bufferCapacity: 1.5 }]) {
    const c = new MediaPipeConnector();
    assert.equal(c.open(config).succeeded, false);
    assert.equal(c.getState(), "error");
    assert.deepEqual(codes(c), ["MEDIAPIPE_CONFIG_INVALID"]);
  }
  for (const input of [{ part: "invalid" }, { part: "toString" }, { part: ["body"] }, { actorId: "" }, { actorId: "\ud800" }]) {
    const c = new MediaPipeConnector(input);
    assert.equal(c.open().succeeded, false);
    assert.equal(typeof c.getDiagnostics()[0].source, "string");
  }
  const c = opened();
  c.acquire(body(), NaN, 1);
  c.getDiagnostics()[0].code = "changed";
  c.getBufferStats().pushedFrames = 99;
  assert.deepEqual(codes(c), ["MEDIAPIPE_TIMESTAMP_INVALID"]);
  assert.equal(c.getBufferStats().pushedFrames, 0);
});

test("profile declarations and generated native codec fixtures agree with acquisition", () => {
  for (const [part, frame] of Object.entries(wireFixtures())) {
    const profile = JSON.parse(readFileSync(new URL(`../profiles/${SOURCE_PROFILES[part]}.json`, import.meta.url), "utf8"));
    assert.deepEqual(profile.capabilities, opened(part).getCapabilities());
    assert.equal(profile.id, frame.sourceProfile);
    assert.deepEqual(profile.jointSet, []);
    if (part !== "face") {
      assert.equal(profile.trackers.length, part === "body" ? 33 : 42);
      assert.equal(profile.basis.upAxis, "-Y");
      assert.equal(profile.basis.forwardAxis, "-Z");
      assert.equal(profile.basis.evidence, "assumed");
      assert.equal(profile.basis.lengthUnit, "metres");
      assert.deepEqual(profile.trackers.map(t => t.source), frame.actors[0].trackers.map(t => t.trackerId));
      assert.deepEqual(profile.basis.conversion, { position: ["x", "-y", "-z"], scale: 1 });
    }
    const golden = JSON.parse(readFileSync(new URL(`./wire/${part}.frame.json`, import.meta.url), "utf8"));
    assert.deepEqual(JSON.parse(JSON.stringify(frame)), golden);
  }
});
