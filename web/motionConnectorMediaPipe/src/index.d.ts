export type Part = "body" | "hands" | "face";
export type ConnectorState = "disconnected" | "connecting" | "connected" | "degraded" | "error";
export type BufferMode = "latest" | "ordered" | "lossless";
export interface Landmark { x: number; y: number; z: number; visibility?: number; }
export interface Category { categoryName: string; score: number; }
export interface BodyResult { landmarks: Landmark[][]; worldLandmarks: Landmark[][]; }
export interface HandsResult extends BodyResult { handedness: Category[][]; }
export interface FaceResult { faceLandmarks: Landmark[][]; faceBlendshapes: { categories: Category[] }[]; }
export interface TrackerObservation {
  trackerId: string; position?: [number, number, number]; confidence?: number;
}
/** FRAME_WIRE_FORMAT v1: metric landmark observations or a channels-only MotionPose. */
export interface ObservationFrame {
  format: "openstrata.motion.frame/v1";
  frameNumber: string;
  sourceProfile: "mediapipe.body.v1" | "mediapipe.hands.v1" | "mediapipe.face.v1";
  timing: { sourceTimestamp: number; receiveTimestamp: number; sourceClock: "localMonotonic" };
  actors: { actor: string; trackers: TrackerObservation[]; pose?: {
    timestamp: number; root: Record<string, never>; joints: Record<string, never>; channels: Record<string, number>;
    metadata: { kind: "liveCapture"; provider: "MediaPipe"; protocol: "mediapipe"; sourceId: string; sourceTimestamp: number };
  } }[];
}
export interface Diagnostic {
  code: string; severity: "warning" | "error"; recoverable: boolean;
  source: string; subject: string; timestamp: number; detail: string;
}
export interface BufferStats {
  pushedFrames: number; polledFrames: number; droppedFrames: number;
  skippedFrames: number; rejectedFrames: number;
}
export const SOURCE_PROFILES: Readonly<{ body: "mediapipe.body.v1"; hands: "mediapipe.hands.v1"; face: "mediapipe.face.v1" }>;
export const DIAGNOSTICS: Readonly<Record<string, string>>;
export class MediaPipeConnector {
  constructor(input?: { part?: Part; actorId?: string });
  open(config?: { sourceProfile?: string; coordinateConversion?: string; bufferMode?: BufferMode; bufferCapacity?: number }): { succeeded: boolean; message: string };
  close(): void;
  getState(): ConnectorState;
  getCapabilities(): readonly string[];
  getDiagnostics(): Diagnostic[];
  getBufferStats(): BufferStats;
  acquire(result: BodyResult | HandsResult | FaceResult, timestampMilliseconds: number, receiveTimestampSeconds: number): boolean;
  poll(): ObservationFrame | undefined;
}
