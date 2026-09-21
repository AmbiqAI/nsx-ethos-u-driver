# Provenance

## external/ethos-u-core-driver/

- **Component**: Arm ethos-u-core-driver (bare-metal Ethos-U NPU driver)
- **Canonical upstream**: https://gitlab.arm.com/artificial-intelligence/ethos-u/ethos-u-core-driver.git
  (anonymous HTTPS clone works; this is where Arm publishes release tags).
  The former `git.mlplatform.org` host redirects here. The GitHub mirror used
  for earlier snapshots (`meta-pytorch/ethos-u-core-driver-mirror`) is archived
  and stops at `24.11-rc2`; do not use it for new bumps.
- **Pinned revision**: `5f0b9d1e17b245aefc308ee0d5a4543833b96b11` — upstream
  release tag **`26.08`** (driver API version 2.0.0). Pair the driver release
  with the Vela compiler version used to produce your command streams;
  mismatched driver/Vela versions are the most common source of runtime errors.
- **Vendored**: 2026-09-18, as plain files. The NSX registry resolver
  materialises modules via `git clone` + `.git` stripping and does not
  initialise submodules, so the tree must be self-contained.
- **Local modifications**: none. The tree is byte-identical to upstream at the
  pinned revision. Keep it that way — express Ambiq/NSX behavior as weak
  overrides or configuration in `src/`, never as edits under `external/`.
- **Why 26.08 (history)**: the previous pin, `24.08`, carried an upstream bug
  in the Ethos-U85 PMU: the `ETHOSU85` symbolic event enum had one entry more
  than the hardware-event table it indexed, so every event after
  `CC_STALLED_ON_BLOCKDEP` programmed the *next* hardware event
  (`NPU_ACTIVE` counted `MAC_ACTIVE`, `MAC_ACTIVE` counted `MAC_DPU_ACTIVE`,
  `WD_ACTIVE` counted `WD_STALLED`, `*_DATA_BEAT_*` counted
  `*_TRAN_REQ_STALLED`). Upstream fixed it in `6a10d3c8` (24.11) and replaced
  the index lookup with explicit switches in 2.0.0.
  `tests/smoke/unit/test_pmu_event_map.c` guards against a recurrence.
- **Integration notes for 2.0.0** (what a future bump should re-check):
  - Sources are now `ethosu_driver.c`, `ethosu_backend_uNN.c`,
    `ethosu_pmu_uNN.c` (mandatory: `ethosu_init()` binds `drv->pmu` to it) and
    `ethosu_pmu.c` (public `ETHOSU_PMU_*` API). `CMakeLists.txt` lists them by
    name. `ETHOSU_MULTI_VARIANT` stays undefined (single-variant build, legacy
    `ethosu_init()` / `ethosu_reserve_driver()` API).
  - The cache hooks changed signature to
    `(const uint64_t *base_addr, const size_t *base_addr_size, int num_base_addr)`
    and are called once per inference with the whole base-pointer table; the
    command stream is no longer passed separately. `src/nsx_ethos_u_cache.c`
    cleans every populated region before dispatch and invalidates it after.
  - Hook ordering changed: `ethosu_invalidate_dcache()` now runs *before*
    `ethosu_inference_end()` (24.08 ran it after), so anything measured between
    the begin/end probes includes the post-inference cache invalidation. On
    the Atomiq110 FPGA (KWS, 256 KB arena) that adds ~208k NPU clock cycles to
    PMU `CYCLE`/`NPU_IDLE`; `NPU_ACTIVE` and `MAC_ACTIVE` are unaffected.
    Profilers should treat `NPU_ACTIVE` as NPU run time, not the probe window.
  - New optional weak hook `ethosu_config_select()`; not overridden.
  - `struct ethosu_driver` layout changed (adds `pmu`); everything that embeds
    it must be rebuilt against these headers.
- **Known upstream issues**: none tracked at this revision.

To verify parity: clone the canonical upstream at the pinned revision and
`diff -rq --exclude=.git` against `external/ethos-u-core-driver/`.
