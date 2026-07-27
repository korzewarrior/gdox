# Run GDOX on Windows

1. Connect the supported `HL-DT-ST GP63EX70 RF02` USB drive.
2. Double-click `gdox.exe`.
3. If requested, drag your own 512-byte MCPX 1.0 dump and compatible
   256 KiB, 512 KiB, or 1 MiB Xbox BIOS onto the GDOX window.
4. Insert an original Xbox disc.

The xemu runtime and blank HDD template are already in this folder. GDOX
creates a private writable HDD and settings under your Windows account;
replacing this release folder does not replace saves or firmware. GDOX does
not install the inserted game during normal play.

GDOX uses the standard Windows optical-storage driver. Do not replace it with
WinUSB or install a third-party device driver.
