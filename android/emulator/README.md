# android emulator core

the android build starts from xemu v0.8.133 and applies the ordered patches in
`patches/series`.

```sh
scripts/prepare_android_emulator.sh
```

the patches add android support, the gdox disc boundary, runtime fixes, small
hot-path changes, an arm64 coroutine context, volatile hard-disk backing, and
the schema-3 save vault. `gdox_qemu_disc.c` remains first-party gdox code
outside the recovered patch series.

android targets original xbox through xemu. it does not include xbox 360 or
xenia support.

the save contract persists the hard-disk configuration/profile region,
logical `UDATA`, and only `TDATA` selected by positively reviewed title rules.
game content and caches remain transient. migration removes a legacy managed
hard disk only after a fresh verified round trip proves complete source
projection with no unclassified `TDATA`. differing same-path save or
configuration entries remain in the current vault; nonconflicting saves are
merged, playback continues, and the legacy source is preserved.

the series leaves out hakuX's later speculative cpu, gpu, cache, draw-order,
and timing changes. new emulator changes need a narrow reason and a physical
hardware test.

xemu and QEMU keep their original GPL/LGPL licensing. the recovered android
work is tracked in `android/dependencies.lock`. a public apk includes its
complete corresponding source.
