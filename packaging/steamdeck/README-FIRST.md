# Install GDOX on Steam Deck

1. Switch to Desktop Mode and extract this archive.
2. Open the extracted folder and run `install.sh`.
3. Complete the single administrator prompt for USB-drive access.
4. Return to Gaming Mode and launch **GDOX** from the Non-Steam library.

The installer creates one Steam shortcut with library artwork. Reinstalling
updates that shortcut instead of adding another copy. GDOX opens at the Deck's
1280 x 800 display size; xemu starts fullscreen with a 16:9 game picture and
2x internal rendering by default. Select **Quit** at the top right to close
GDOX.

The bundled xemu runtime and blank Xbox HDD are included. Firmware you provide,
xemu configuration, and game saves stay in private home-directory data and are
not replaced by application updates. Preservation output defaults to the same
persistent data area and can be redirected from Sources.

Connect the supported `HL-DT-ST GP63EX70 RF02` drive before inserting a disc.
If the Deck account has no administrator password yet, set one once with
`passwd` in Konsole, then rerun the installer.
