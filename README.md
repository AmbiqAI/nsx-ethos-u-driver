# nsx-ethos-u-driver

NSX integration of [Arm's ethos-u-core-driver][upstream] — a runtime-agnostic
module that lets any NSX consumer (HeliaAOT, HeliaRT, plain TFLM, custom C
runtimes) invoke Vela-compiled command streams on an Arm Ethos-U NPU.

[upstream]: https://git.mlplatform.org/ml/ethos-u/ethos-u-core-driver.git
            (mirror: https://github.com/meta-pytorch/ethos-u-core-driver-mirror)

## What this module is

- A thin NSX-flavoured facade over the upstream driver: see
  [`includes-api/nsx_ethos_u.h`](includes-api/nsx_ethos_u.h).
- A small set of weak-override sources that wire the driver into the NSX
  platform:
  - **`nsx_ethos_u_cache.c`** — CMSIS `SCB_CleanDCache_by_Addr` /
    `SCB_InvalidateDCache_by_Addr` overrides for `ethosu_flush_dcache` and
    `ethosu_invalidate_dcache`.
  - **`nsx_ethos_u_callbacks.c`** — default `ethosu_inference_begin` /
    `ethosu_inference_end` that fan out to a single, optional NSX probe.
    helia-profiler and similar tools subscribe via `nsx_ethos_u_set_probe()`.
  - **`nsx_ethos_u_remap.c`** — identity `ethosu_address_remap`. BSPs whose
    NPU sees memory through a different aperture (e.g. an in-package DRAM
    window) provide a strong override in their own translation unit.
  - **`nsx_ethos_u.c`** — single-handle convenience wrapper:
    `nsx_ethos_u_init()` and `nsx_ethos_u_irq()`.
- A CMake target `nsx::ethos_u_driver` that consumers link against.

## Where it fits

- Vela remains the host-side compiler that produces the command stream.
  This module is the runtime-side driver layer that consumes that stream on
  the target.
- HeliaRT, HeliaAOT, TFLM, and other runtimes sit above this layer and use
  `nsx::ethos_u_driver` as a regular dependency.
- Board and BSP layers still provide the NPU base address, IRQ number, and
  vector wiring. This module stays focused on the reusable driver and NSX
  integration surface rather than board-specific setup.

## Vendoring

The upstream driver lives at `external/ethos-u-core-driver/` as a plain
vendored snapshot (not a git submodule): the NSX registry resolver
materialises modules with `git clone` + `.git` stripping and does not
initialise submodules, so the tree must be self-contained. Provenance
(upstream URL, mirror, pinned revision) is recorded in
[`PROVENANCE.md`](PROVENANCE.md).

To upgrade upstream, replace the tree with the new revision's files and
update `PROVENANCE.md` in the same commit:

```sh
git clone https://github.com/meta-pytorch/ethos-u-core-driver-mirror.git /tmp/eucd
git -C /tmp/eucd checkout <tag-or-sha>
rsync -a --delete --exclude=.git /tmp/eucd/ external/ethos-u-core-driver/
git add external/ethos-u-core-driver PROVENANCE.md
git commit -m "deps(ethos-u-core-driver): bump to <tag-or-sha>"
```

Keep the Arm source pristine — express Ambiq/NSX behavior as weak
overrides or config in `src/`, never as edits inside `external/`.

Pin to the version that matches the Vela compiler used to produce the
command streams your apps run. Mismatched driver/Vela versions are the most
common source of runtime errors.

## Build model

We compile the upstream sources directly under NSX toolchain flags rather
than using upstream's own CMakeLists. Upstream drives target-CPU selection
via `CMAKE_SYSTEM_PROCESSOR` (or `TARGET_CPU` with ethos-u-core-platform
toolchain files); NSX drives it via `nsx::soc_flags`, which carries the
active SoC's CPU/FPU/ABI/compiler facts transitively. That keeps this module
aligned with the newer neuralSPOT-X split between generic SoC facts and
board-only facts, while still ensuring the upstream TUs build with the same
flags as every other NSX module.

See [`CMakeLists.txt`](CMakeLists.txt) for the full rationale.

## Configuration

Set in your board (or app) before adding the module:

| Variable | Default | Purpose |
| --- | --- | --- |
| `NSX_ETHOSU_NPU_CONFIG` | `ethos-u85-256` | Vela / driver NPU config (`ethos-uNN-MACS`). |
| `NSX_ETHOSU_BUILD_PMU` | `ON` | Build the upstream PMU helper TU. |
| `NSX_ETHOSU_LOG_ENABLE` | `OFF` | Enable upstream `LOG()`/`fprintf` driver logging. |
| `NSX_ETHOSU_INFERENCE_TIMEOUT_MS` | *(empty)* | Deadline in ms for `ethosu_wait()`. Empty = upstream's "wait forever"; otherwise 1…4294967295 (0 is rejected — it would time out every inference instantly). |

The CMakeLists parses the family token (`u55` / `u65` / `u85`) from
`NSX_ETHOSU_NPU_CONFIG` and selects the matching `ethosu_device_uNN`
source. The family is exposed to consumers as the public compile
definitions `ETHOSU_ARCH`, `ETHOSU_MACS` and `ETHOSU55`/`ETHOSU65`/`ETHOSU85`
— the names upstream's own sources actually gate on. (There is no
`ETHOSU_TARGET_NPU_CONFIG` define; it is a cache variable read only by
upstream's own CMakeLists, which this module does not `add_subdirectory()`.)

`NSX_ETHOSU_INFERENCE_TIMEOUT_MS` has to be set at *library* configure time:
it becomes `ETHOSU_SEMAPHORE_WAIT_INFERENCE`, which upstream's
`ethosu_driver.c` consumes when the library is compiled, so an app-side
`#define` would have no effect. A finite value only takes effect once the
application also supplies the `nsx_ethos_u_ticks()` /
`nsx_ethos_u_ticks_per_ms()` timebase hooks documented in
`includes-api/nsx_ethos_u.h`; without them the wait stays unbounded, exactly
as upstream behaves.

## Consuming the module

```cmake
target_link_libraries(my_app PRIVATE nsx::ethos_u_driver)
```

`PRIVATE` is correct for a **final** target (an executable, or a library
nothing else links). A library that re-exports the driver to its own
dependents — a BSP or HAL wrapper — **must** link it `PUBLIC`:

```cmake
target_link_libraries(my_bsp PUBLIC nsx::ethos_u_driver)   # correct
target_link_libraries(my_bsp PRIVATE nsx::ethos_u_driver)  # silently broken
```

The weak-symbol overrides reach a link line as object files carried on the
driver's `INTERFACE_SOURCES`. `PRIVATE` wraps the dependency in
`$<LINK_ONLY:...>`, which propagates the link requirement but strips every
usage requirement, sources included — so `my_bsp`'s dependents link the
archive without the overrides and quietly fall back to upstream's
`malloc()`-based semaphore and no-op cache maintenance. There is no warning.
`tests/smoke` builds this exact topology (`nsx_ethos_u_link_transitive`) and
fails the build if the overrides go missing.

Minimal app code:

```c
#include "nsx_ethos_u.h"

static struct ethosu_driver g_drv;

void NPU_IRQHandler(void) { nsx_ethos_u_irq(); }

int main(void) {
    if (nsx_ethos_u_init(&g_drv, (void *)NPU_BASE_ADDR, NPU_IRQ_NUM) != 0) {
        /* handle error */
    }

    int rc = ethosu_invoke(&g_drv,
                           cmd_stream, cmd_stream_size,
                           base_addrs, base_addr_sizes, n_bases);
    /* ... */
}
```

## License

Apache-2.0. The vendored upstream driver is also Apache-2.0 and retains its
own copyright headers.
