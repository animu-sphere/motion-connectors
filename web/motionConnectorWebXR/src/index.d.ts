export type ConnectorState = "disconnected" | "connecting" | "connected" | "degraded" | "error";
export type BufferMode = "latest" | "ordered" | "lossless";
export interface TrackerObservation {
  trackerId: string;
  position?: [number, number, number];
  rotation?: [number, number, number, number];
}
/** The tracker-only subset of FRAME_WIRE_FORMAT v1; this is not a native ABI. */
export interface ObservationFrame {
  format: "openstrata.motion.frame/v1";
  frameNumber: string;
  sourceProfile: "webxr.observations.v1";
  timing: { sourceTimestamp: number; receiveTimestamp: number; sourceClock: "localMonotonic" };
  actors: { actor: string; trackers: TrackerObservation[] }[];
}
export interface Diagnostic {
  code: string; severity: "warning" | "error"; recoverable: boolean;
  source: string; subject: string; timestamp: number; detail: string;
}
export interface BufferStats {
  pushedFrames: number; polledFrames: number; droppedFrames: number;
  skippedFrames: number; rejectedFrames: number;
}
// Structural browser interfaces avoid requiring @types/webxr or a native SDK.
export interface PoseInput {
  transform: { position: { x: number; y: number; z: number }; orientation: { x: number; y: number; z: number; w: number } };
  emulatedPosition?: boolean;
}
export interface InputSource {
  handedness: string;
  gripSpace?: object | null;
  hand?: { get(name: string): object | undefined } | null;
}
export interface SessionInput extends EventTarget { readonly inputSources: Iterable<InputSource>; }
export interface FrameInput {
  readonly session: SessionInput;
  getViewerPose(space: object): PoseInput | null | undefined;
  getPose(space: object, baseSpace: object): PoseInput | null | undefined;
  getJointPose?(space: object, baseSpace: object): PoseInput | null | undefined;
}
export const SOURCE_PROFILE: "webxr.observations.v1";
export const HAND_JOINTS: readonly string[];
export const DIAGNOSTICS: Readonly<Record<string, string>>;
export class WebXRConnector {
  constructor(input: { session: SessionInput; referenceSpace: EventTarget; viewer?: boolean; controllers?: boolean; hands?: boolean });
  open(config?: { sourceProfile?: string; coordinateConversion?: string; bufferMode?: BufferMode; bufferCapacity?: number }): { succeeded: boolean; message: string };
  close(): void;
  getState(): ConnectorState;
  getCapabilities(): readonly string[];
  getDiagnostics(): Diagnostic[];
  getBufferStats(): BufferStats;
  acquire(frame: FrameInput, callbackTimeMilliseconds: number, receiveTimestampSeconds: number): boolean;
  poll(): ObservationFrame | undefined;
}
