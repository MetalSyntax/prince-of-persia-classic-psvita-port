# Install Prince of Persia Classic on a real PS Vita (via FTP)

This guide assumes you have already compiled `popclassic.vpk` (see `plan_portabilidad.md` §8, or run
`./build_and_install.sh` to compile it — it generates `build/popclassic.vpk`). It covers everything needed to
transfer it to a real Vita via FTP using VitaShell, without needing a separate SD card/reader.

## 0. Console requirements

1. **Vita with installed CFW** (HENkaku/h-encore² or Ensō — anything that allows running taiHEN signed
   homebrew). If you haven't jailbroken it yet, follow the official guide for your firmware version before
   continuing; it's outside the scope of this document.
2. **VitaShell** installed (the standard homebrew file explorer — usually comes with CFW,
   or is installed like any `.vpk`). This will expose the FTP server.
3. **kubridge** installed (`ur0:tai/kubridge.skprx`, loaded in taiHEN's `config.txt`). Required on
   real hardware (unlike Vita3K, which doesn't require it — see plan §9.2). Most modern CFWs
   already include it; if not, it is installed alongside **NoNpDrm**/**FdFix** from the same VitaShell repo.
4. **`ur0:data/libshacccg.suprx`** present (Sony's shader compiler — without this, the game shows
   an explicit error dialog and won't start). Search for "ShaRKBR33D" for legitimate ways to obtain it
   from your own console (it cannot be redistributed, which is why `*.suprx` is in `.gitignore`).

## 1. Activate VitaShell FTP server

1. On the Vita, open **VitaShell**.
2. Press **SELECT**. This shows the console's local IP and the FTP server port (default
   `1337`), something like:
   ```
   IP: 192.168.0.XXX  Port: 1337
   ```
3. Note that IP — you'll need it on the Mac. Keep VitaShell open with FTP active throughout the whole
   process.
4. Confirm that the console and the Mac are on the **same Wi-Fi network**.

## 2. Transfer files from the Mac

Using `curl` (already installed on macOS, nothing else is needed). Replace `192.168.0.XXX` with the real IP
that VitaShell showed you in the previous step.

```bash
export VITA_IP="192.168.0.XXX"   # <-- put your console's real IP
cd "/Volumes/Seagate/PSVITA Develop/Prince of Persia "
```

### 2.1. Install the game (the `.vpk`)

VitaShell can install a `.vpk` directly if you upload it to `ux0:` and "open" it from there, but it's
cleaner to copy it to a temporary folder and confirm the installation via FTP using VitaShell's command protocol
(port `1338`, the same used by the `send`/`send_kvdb` targets in `CMakeLists.txt`):

```bash
# Uploads the vpk to ux0:/
curl -T build/popclassic.vpk "ftp://${VITA_IP}:1337/ux0:/popclassic.vpk"
```

Then, **on the console**: in VitaShell navigate to `ux0:/`, select `popclassic.vpk` with the **X** button and
choose **Install** from the context menu (**△** button). This installs the game with Title ID `POPC00001`
(defined in `CMakeLists.txt` as `VITA_TITLEID`). You can delete `ux0:/popclassic.vpk` after installing.

### 2.2. Upload assets (`ux0:data/popclassic/`)

The game expects to find, in the console's memory, the exact tree assembled in `ux0_data/popclassic/`
of this repo (see plan §2.3/§2.4 and §9.9 for details on each file):

```
ux0:data/popclassic/
├── libcocosdenshion.so
├── libcocos2d.so
├── libgame_logic.so
├── original.apk                                  <- copy of original/*.apk (assets/appConfig.txt lives there)
├── main.1.org.ubisoft.premium.POPClassic.obb      <- copy of original/*.obb (with that exact name)
├── appConfig.txt
├── assets/
│   └── appConfig.txt
├── save/                                          <- empty folder, game writes its saves there
└── Data/
    ├── Animations/  Audio/  Effects/  Localization/
    ├── Logo/  Maps/  Particles/  Texture/
    └── ... (all Data/ content, ~113 MB)
```

> [!IMPORTANT]
> `ux0_data/` is in `.gitignore` (these are assets extracted from the original APK/OBB, copyrighted by
> Ubisoft — they are never uploaded to git, see `.gitignore`). If you cloned this repo on another machine, you must first
> rebuild that folder by following Phase 2 of the plan (`plan_portabilidad.md` §2) before this step.

Create the remote folder and upload everything at once with `curl` (recursive via `find`, since `curl` doesn't upload
complete directories at once):

```bash
# Creates the base folder (curl -Q sends raw FTP commands before transfer)
curl -s "ftp://${VITA_IP}:1337/ux0:/data/" -Q "MKD popclassic" -Q "MKD popclassic/save" -Q "MKD popclassic/assets" > /dev/null

# Uploads the whole tree preserving subfolders
cd ux0_data/popclassic
find . -type f | while read -r f; do
    remote_dir="ux0:data/popclassic/$(dirname "$f")"
    # creates the remote subfolder if it doesn't exist (ignores error if it already exists)
    curl -s "ftp://${VITA_IP}:1337/${remote_dir}/" -Q "MKD $(dirname "$f")" > /dev/null 2>&1 || true
    echo "Uploading: $f"
    curl -s -T "$f" "ftp://${VITA_IP}:1337/ux0:/data/popclassic/${f#./}"
done
cd ../..
```

This may take several minutes (~116 MB total). If you prefer a GUI instead of the terminal, any
standard FTP client (**Cyberduck**, **FileZilla**, **Transmit**) connecting to `ftp://<IP>:1337` without username/
password works too — simply drag the complete `ux0_data/popclassic/` folder to `ux0:/data/`
on the console.

## 3. First boot

1. On the Vita, return to the **LiveArea** menu and look for the "Prince of Persia Classic" icon (Title ID
   `POPC00001`).
2. Open it. If `kubridge` and `libshacccg.suprx` are installed correctly, it should reach the same point already
   tested in Vita3K (logo screen → menu, with readable text thanks to the fix in §9.16 of the plan).
3. If it crashes immediately with an explicit error dialog, that dialog almost always says **what's missing**
   (e.g. "libshacccg.suprx is not installed") — review the corresponding step 0.
4. If it closes without any dialog (silent crash), use the `dump` target from `CMakeLists.txt`
   (`make dump`, requires the IP configured in `PSVITAIP` of `CMakeLists.txt` itself) to capture a
   core dump via FTP and analyze it — see `extras/scripts/get_dump.sh`.

## 4. Iterating without reinstalling the `.vpk` every time

While you're just tweaking the executable (not the assets), replacing `eboot.bin` directly via FTP
is enough instead of reinstalling the complete `.vpk` every time:

```bash
curl -T build/popclassic.vpk/eboot.bin "ftp://${VITA_IP}:1337/ux0:/app/POPC00001/eboot.bin"
```

(This is exactly what the `send`/`send_kvdb` targets defined in `CMakeLists.txt` already do, which also
automatically restart the app via VitaShell's command port `1338` — `make send` if you have
`PSVITAIP` configured in the `cmake` cache.)

## Notes

- This guide is independent of the Vita3K tests documented in `plan_portabilidad.md` §9: several of
  the workarounds there (e.g. `EMULATOR_BUILD`, the relaxed `kubridge` check) are specifically intended
  so that they **do not** interfere with real hardware — always compile with `EMULATOR_BUILD=OFF`
  (the default value) for the console.
- The non-power-of-2 texture bug documented in plan §9.17 is specific to the Vita3K renderer
  (confirmed by reading its source code) — it is very likely that on real hardware, where swizzling is done
  directly by the GPU, that problem does not exist. If you manage to start a real game on console, it is
  valuable data to confirm or rule out that hypothesis.
