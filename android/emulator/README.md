# android emulator core

the android build starts from xemu v0.8.133 and applies the ordered patches in
`patches/series`.

```sh
scripts/prepare_android_emulator.sh
```

the patches add android support, the gdox disc boundary, runtime fixes, small
hot-path changes, and an arm64 coroutine context. `gdox_qemu_disc.c` remains
first-party gdox code outside the recovered patch series.

the series leaves out hakuX's later speculative cpu, gpu, cache, draw-order,
and timing changes. new emulator changes need a narrow reason and a physical
hardware test.

xemu and QEMU keep their original GPL/LGPL licensing. the recovered android
work is tracked in `android/dependencies.lock`. a public apk includes its
complete corresponding source.
