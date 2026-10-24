# RKNN Runtime ABI

Header: `rockchip-linux/rknpu2`, tag `v1.5.0`,
`runtime/RK356X/Linux/librknn_api/include/rknn_api.h`.
Git blob: `8007931f966fc8fb72e30dd96afc8027c25e0f9d`.

The repository's BSD 3-clause license is included as `LICENSE`.
The header's original notices are retained. No firmware runtime library is
redistributed here. The app loads the pen's existing OEM runtime by absolute
path and verifies the actual model/runtime through the C API.
