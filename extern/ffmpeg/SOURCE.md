# FFmpeg build input

Version embedded in the supplied headers: N-117227-ge932809b8f.
Corresponding upstream source: https://github.com/averne/FFmpeg/tree/e932809b8f
The matching source snapshot is included in upstream/.

The prebuilt Switch archives are retained as required build inputs. The source
snapshot and configuration are provided for rebuilding/review. This release did
not rebuild or retune these archives. devkitPro Switch portlibs supply their
additional dependencies.

License reported by libavcodec: GPL version 2 or later.

Configuration recovered from libavcodec:

```text
--prefix=/opt/devkitpro/portlibs/switch --enable-gpl --disable-shared --enable-static --cross-prefix=aarch64-none-elf- --enable-cross-compile --arch=aarch64 --cpu=cortex-a57 --target-os=horizon --enable-pic --extra-cflags='-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec' --extra-cxxflags='-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec' --extra-ldflags='-fPIE -L${PORTLIBS_PREFIX}/lib -L${DEVKITPRO}/libnx/lib' --disable-runtime-cpudetect --disable-programs --disable-debug --disable-doc --disable-autodetect --enable-asm --enable-neon --disable-avdevice --disable-encoders --disable-muxers --enable-swscale --enable-swresample --enable-network --disable-protocols --enable-protocol='file,http,ftp,tcp,udp,rtmp' --enable-zlib --enable-bzlib --enable-libass --enable-libfreetype --enable-libfribidi --enable-libdav1d --enable-nvtegra
```
