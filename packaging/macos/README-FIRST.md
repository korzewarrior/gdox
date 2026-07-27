# Run GDOX on macOS

1. Connect the supported `HL-DT-ST GP63EX70 RF02` USB drive.
2. Open `GDOX.app`.
3. If requested, drag your own 512-byte MCPX 1.0 dump and compatible
   256 KiB, 512 KiB, or 1 MiB Xbox BIOS onto the GDOX window.
4. Insert an original Xbox disc.

The universal xemu runtime and blank HDD template are inside the application.
GDOX creates a private writable HDD and settings in your Application Support
folder; replacing `GDOX.app` does not replace saves or firmware. GDOX does not
install the inserted game during normal play.

No kernel extension or optical driver is installed. GDOX uses Apple's native
USB and Disk Arbitration APIs. Local test builds are ad-hoc signed and may
require right-clicking the app and choosing **Open** once. Public builds must
be Developer ID signed and notarized before distribution.
