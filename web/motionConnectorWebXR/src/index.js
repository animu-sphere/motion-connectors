// SPDX-License-Identifier: Apache-2.0

export const SOURCE_PROFILE = "webxr.observations.v1";
export const HAND_JOINTS = Object.freeze([
  "wrist", "thumb-metacarpal", "thumb-phalanx-proximal", "thumb-phalanx-distal", "thumb-tip",
  ...["index", "middle", "ring", "pinky"].flatMap(finger => [
    `${finger}-finger-metacarpal`, `${finger}-finger-phalanx-proximal`,
    `${finger}-finger-phalanx-intermediate`, `${finger}-finger-phalanx-distal`, `${finger}-finger-tip`,
  ]),
]);
export const DIAGNOSTICS = Object.freeze({
  WEBXR_CONFIG_INVALID: "The source profile, borrowed session/space or buffer configuration is invalid.",
  WEBXR_TIMESTAMP_INVALID: "Source time must advance and receive time must not regress; both must be finite and nonnegative.",
  WEBXR_BUFFER_FULL: "Lossless acquisition requires the caller to drain the queue and retry.",
  WEBXR_POSE_UNAVAILABLE: "The browser reported no pose for the configured observation.",
  WEBXR_POSITION_EMULATED: "The browser supplied an emulated position; the value is retained without inferred confidence.",
  WEBXR_TRANSFORM_INVALID: "A component is missing, non-finite, outside float range or has a zero quaternion.",
  WEBXR_API_FAILED: "A browser pose query failed; no partial frame was published. Reopen after restoring the session.",
  WEBXR_INPUT_LIMIT: "The session has more input sources than the acquisition bound.",
  WEBXR_REFERENCE_SPACE_RESET: "The reference space changed; reopen to begin a new acquisition epoch.",
  WEBXR_SESSION_ENDED: "The borrowed session ended; construct a connector for the replacement session.",
});
const fatal = new Set(["WEBXR_CONFIG_INVALID", "WEBXR_API_FAILED", "WEBXR_REFERENCE_SPACE_RESET", "WEBXR_SESSION_ENDED"]);
const UINT64_MAX = (1n << 64n) - 1n;
const FLOAT_MAX = 3.4028234663852886e38;
const finite = value => typeof value === "number" && Number.isFinite(value);

/** Reads only during the caller's active XR animation callback. Poll never calls WebXR. */
export class WebXRConnector {
  #session; #space; #inputs; #ended = false; #attached = false;
  #state = "disconnected"; #queue = []; #diagnostics = []; #stats;
  #mode; #capacity; #number = 0n; #lastSource; #lastReceive;
  #ids = new WeakMap(); #nextId = 0n;
  #onEnd = () => {
    this.#ended = true;
    this.#event("WEBXR_SESSION_ENDED");
  };
  #onReset = () => this.#event("WEBXR_REFERENCE_SPACE_RESET");

  constructor({ session, referenceSpace, viewer = true, controllers = true, hands = true } = {}) {
    this.#session = session;
    this.#space = referenceSpace;
    this.#inputs = { viewer, controllers, hands };
    this.#resetStats();
  }

  #resetStats() {
    this.#stats = { pushedFrames: 0, polledFrames: 0, droppedFrames: 0, skippedFrames: 0, rejectedFrames: 0 };
  }

  #report(code, subject, timestamp, detail = DIAGNOSTICS[code]) {
    this.#diagnostics.push({ code, severity: fatal.has(code) ? "error" : "warning",
      recoverable: !fatal.has(code), source: SOURCE_PROFILE, subject, timestamp, detail });
  }

  #event(code) {
    this.#diagnostics = [];
    this.#report(code, code === "WEBXR_SESSION_ENDED" ? "session" : "referenceSpace", this.#lastReceive ?? 0);
    this.#state = "error";
  }

  open({ sourceProfile = SOURCE_PROFILE, coordinateConversion = "", bufferMode = "latest", bufferCapacity = 1 } = {}) {
    this.close();
    this.#diagnostics = [];
    this.#number = 0n;
    this.#lastSource = this.#lastReceive = undefined;
    this.#ids = new WeakMap();
    this.#nextId = 0n;
    this.#resetStats();
    const eventTarget = target => target && typeof target.addEventListener === "function" &&
      typeof target.removeEventListener === "function";
    if (!eventTarget(this.#session) || !eventTarget(this.#space) || this.#ended ||
        sourceProfile !== SOURCE_PROFILE || coordinateConversion !== "" ||
        !Object.values(this.#inputs).every(value => typeof value === "boolean") ||
        !Object.values(this.#inputs).some(Boolean) ||
        !["latest", "ordered", "lossless"].includes(bufferMode) ||
        !Number.isInteger(bufferCapacity) || bufferCapacity < 1 || bufferCapacity > 4096) {
      this.#report("WEBXR_CONFIG_INVALID", "config", 0);
      this.#state = "error";
      return { succeeded: false, message: DIAGNOSTICS.WEBXR_CONFIG_INVALID };
    }
    this.#mode = bufferMode;
    this.#capacity = bufferCapacity;
    this.#session.addEventListener("end", this.#onEnd);
    this.#space.addEventListener("reset", this.#onReset);
    this.#attached = true;
    this.#state = "connecting";
    return { succeeded: true, message: "" };
  }

  close() {
    if (this.#attached) {
      this.#session.removeEventListener("end", this.#onEnd);
      this.#space.removeEventListener("reset", this.#onReset);
      this.#attached = false;
    }
    this.#queue = [];
    this.#state = "disconnected";
  }

  getState() { return this.#state; }
  getCapabilities() {
    return Object.freeze(["trackers", "source-timestamps", ...(this.#inputs.controllers ? ["controllers"] : [])]);
  }
  getDiagnostics() { return this.#diagnostics.map(item => ({ ...item })); }
  getBufferStats() { return { ...this.#stats }; }

  #observe(trackerId, pose, timestamp) {
    const tracker = { trackerId };
    if (!pose) {
      this.#report("WEBXR_POSE_UNAVAILABLE", trackerId, timestamp);
      return tracker;
    }
    const position = pose.transform?.position;
    const coordinates = [position?.x, position?.y, position?.z];
    if (coordinates.every(value => finite(value) && Math.abs(value) <= FLOAT_MAX)) {
      tracker.position = [-coordinates[0], coordinates[1], -coordinates[2]].map(Math.fround);
    } else {
      this.#report("WEBXR_TRANSFORM_INVALID", `${trackerId}.position`, timestamp);
    }
    const q = pose.transform?.orientation;
    const components = [q?.w, q?.x, q?.y, q?.z];
    const length = components.every(finite) ? Math.hypot(...components) : 0;
    if (finite(length) && length > 0) {
      tracker.rotation = [components[0], -components[1], components[2], -components[3]]
        .map(value => Math.fround(value / length));
    } else {
      this.#report("WEBXR_TRANSFORM_INVALID", `${trackerId}.rotation`, timestamp);
    }
    if (pose.emulatedPosition) this.#report("WEBXR_POSITION_EMULATED", trackerId, timestamp);
    return tracker;
  }

  /** callbackTime is the XR requestAnimationFrame timestamp in ms; receive time is monotonic seconds. */
  acquire(frame, callbackTime, receiveTimestamp) {
    if (this.#state === "disconnected" || this.#state === "error") return false;
    this.#diagnostics = [];
    if (!finite(callbackTime) || callbackTime < 0 ||
        (this.#lastSource !== undefined && callbackTime <= this.#lastSource) ||
        !finite(receiveTimestamp) || receiveTimestamp < 0 ||
        (this.#lastReceive !== undefined && receiveTimestamp < this.#lastReceive)) {
      this.#report("WEBXR_TIMESTAMP_INVALID", "time", finite(receiveTimestamp) ? receiveTimestamp : 0);
      this.#state = "degraded";
      return false;
    }
    if (this.#mode === "lossless" && this.#queue.length >= this.#capacity) {
      ++this.#stats.rejectedFrames;
      this.#report("WEBXR_BUFFER_FULL", "frame", receiveTimestamp);
      this.#state = "degraded";
      return false;
    }
    const trackers = [];
    try {
      if (frame?.session !== this.#session || this.#number === UINT64_MAX) {
        throw new Error("XRFrame belongs to a different session or the frame counter is exhausted.");
      }
      if (this.#inputs.viewer) {
        trackers.push(this.#observe("head", frame.getViewerPose(this.#space), receiveTimestamp));
      }
      if (this.#inputs.controllers || this.#inputs.hands) {
        // Eight inputs bound iteration and output to at most 209 observations.
        let count = 0;
        for (const input of this.#session.inputSources) {
          if (++count > 8) {
            this.#report("WEBXR_INPUT_LIMIT", "inputSources", receiveTimestamp);
            this.#state = "degraded";
            return false;
          }
          if (!this.#ids.has(input)) this.#ids.set(input, String(++this.#nextId));
          const side = ["left", "right"].includes(input.handedness) ? input.handedness : "none";
          const id = `${side}:${this.#ids.get(input)}`;
          // Grip is a physical controller observation. Target rays are pointing semantics.
          if (this.#inputs.controllers && !input.hand && input.gripSpace) {
            trackers.push(this.#observe(`controller:${id}`, frame.getPose(input.gripSpace, this.#space), receiveTimestamp));
          }
          if (this.#inputs.hands && input.hand) {
            for (const joint of HAND_JOINTS) {
              const space = input.hand.get(joint);
              trackers.push(this.#observe(`hand:${id}:${joint}`,
                space ? frame.getJointPose(space, this.#space) : null, receiveTimestamp));
            }
          }
        }
      }
    } catch (error) {
      this.#report("WEBXR_API_FAILED", "frame", receiveTimestamp,
        `${DIAGNOSTICS.WEBXR_API_FAILED} ${String(error?.message ?? error)}`);
      this.#state = "error";
      return false;
    }
    const result = {
      format: "openstrata.motion.frame/v1", frameNumber: String(this.#number + 1n), sourceProfile: SOURCE_PROFILE,
      timing: { sourceTimestamp: callbackTime / 1000, receiveTimestamp, sourceClock: "localMonotonic" },
      actors: [{ actor: "webxr:0", trackers }],
    };
    if (this.#mode === "latest") {
      this.#stats.droppedFrames += this.#queue.length;
      this.#stats.skippedFrames += this.#queue.length;
      this.#queue = [];
    } else if (this.#queue.length >= this.#capacity) {
      this.#queue.shift();
      ++this.#stats.droppedFrames;
      ++this.#stats.skippedFrames;
    }
    this.#queue.push(result);
    ++this.#stats.pushedFrames;
    ++this.#number;
    this.#lastSource = callbackTime;
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
