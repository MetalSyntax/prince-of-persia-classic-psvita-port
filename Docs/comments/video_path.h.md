# video_path.h – Developer Comment Reference

Extracted and translated developer/technical comments from
[`source/video_path.h`](../../source/video_path.h).

---

## sanitize_video_path – Normalizing Missing Resolution-Folder Requests

**Location:** inline comment inside `sanitize_video_path()`, immediately
before the final filename normalization.

Requests may arrive without the `High/` resolution folder, or as a bare
filename (e.g. just `PoP_V1_1.mp4`), when the enum-to-path mapping the game
performs internally does not preserve the full Android asset path. Because
of this, `sanitize_video_path()` does not trust whatever resolution folder
(if any) is present in the incoming request: it discards everything above
the filename and always normalizes the result to land under `Video/Mid/`.
