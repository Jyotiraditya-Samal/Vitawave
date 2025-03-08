# VitaWave

A music player homebrew for the PS Vita.

Work in progress.

## Building

Requires VitaSDK.

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
make
```
