// SPDX-License-Identifier: Apache-2.0

export const SOURCE_PROFILES = Object.freeze({
  body: "mediapipe.body.v1", hands: "mediapipe.hands.v1", face: "mediapipe.face.v1",
});
export const DIAGNOSTICS = Object.freeze({
  MEDIAPIPE_CONFIG_INVALID: "The profile, actor, coordinate conversion or buffer configuration is invalid.",
  MEDIAPIPE_TIMESTAMP_INVALID: "Source time must advance and receive time must not regress; both must be finite and nonnegative.",
  MEDIAPIPE_BUFFER_FULL: "Lossless acquisition requires the caller to drain the queue and retry.",
  MEDIAPIPE_RESULT_INVALID: "The result has an invalid topology, handedness, category set or exceeds the single-actor bound.",
  MEDIAPIPE_POSITION_INVALID: "A world landmark has no finite float-range position; the observation remains unavailable.",
  MEDIAPIPE_CONFIDENCE_INVALID: "Visibility is outside [0,1]; confidence is omitted.",
  MEDIAPIPE_NO_DETECTION: "The task reported no detection; no previous observation is retained.",
  MEDIAPIPE_BLENDSHAPES_UNAVAILABLE: "Enable outputFaceBlendshapes on the caller's FaceLandmarker.",
});
const FLOAT_MAX = 3.4028234663852886e38;
const UINT64_MAX = (1n << 64n) - 1n;
const finite = value => typeof value === "number" && Number.isFinite(value);
const score = value => finite(value) && value >= 0 && value <= 1;
const text = value => typeof value === "string" && value.length > 0 && value.length <= 256 &&
  !/[\u0000-\u001f\u007f\ud800-\udfff]/u.test(value);

/** Acquisition consumes caller-owned Tasks results; it never runs inference or opens a device. */
export class MediaPipeConnector {
  #part; #actor; #state = "disconnected"; #queue = []; #diagnostics = [];
  #stats; #mode; #capacity; #number = 0n; #lastSource; #lastReceive;

  constructor({ part = "body", actorId = "mediapipe:0" } = {}) {
    this.#part = part;
    this.#actor = actorId;
    this.#resetStats();
  }
  #resetStats() {
    this.#stats = { pushedFrames: 0, polledFrames: 0, droppedFrames: 0, skippedFrames: 0, rejectedFrames: 0 };
  }
  #report(code, subject, timestamp) {
    this.#diagnostics.push({ code, severity: code === "MEDIAPIPE_CONFIG_INVALID" ? "error" : "warning",
      recoverable: code !== "MEDIAPIPE_CONFIG_INVALID",
      source: Object.hasOwn(SOURCE_PROFILES, this.#part) ? SOURCE_PROFILES[this.#part] : "mediapipe",
      subject, timestamp, detail: DIAGNOSTICS[code] });
  }
  open({ sourceProfile = SOURCE_PROFILES[this.#part], coordinateConversion = "",
    bufferMode = "latest", bufferCapacity = 1 } = {}) {
    this.close();
    this.#diagnostics = [];
    this.#number = 0n;
    this.#lastSource = this.#lastReceive = undefined;
    this.#resetStats();
    if (typeof this.#part !== "string" || !Object.hasOwn(SOURCE_PROFILES, this.#part) || !text(this.#actor) ||
        sourceProfile !== SOURCE_PROFILES[this.#part] || coordinateConversion !== "" ||
        !["latest", "ordered", "lossless"].includes(bufferMode) ||
        !Number.isInteger(bufferCapacity) || bufferCapacity < 1 || bufferCapacity > 4096) {
      this.#report("MEDIAPIPE_CONFIG_INVALID", "config", 0);
      this.#state = "error";
      return { succeeded: false, message: DIAGNOSTICS.MEDIAPIPE_CONFIG_INVALID };
    }
    this.#mode = bufferMode;
    this.#capacity = bufferCapacity;
    this.#state = "connecting";
    return { succeeded: true, message: "" };
  }
  close() { this.#queue = []; this.#state = "disconnected"; }
  getState() { return this.#state; }
  getCapabilities() {
    return Object.freeze(this.#part === "face" ? ["face", "source-timestamps"] :
      ["trackers", "source-timestamps", ...(this.#part === "body" ? ["confidence"] : [])]);
  }
  getDiagnostics() { return this.#diagnostics.map(item => ({ ...item })); }
  getBufferStats() { return { ...this.#stats }; }

  #landmark(trackerId, point, timestamp) {
    const tracker = { trackerId };
    const coordinates = [point?.x, point?.y, point?.z];
    if (coordinates.every(value => finite(value) && Math.abs(value) <= FLOAT_MAX)) {
      // Metric, source-relative positions. Axis declaration remains assumed until measured.
      tracker.position = [coordinates[0], -coordinates[1], -coordinates[2]].map(Math.fround);
    } else {
      this.#report("MEDIAPIPE_POSITION_INVALID", trackerId, timestamp);
    }
    if (this.#part === "body" && point?.visibility !== undefined) {
      if (score(point.visibility)) tracker.confidence = Math.fround(point.visibility);
      else this.#report("MEDIAPIPE_CONFIDENCE_INVALID", trackerId, timestamp);
    }
    return tracker;
  }

  #actors(result, sourceTimestamp, receiveTimestamp) {
    if (!result || typeof result !== "object") throw new Error();
    if (this.#part === "face") {
      const faces = result.faceLandmarks;
      const shapes = result.faceBlendshapes;
      if (!Array.isArray(faces) || faces.length > 1 || !Array.isArray(shapes) ||
          shapes.length > faces.length) throw new Error();
      if (!faces.length) return [];
      if (!shapes.length || (Array.isArray(shapes[0]?.categories) && !shapes[0].categories.length)) {
        this.#report("MEDIAPIPE_BLENDSHAPES_UNAVAILABLE", "faceBlendshapes", receiveTimestamp);
        return [{ actor: this.#actor, trackers: [] }];
      }
      const categories = shapes[0]?.categories;
      if (!Array.isArray(categories) || categories.length > 64) throw new Error();
      const channels = {};
      for (const category of categories) {
        if (!text(category?.categoryName) || !score(category.score)) throw new Error();
        const name = `mediapipe:${category.categoryName}`;
        if (Object.hasOwn(channels, name)) throw new Error();
        channels[name] = Math.fround(category.score);
      }
      return [{ actor: this.#actor, pose: {
        timestamp: sourceTimestamp, root: {}, joints: {}, channels,
        metadata: { kind: "liveCapture", provider: "MediaPipe", protocol: "mediapipe",
          sourceId: this.#actor, sourceTimestamp },
      }, trackers: [] }];
    }
    const groups = result.worldLandmarks;
    const limit = this.#part === "body" ? 1 : 2;
    const count = this.#part === "body" ? 33 : 21;
    if (!Array.isArray(groups) || groups.length > limit ||
        !Array.isArray(result.landmarks) || result.landmarks.length !== groups.length ||
        !groups.every(group => Array.isArray(group) && group.length === count)) throw new Error();
    if (this.#part === "hands" && (!Array.isArray(result.handedness) ||
        result.handedness.length !== groups.length)) throw new Error();
    if (!groups.length) return [];
    const trackers = [];
    const sides = new Set();
    for (let group = 0; group < groups.length; ++group) {
      let prefix = "body";
      if (this.#part === "hands") {
        const categories = result.handedness[group];
        if (!Array.isArray(categories) || !categories.length || categories.length > 2 ||
            !categories.every(c => ["Left", "Right"].includes(c?.categoryName) && score(c.score)) ||
            new Set(categories.map(c => c.categoryName)).size !== categories.length) throw new Error();
        const best = categories.reduce((a, b) => b.score > a.score ? b : a);
        if (categories.length === 2 && categories[0].score === categories[1].score) throw new Error();
        const side = best.categoryName.toLowerCase();
        if (sides.has(side)) throw new Error();
        sides.add(side);
        prefix = `hand:${side}`;
      }
      for (let index = 0; index < count; ++index) {
        trackers.push(this.#landmark(`${prefix}:${index}`, groups[group][index], receiveTimestamp));
      }
    }
    return [{ actor: this.#actor, trackers }];
  }

  /** timestampMilliseconds is the caller's local monotonic video/inference timestamp. */
  acquire(result, timestampMilliseconds, receiveTimestamp) {
    if (this.#state === "disconnected" || this.#state === "error") return false;
    this.#diagnostics = [];
    if (!finite(timestampMilliseconds) || timestampMilliseconds < 0 ||
        (this.#lastSource !== undefined && timestampMilliseconds <= this.#lastSource) ||
        !finite(receiveTimestamp) || receiveTimestamp < 0 ||
        (this.#lastReceive !== undefined && receiveTimestamp < this.#lastReceive)) {
      this.#report("MEDIAPIPE_TIMESTAMP_INVALID", "time", finite(receiveTimestamp) ? receiveTimestamp : 0);
      this.#state = "degraded";
      return false;
    }
    if (this.#mode === "lossless" && this.#queue.length >= this.#capacity) {
      ++this.#stats.rejectedFrames;
      this.#report("MEDIAPIPE_BUFFER_FULL", "frame", receiveTimestamp);
      this.#state = "degraded";
      return false;
    }
    let actors;
    try {
      if (this.#number === UINT64_MAX) throw new Error();
      actors = this.#actors(result, timestampMilliseconds / 1000, receiveTimestamp);
    } catch {
      // Topology failures reject atomically, including diagnostics for a partial result.
      this.#diagnostics = [];
      this.#report("MEDIAPIPE_RESULT_INVALID", "result", receiveTimestamp);
      this.#state = "degraded";
      return false;
    }
    if (!actors.length) this.#report("MEDIAPIPE_NO_DETECTION", "result", receiveTimestamp);
    const frame = { format: "openstrata.motion.frame/v1", frameNumber: String(this.#number + 1n),
      sourceProfile: SOURCE_PROFILES[this.#part],
      timing: { sourceTimestamp: timestampMilliseconds / 1000, receiveTimestamp, sourceClock: "localMonotonic" }, actors };
    if (this.#mode === "latest") {
      this.#stats.droppedFrames += this.#queue.length;
      this.#stats.skippedFrames += this.#queue.length;
      this.#queue = [];
    } else if (this.#queue.length >= this.#capacity) {
      this.#queue.shift();
      ++this.#stats.droppedFrames;
      ++this.#stats.skippedFrames;
    }
    this.#queue.push(frame);
    ++this.#stats.pushedFrames;
    ++this.#number;
    this.#lastSource = timestampMilliseconds;
    this.#lastReceive = receiveTimestamp;
    this.#state = this.#diagnostics.length ? "degraded" : "connected";
    return true;
  }
  poll() {
    if (!this.#queue.length) return undefined;
    ++this.#stats.polledFrames;
    return this.#queue.shift();
  }
}
