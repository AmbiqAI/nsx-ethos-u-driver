# Changelog

All notable changes to this project will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.2](https://github.com/AmbiqAI/nsx-ethos-u-driver/compare/nsx-ethos-u-driver-v0.1.1...nsx-ethos-u-driver-v0.1.2) (2026-08-22)


### Features

* CI smoke build + semaphore override with real timeout ([#6](https://github.com/AmbiqAI/nsx-ethos-u-driver/issues/6)) ([728aa52](https://github.com/AmbiqAI/nsx-ethos-u-driver/commit/728aa529b45e3b4d0e47160b6cd5dd792f85234e))


### Bug Fixes

* **smoke:** disable PIE for the partial-link acceptance on GNU hosts ([#8](https://github.com/AmbiqAI/nsx-ethos-u-driver/issues/8)) ([b58bef7](https://github.com/AmbiqAI/nsx-ethos-u-driver/commit/b58bef7623aebb12208efbabc8573af4a1458a5e))

## [0.1.1](https://github.com/AmbiqAI/nsx-ethos-u-driver/compare/nsx-ethos-u-driver-v0.1.0...nsx-ethos-u-driver-v0.1.1) (2026-08-21)


### Features

* **api:** add nsx_ethos_u_deinit and nsx_ethos_u_init_ex ([cc09edb](https://github.com/AmbiqAI/nsx-ethos-u-driver/commit/cc09edb0a95ac894350afe20dea3e9dabe0f70d0))
* **cmake:** default upstream driver logging off (NSX_ETHOSU_LOG_ENABLE) ([29e56a8](https://github.com/AmbiqAI/nsx-ethos-u-driver/commit/29e56a8aa377094316f76fc1933e71e0f7345866))
* initial nsx-ethos-u-driver scaffolding ([5eb51f7](https://github.com/AmbiqAI/nsx-ethos-u-driver/commit/5eb51f78b321826a8f01b94462b08a437c5fcf4c))
* make the module NSX-registry-resolvable and port PR-46-era fixes ([b10cd6b](https://github.com/AmbiqAI/nsx-ethos-u-driver/commit/b10cd6b552497b322bf092f0c73666d222c1d389))


### Bug Fixes

* **cache:** honor upstream NULL = whole-cache maintenance contract ([01fe517](https://github.com/AmbiqAI/nsx-ethos-u-driver/commit/01fe5170077efedbd2f953e77b62815cc9abfdb5))

## [Unreleased]

### Added
- Initial module scaffolding.
- Vendored Arm `ethos-u-core-driver` under `external/ethos-u-core-driver/`.
- NSX-flavoured facade in `includes-api/nsx_ethos_u.h`.
- CMSIS-based weak overrides for `ethosu_flush_dcache`,
  `ethosu_invalidate_dcache`, `ethosu_inference_begin`,
  `ethosu_inference_end`, and `ethosu_address_remap`.
