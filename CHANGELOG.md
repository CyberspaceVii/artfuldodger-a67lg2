# Changelog

## v0.5 — 2026-11-16

First public release since v0.2. Includes the changes tested locally as v0.3
and v0.4.

- Replace the property trigger with authenticated local capture IPC and save
  acknowledgements. Confirm gallery indexing before showing success.
- Resolve capture interfaces before intercepting keys. Release interception on
  worker failure and restart the daemon through the supervisor.
- Fix handling of repeated keys, unrelated buffered events, late chords and
  input write failures. Reject duplicate daemons with a singleton lock.
- Cache JNI interfaces and release bitmap, hardware-buffer, stream and local
  references after each capture.
- Save through unique private files without overwriting existing screenshots.
  Retain failed publications for recovery, up to 32 pending images.
- Add `control.sh` status, on/off, retry and diagnostics commands. The Action
  button toggles capture and reports status; the preference survives reboot.
- Report specific capture, storage, indexing, conflict and timeout failures,
  session counts and capture/save times. Keep diagnostics private and bounded.
- Serialize publication and recovery with file locking compatible with Android
  Toybox and mksh. Reindex identical existing files without making duplicates.
- Restrict capture to unlocked foreground user 0.
- Add input/IPC, publisher and control tests. Validate secure portrait/landscape
  captures, 50 sequential captures, Recents protection, socket credentials and
  pending-image failure/recovery on the A67L Gen 2.
- Refresh installation, recovery and validation docs; format native source and
  trim redundant comments.

## v0.4 — local development

Status, diagnostics and pending-image recovery. Installed and tested locally;
included in v0.5.

## v0.3 — local development

Capture IPC, input handling, JNI cleanup and checked image publication. Installed
and tested locally; included in v0.5.

## v0.2

Screenshots of `FLAG_SECURE` windows without removing the flag on the Foxxd
A67L Gen 2.
