# security

## private reports

report a suspected vulnerability through
[github private vulnerability reporting](https://github.com/korzewarrior/gdox/security/advisories/new).

do not open a public issue for a report that contains an unpatched
vulnerability, private path, device identifier, firmware, key, credential, or
game data.

include:

- gdox version and host platform
- affected system and exact drive identity when relevant
- the smallest reproducible sequence
- observed and expected behavior
- logs with personal paths, serials, firmware, keys, and game data removed

do not attach proprietary firmware, console keys, game images, or decrypted
security data.

## scope

security reports include:

- a drive command or state change outside the documented profile boundary
- persistent drive or firmware modification
- access outside a user-selected output path
- unintended network exposure
- release-signature or checksum verification failures
- disclosure of firmware, keys, private paths, device identifiers, or game
  data

an unsupported drive, unreadable disc, emulator failure, or ordinary
compatibility problem is not by itself a security vulnerability. report those
through a public issue or the project discord after removing private data.

## supported release

the current active development release receives security review and fixes.
older development releases may be used to reproduce a report, but fixes are
made against the current release.
