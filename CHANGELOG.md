# Changelog

All notable changes to this project will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Initial module scaffolding.
- Vendored Arm `ethos-u-core-driver` as a git submodule under
  `external/ethos-u-core-driver/`.
- NSX-flavoured facade in `includes-api/nsx_ethos_u.h`.
- CMSIS-based weak overrides for `ethosu_flush_dcache`,
  `ethosu_invalidate_dcache`, `ethosu_inference_begin`,
  `ethosu_inference_end`, and `ethosu_address_remap`.
