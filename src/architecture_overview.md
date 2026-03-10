# Camera Stream & Recording Data Flow

This document describes the architectural flow of video data, metadata, and OCR results within the VEDA project, updated to reflect the `MainWindowController` integration for recording and captures.

## Current Data Flow Diagram (PlantUML)

@startuml
title Camera Stream Data Flow (Updated)

node "Camera" as Camera

component "CameraManager\n(per camera)" as Manager {
  component "VideoThread\n(display)" as DisplayThread
  component "MetadataThread" as MetadataThread
  component "VideoThread\n(OCR)" as OcrThread
}

component "CameraSource\n(per camera)" as Source {
  [ROI Service]
  [Metadata Objects]
}

component "MainWindowController" as MWC {
  [onRawFrameReady()]
  [getBufferByIndex()]
}

package "Display Path" {
  component "Primary/Secondary UI" as UI
  component "Thumbnail" as Thumbnail
}

package "Recording / Capture Path" {
  component "Continuous Buffers\n(Throttled 5 FPS)" as ContBuffer
  component "Manual/Event Buffers\n(Full FPS)" as ManualBuffer
  component "MediaRecorderWorker\n(Background Thread)" as Recorder
}

package "Analysis Path" {
  component "PlateOcrCoordinator" as OcrCoordinator
}

' Connections
Camera --> DisplayThread : RTSP
Camera --> MetadataThread : RTSP
Camera --> OcrThread : RTSP

DisplayThread --> Source : frameCaptured()
MetadataThread --> Source : metadataReceived()
OcrThread --> Source : ocrFrameCaptured()

Source --> Thumbnail : thumbnailFrameReady()
Source --> UI : displayFrameReady()

' Distribution to Controller
Source --> MWC : rawFrameReady(Mat, cardIdx)

' Controller Distributing to Buffers
MWC --> ContBuffer : throttled addFrame()
MWC --> ManualBuffer : direct addFrame()

' Controller Coordinating Storage
MWC --> Recorder : saveVideo(frames)
Recorder --> [Disk] : .mp4

Source --> OcrCoordinator : OCR request / result

note right of Manager
One CameraManager per camera
Contains specialized VideoThreads
end note

note right of Source
One CameraSource per camera
Central proxy for UI & Analysis
Emits signals for all frames
end note

note bottom of MWC
Distributes raw frames to
appropriate buffers and handles
manual/event recording triggers.
end note
@enduml

## Description of Components

### CameraSource
The central proxy for each camera. It receives frames and metadata from the `CameraManager` and distributes them to the UI and other subscribers via Qt signals. It now emits `rawFrameReady` to provide raw `cv::Mat` frames for recording.

### MainWindowController
Acts as the global coordinator. It subscribes to `rawFrameReady` from all `CameraSource` instances and:
1.  **Throttles** frames for the **Continuous Recording** buffers (targets ~5 FPS).
2.  **Feeds** the **Manual/Event** buffers at full capture rate.
3.  **Triggers** the `MediaRecorderWorker` when a manual record is toggled or an event is detected.

### Recording Buffers
- **Continuous Buffer**: Circular buffers maintaining recent frames for 1-minute interval segments.
- **Manual/Event Buffer**: Larger buffers (e.g., 600 frames) used to extract "Pre-Event" and "Post-Event" video clips.

### MediaRecorderWorker
A background worker running in its own thread to avoid blocking the UI. It uses OpenCV `VideoWriter` to encode and save frame sequences to disk.
