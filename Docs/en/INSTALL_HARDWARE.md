# Install Prince of Persia Classic on a real PS Vita (via FTP)

This guide assumes that you have already compiled `popclassic_audio.vpk` (see `portabilityplan.md` §8, or run
`./build_and_install.sh` to compile it — generates `build/popclassic_audio.vpk`; the older silent build
was `build/popclassic.vpk` and remains useful as a rollback). Covers everything you need to
transfer it to a real Vita via FTP with VitaShell, without needing a separate SD card/reader.

## 0. Requirements in the console

1. **Vita with CFW installed** (HENkaku/h-encore² or Ensō — whichever allows signed homebrew to run
   by taiHEN). If you don't have it jailbroken yet, follow the official guide for your firmware version before
   to continue; is outside the scope of this document.
2. **VitaShell** installed (the standard homebrew file explorer — it usually comes with CFW,
   or is installed like any `.vpk`). It is the one that will expose the FTP server.
3. **kubridge** installed (`ur0:tai/kubridge.skprx`, uploaded to taiHEN's `config.txt`). Necessary in
   real hardware (unlike Vita3K, which does not require it — see plan §9.2). Most CFWs
   moderns already bring it; if not, it is installed along with **NoNpDrm**/**FdFix** from the same VitaShell repo.
4. **`ur0:data/libshacccg.suprx`** present (Sony's shader compiler — without this, the game displays
   an explicit error dialog and does not start). Search "ShaRKBR33D" for legitimate ways to obtain it
   from your own console (it can't be redistributed, that's why it's in `.gitignore` as `*.suprx`).

## 1. Activate VitaShell FTP Server

1. On the Vita, open **VitaShell**.
2. Press **SELECT**. This shows the local IP of the console and the port of the FTP server (by default
   `1337`), something like:
   ```
   IP: 192.168.0.XXX Port: 1337
   ```
3. Write down that IP — you'll need it on your Mac. Leave VitaShell open with FTP active for the entire duration.
   process.
4. Confirm that the console and Mac are on the **same Wi-Fi network**.

## 2. Transfer files from Mac

With `curl` (it is already installed on macOS, nothing else is needed). Replace `192.168.0.XXX` with the real IP
that VitaShell showed you in the previous step.

```bash
export VITA_IP="192.168.0.XXX" # <-- enter the real IP of your console
cd "/Volumes/Seagate/PSVITA Develop/Prince of Persia "
```

### 2.1. Install the game (the `.vpk`)

VitaShell can install a `.vpk` directly if you upload it to `ux0:` and "open" it from there, but it is more
verbose copy it to a temporary folder and confirm the installation by FTP with the command protocol
VitaShell (port `1338`, the same one already used by the `send`/`send_kvdb` targets of `CMakeLists.txt`):

```bash
# Upload the vpk to ux0 :/
curl -T build/popclassic_audio.vpk "ftp://${VITA_IP}:1337/ux0:/popclassic_audio.vpk"
```

Then **in the console**: in VitaShell navigate to `ux0:/`, select `popclassic_audio.vpk` with the **X** button and
I chose **Install** from the context menu (**△** button). This installs the game with Title ID `POPC00001`
(defined in `CMakeLists.txt` as `VITA_TITLEID`). You can delete `ux0:/popclassic_audio.vpk` after installing.

### 2.2. Upload the assets (`ux0:data/popclassic/`)

The game expects to find, in console memory, exactly the tree assembled at `ux0_data/popclassic/`
of this repo (see plan §2.3/§2.4 and §9.9 for the details of each file):

```
ux0:data/popclassic/
├── libcocosdenshion.so
├── libcocos2d.so
├── libgame_logic.so
├── original.apk <- copy of original/*.apk (assets/appConfig.txt lives there)
├── main.1.org.ubisoft.premium.POPClassic.obb <- copy of original/*.obb (with that exact name)
├── appConfig.txt
├── assets/
│ └── appConfig.txt
├── save/ <- empty folder, the game writes its saves there
└── Data/
    ├── Animations/ Audio/ Effects/ Localization/
    ├── Logo/ Maps/ Particles/ Texture/
    └── ... (all contents of Data/, ~113 MB)
```

> [!IMPORTANT]
> `ux0_data/` is in `.gitignore` (they are the assets extracted from the original APK/OBB, copyrighted by
> Ubisoft — they are never uploaded to git, see `.gitignore`). If you cloned this repo on another machine, first you have to
> rebuild that folder following Phase 2 of the plan (`portabilityplan.md` §2) before this step.

Create the remote folder and upload everything at once with `curl` (recursive by `find`, since `curl` does not upload
entire directories in one fell swoop):

```bash
# Create base folder (curl -Q sends raw FTP commands before transfer)
curl -s "ftp://${VITA_IP}:1337/ux0:/data/" -Q "MKD popclassic" -Q "MKD popclassic/save" -Q "MKD popclassic/assets" > /dev/null

# Upload the entire tree preserving subfolders
cd ux0_data/popclassic
find . -type f | while read -r f; do
    remote_dir="ux0:data/popclassic/$(dirname "$f")"
    # create the remote subfolder if it does not exist (ignore error if it already exists)
    curl -s "ftp://${VITA_IP}:1337/${remote_dir}/" -Q "MKD $(dirname "$f")" > /dev/null 2>&1 || true
    echo "Uploading: $f"
    curl -s -T "$f" "ftp://${VITA_IP}:1337/ux0:/data/popclassic/${f#./}"
donated
cd ../..
```

This may take several minutes (~116 MB total). If you prefer a GUI instead of the terminal, any
normal FTP client (**Cyberduck**, **FileZilla**, **Transmit**) connecting to `ftp://<IP>:1337` without user/
password works the same — just drag the entire `ux0_data/popclassic/` folder to `ux0:/data/`
on the console.

## 3. First boot

1. On the Vita, go back to the **LiveArea** menu and look for the "Prince of Persia Classic" icon (Title ID
   `POPC00001`).
2. Open it. If `kubridge` and `libshacccg.suprx` are installed correctly, you should reach the same point as already
   tested on Vita3K (logo screen → menu, with readable text thanks to the fix in §9.16 of the plan).
3. If you crash immediately with an explicit error dialog, that dialog almost always says **what's missing**
   (e.g. "libshacccg.suprx is not installed") — review the corresponding step 0.
4. If it closes without any dialog (silent crash), use the `dump` target of `CMakeLists.txt`
   (`make dump`, requires the IP configured in `PSVITAIP` of `CMakeLists.txt` itself) to capture a
   core dump via FTP and analyze it — see `extras/scripts/get_dump.sh`.

## 4. Iterate without reinstalling the `.vpk` every time

While you're tweaking just the executable (not the assets), just replace `eboot.bin` directly
via FTP instead of reinstalling the entire `.vpk` each time:

```bash
curl -T build/eboot.bin "ftp://${VITA_IP}:1337/ux0:/app/POPC00001/eboot.bin"
```

(This is exactly what the `send`/`send_kvdb` targets already defined in `CMakeLists.txt` do, which also
restart the app automatically via VitaShell command port `1338` — `make send` if you have
`PSVITAIP` configured in the `cmake` cache.)

## Notes

- This guide is independent of the Vita3K tests documented in `plan_portibilidad.md` §9: several of
  The workarounds there (e.g. `EMULATOR_BUILD`, the relaxed `kubridge` check) are intended
  specifically so that they **do not** interfere with real hardware — always compile with `EMULATOR_BUILD=OFF`
  (the default value) for the console.
- The non-power-of-2 texture bug documented in §9.17 is specific to the Vita3K renderer
  (confirmed by reading its source code) — most likely on real hardware, where swizzling does it
  the GPU directly, that problem does not exist. If you manage to start a real game on console, it is a fact
  valuable to confirm or discard that hypothesis.