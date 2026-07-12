---
name: "Android and Native Decompilation"
description: "Decompile Android classes.dex to Java using Jadx and native .so files to C pseudo-code using devrvk/so-decompiler (Ghidra/Angr) via Docker."
---

# Android and Native Decompilation Skill

Use this skill whenever you need to automate the decompilation of Android apps (.apk or extracted `classes.dex`) and native libraries (`.so`).

## Decompiling Java (`classes.dex` -> `.java`)
Use the following Docker command to download and run Jadx statelessly (without needing an installation):

```bash
docker run --rm -v "$(pwd):/app" ubuntu:latest bash -c "
  cd /app
  apt-get update && apt-get install -y wget unzip default-jre
  wget -qO- https://github.com/skylot/jadx/releases/download/v1.4.7/jadx-1.4.7.zip > jadx.zip
  unzip -q jadx.zip -d jadx
  ./jadx/bin/jadx -d /app/apk_decompiled /app/path/to/classes.dex
  rm -rf jadx jadx.zip
"
```

## Decompiling C/C++ (`.so` -> `.c` pseudo-code)
Use the `devrvk/so-decompiler` Docker image to extract pseudo-code using Ghidra Headless and Angr.

```bash
docker run --rm --platform linux/amd64 -v "$(pwd):/app" devrvk/so-decompiler decompile /app/path/to/library.so /app/so_decompiled/output_folder_name
```

## Ready-to-Use Script
For standard PS Vita Android ports, a script named `decompile_all.sh` can be written to the project root to automate this entirely.
