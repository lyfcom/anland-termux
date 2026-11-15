# Mouse input delivery

This fork requests unbuffered delivery for POINTER and TRACKBALL source classes
when the root view attaches. Ordinary mice use POINTER; captured relative mice
use TRACKBALL. Android API 30 is the minimum supported version.

The request belongs to the root view because it handles captured pointer events
and contains the display surface. Attachment ensures the view has a ViewRootImpl.
Rendering, pointer coordinates, acceleration, and capture policy are unchanged.

## Signed builds

The main-branch workflow builds only the standard transport variant. Repository Actions
Secrets: ANLAND_KEYSTORE_B64, ANLAND_KEY_ALIAS, ANLAND_STORE_PASSWORD,
ANLAND_KEY_PASSWORD. The standard variant retains the upstream Termux GitHub
certificate because its shared UID requires the same signer as Termux. This is
the upstream public test key, not a new private production identity.

For a local build, set the same ANLAND_* environment variables and use Gradle
9.7.1 with JDK 21, Android 37.2, and NDK 29.0.14206865. Run tools/build-app.sh.
Without these variables, local builds retain the upstream test-key defaults.

Build success and signature verification do not measure pointer latency.
Compare touchpad and external-mouse behavior on the device after installation.

## Refresh rate

Display settings lists Automatic and the rates advertised for the current panel
resolution. Manual selection requests the matching display mode at the same panel resolution
and its window/surface rate on desktop resume and
surface creation/change. Automatic clears the mode and rate requests. The existing display
listener forwards the actual rate to KWin, so an OS limit cannot make the desktop
assume the requested rate was granted. The preference is included in settings
backup. No global Android settings are changed.
