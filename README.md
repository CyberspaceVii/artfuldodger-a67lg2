# The Artful Dodger

Screenshot `FLAG_SECURE` windows without stripping the flag.

A KernelSU Next module for the **Foxxd A67L Gen 2**.

**[Download v0.5](https://github.com/CyberspaceVii/artfuldodger-a67lg2/releases/download/v0.5/artfuldodger-a67lg2.zip)** · [Changelog](CHANGELOG.md)

Press **Power + Volume Down** to capture the screen and save a PNG to
`Pictures/Screenshots`. A toast confirms when the image has been saved and
indexed in the gallery. Window flags stay intact, so ordinary screenshots,
screen recordings and Recents previews still respect `FLAG_SECURE`.

## Requirements

- Foxxd A67L Gen 2 with KernelSU Next
- An enabled Zygisk implementation; tested with ReZygisk
- Foreground user 0 with credential storage unlocked

Tested on this device's Android 16 firmware. Other devices and firmware builds
have not been validated. The installer checks the device and KernelSU;
[validation results](VALIDATION.md) describe the tested setup.

## Install or update

1. Download [`artfuldodger-a67lg2.zip`](https://github.com/CyberspaceVii/artfuldodger-a67lg2/releases/latest/download/artfuldodger-a67lg2.zip).
2. In KernelSU Next, open **Modules → Install from storage** and select the ZIP.
3. Reboot.

Updates use the same module ID, `artfuldodger_a67lg2`. If the older Foot Fetish
module is still installed, remove it before installing Artful Dodger. Run only
one interceptor for this key chord.

Capture starts on boot unless switched off. Tap **Action** to toggle it; the
setting survives reboot. Enabling starts the interceptor once the capture
worker is ready. To uninstall, remove the module in KernelSU Next and reboot.
Uninstall removes private state and pending images; saved gallery images remain.

## Status and recovery

From a root shell:

```sh
sh /data/adb/modules/artfuldodger_a67lg2/control.sh status
sh /data/adb/modules/artfuldodger_a67lg2/control.sh retry
sh /data/adb/modules/artfuldodger_a67lg2/control.sh diagnostics
```

`status` shows whether capture is enabled and actually ready, the number of
pending images, session counts, the last capture result and capture/save times.
`on` and `off` set the capture preference explicitly. `retry` attempts to save
pending images after a storage or gallery-indexing problem has been resolved.

Failed publication keeps its private source for retry. An identical file already
in the destination can be reindexed; a conflicting file is left alone. Pending
images are limited to 32. Recovery saves are reported separately from the
daemon's session counters.

Diagnostics are stored under root-only `/data/adb/artfuldodger`. Event records
contain request numbers, error codes and timings. They contain no image
contents, app names, window titles or screen text. Event and service logs are
rotated; idle status does not continuously write to storage.

## How it works

A root daemon grabs the hardware `gpio-keys` device and replays events through a
`uinput` clone. When the capture worker is ready, it consumes a fresh Power +
Volume-Down chord within 120 ms. Other events pass through. The consumed chord
does not invoke the stock screenshot animation, shutter or preview.

A Zygisk worker in `system_server` calls `ScreenCapture` with secure layers
enabled. Its library unloads from app processes. The worker and daemon use a
local sequenced-packet socket: the worker accepts UID 0, and the daemon checks
for UID 1000. A disconnect releases interception and the supervisor retries.

The worker writes a private PNG. A root helper publishes it without overwriting
existing images and checks the MediaStore entry before acknowledging success.
Capture compresses PNG on a dedicated `system_server` thread.

## Limits

Only one capture runs at a time. While busy, additional chords follow the stock
path, which can show the usual screenshot UI or trigger an app's screenshot
callback. The controlled test app received no official callback from custom
captures; other detection methods have not been ruled out.

Work profiles and other foreground users are unsupported. There is no separate
screen-lock policy. Hardware-protected DRM buffers and future firmware
compatibility have not been validated. PNG compression still runs inside
`system_server`; the bounded stress test does not establish long-term leak
freedom. Root and system processes are inside the trust boundary.

## Build and test

Run `./build.sh` with Android NDK r30. Set `NDK_DIR` if it is installed outside
`/opt/android-ndk`. C and C++ builds use `-Wall -Wextra -Werror`.

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Isource tests/native.c -o /tmp/artfuldodger-tests
/tmp/artfuldodger-tests
python3 tests/publisher.py
python3 tests/control.py
```

The host tests cover input handling, IPC, publication and status reporting.
See [VALIDATION.md](VALIDATION.md) for hardware results.

## License

[WTFPL](LICENSE).

CyberspaceVII · https://www.cyberspace7.org
