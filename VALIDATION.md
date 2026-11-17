# Validation

Hardware testing completed on 2026-11-16 using the code tested locally as v0.3
and v0.4. Release v0.5 changes version metadata, formatting, comments and docs;
its rebuilt native binaries are compared with those tested on the phone.

## Setup

- Foxxd A67L Gen 2, Android 16, build 1784605054
- Custom March r18 kernel, KernelSU Next and ReZygisk
- Foreground user 0 with credential storage unlocked
- Controlled test app with a colored `FLAG_SECURE` window

The older Foot Fetish module was removed. Testing used one capture interceptor.

## Capture and privacy

- Custom captures contained the expected test colors in portrait (720×1600) and
  landscape (1600×720).
- Standard screenshots and a short screen recording blacked out the secure
  content. The window retained `FLAG_SECURE` after custom captures.
- The capture library was present in `system_server` and absent from the test
  app's process mappings.
- The test app's Recents tile remained black. Its task identity was checked;
  no disk snapshot for that task was present.
- Custom captures did not increment the app's official screenshot callback.
  A stock screenshot control did increment it.

These results cover this test app and firmware. They do not establish immunity
to other screenshot detectors or protection of hardware DRM buffers.

## Input and service

Both chord orders and repeated-key events produced one capture per request.
Ordinary Power presses still slept and woke the display. Killing the daemon
released interception, and the supervisor restarted one instance. A duplicate
start was refused. The disabled preference survived a normal reboot; enabling
restored capture.

The socket rejected a native UID 2000 client before sending readiness, while a
UID 0 client received readiness. Both clients used the same test context to
isolate the credential check. An ordinary app socket probe also failed.

## Publication and recovery

An injected gallery-indexing failure returned error 27 and kept the private
source. Status reported the failure. After restoring the helper, `control.sh
retry` saved exactly one image, produced no duplicate and emptied the queue.

A queue of 32 controlled pending files returned error 23. Removing the test
files restored capture. A final capture had correct pixels and a successful
save acknowledgement. The test app, gallery images and device test tools were
removed afterwards.

Android testing exposed a Toybox/mksh file-locking incompatibility. The helper
was fixed to use bounded nonblocking flock retries with explicit descriptor
inheritance before the recovery and stress checks below.

## Repeated capture

Fifty consecutive secure captures passed pixel checks in 329 seconds.
`system_server` kept the same PID. RSS samples fluctuated from 320,588 to
307,948 KiB; descriptor counts fell from 733 to 723 and stayed there. This
bounded run showed no sustained growth and does not establish leak freedom
over hours or arbitrary applications.

## Local checks

- NDK r30 AArch64 C/C++ build with warnings treated as errors
- ASan/UBSan tests of the production chord state machine and IPC helpers
- Eight publisher tests and six control/status tests
- Shell syntax, source formatting and whitespace checks
- Release ZIP contents compared with source files
- No `libc++_shared.so` dependency in the capture library

## Untested or retained limits

Only unlocked foreground user 0 is supported. Work profiles, other users,
hardware-protected DRM and future firmware are unvalidated. PNG compression
runs on a dedicated `system_server` thread. Extra chords while busy follow the
stock path. Power long-hold/recovery combinations were not deliberately tested.

Peer credentials authenticate privilege, not executable identity. Root and
system processes remain inside the trust boundary. Abrupt process death can
leave partial temporary images that require manual cleanup.
