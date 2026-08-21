# Provenance

## external/ethos-u-core-driver/

- **Component**: Arm ethos-u-core-driver (bare-metal Ethos-U NPU driver)
- **Canonical upstream**: https://git.mlplatform.org/ml/ethos-u/ethos-u-core-driver.git
  (no anonymous smart-HTTP; not used for cloning)
- **Mirror used**: https://github.com/meta-pytorch/ethos-u-core-driver-mirror.git
  (archived; tag-stable through 24.11-rc2)
- **Pinned revision**: `5403fc9100a8764fe9b587fdbd310287eb2abd01` — upstream
  release tag **`24.08`** (also `24.08-rc2`; both lightweight tags on the
  mirror). Pair the driver release with the Vela compiler version used to
  produce your command streams; mismatched driver/Vela versions are the most
  common source of runtime errors.
- **Vendored**: 2026-08-21, as plain files (previously a git submodule).
  The NSX registry resolver materialises modules via `git clone` + `.git`
  stripping and does not initialise submodules, so the tree must be
  self-contained.
- **Local modifications**: none. The tree is byte-identical to the mirror at
  the pinned revision. Keep it that way — express Ambiq/NSX behavior as weak
  overrides or configuration in `src/`, never as edits under `external/`.
- **Known upstream issues** (present at the pinned revision, deliberately not
  patched locally):
  - `src/ethosu_log.h` line 55: `LOG()` expands `##__VA__ARGS__` (typo for
    `##__VA_ARGS__`). Latent — no driver source calls bare `LOG()`; the
    severity-level macros (`LOG_ERR`/`LOG_WARN`/`LOG_INFO`/`LOG_DEBUG`) are
    correct. Report upstream; pick up the fix on the next bump.

To verify parity: clone the mirror at the pinned revision and
`diff -rq --exclude=.git` against `external/ethos-u-core-driver/`.
