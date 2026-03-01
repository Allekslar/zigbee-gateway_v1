<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Integration Tests (Host)

This package covers integration scenarios for modules that are not included in the `test/host` build:

- `components/web_ui/web_server.cpp`
- `components/web_ui/web_routes.cpp`
- `components/web_ui/web_handlers_device.cpp`
- `components/web_ui/web_handlers_static.cpp`
- `components/app_hal/hal_mdns.c`
- `components/app_hal/hal_spiffs.c`
- `components/app_hal/hal_matter.c`
- `components/app_hal/hal_rcp.c`

## Run

```bash
cmake -S test/integration -B build-integration
cmake --build build-integration -j
ctest --test-dir build-integration --output-on-failure
```
