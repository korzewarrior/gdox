# Run GDOX on Linux

1. Open a terminal in this folder and run
   `sudo ./setup-device-access.sh` once.
2. Connect the supported `HL-DT-ST GP63EX70 RF02` USB drive.
3. Run `./gdox`.
4. If requested, drag your own 512-byte MCPX 1.0 dump and compatible
   256 KiB, 512 KiB, or 1 MiB Xbox BIOS onto the GDOX window.
5. Insert an original Xbox disc.

The xemu runtime and blank HDD template are already in this folder. GDOX
creates a private writable HDD and settings under your user account; replacing
this release folder does not replace saves or firmware. GDOX does not install
the inserted game during normal play.

The portable launcher reuses the verified runtime's bundled libraries and
includes libusb. A normal 64-bit Linux desktop still supplies its own glibc,
X11, OpenGL, and C++ runtime, as xemu's AppImage does.

The one-time device rule is required because raw optical commands must not run
as root. It grants the active desktop user access only to optical command
devices and the exact validated USB ID `0E8D:1887`.
