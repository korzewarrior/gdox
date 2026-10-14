# Wii direct-disc playback

Wii support is in development and is not in the published 0.3.0 release.

The Linux reader accepts single-layer Wii discs in an LG GP63EX70 with RF02
firmware and USB ID `0e8d:1887`. Select the drive and choose **Start Dolphin**.
Configure a Wii Remote and Nunchuk in the game window's **Controllers** settings.

The disc stays in the drive. GDOX uses the existing 16 MiB RAM buffer and
read-ahead; it does not save a disc image. Dolphin handles Wii partitions and
keeps saves, settings, and shader caches in the normal GDOX user directory.

## Reader

The header selects GameCube or Wii capacity before playback. Wii partition
tables and data extents must fit the 2,294,912-sector single-layer layout.
Larger layouts are rejected. Every sector still requires physical-address
and EDC validation, and transient RF02 admission fields are restored after
each acquisition.

The existing `GDOX_BUILD_GAMECUBE` build option controls both Nintendo readers.

## Tested

- The Legend of Zelda: Twilight Princess, `RZDE01`, disc revision 2.
- Reads through the final sector, including regions beyond GameCube capacity.
- Boot, title sequence, save creation, and the new-game opening through the
  GDOX disc bridge, using an emulated Wii Remote and Nunchuk.

Optical seeks can still pause playback; the longest measured read took 2.6 seconds.

Dual-layer discs, other drives, Windows, macOS, and Steam Deck Wii playback
have not been validated.
