# CONTROL Resonant / Adreno 830 – GPU-generated indirect draw investigation

**Status:** work in progress, **not a patched Turnip driver**.

Target: preserve the 52-byte `INDEX_BUFFER_VIEW + 4x32-bit CONSTANT + DRAW_INDEXED` D3D12 stream produced by `CONTROLResonant.exe` without dropping state changes when `VK_EXT_device_generated_commands` is absent.

The original VKD3D-Proton 3.0.1 `d3d12_command_list_ExecuteIndirect` returns early on this signature, so a standard indexed indirect draw is not equivalent.

## Planned integration criteria

1. Process indirect data on GPU, respecting the GPU-generated sequence count and the 52-byte stride.
2. Bind the appropriate index buffer address, length and index format for **each** draw.
3. Map the root constant range into the actual PSO's shared/per-stage constant registers.
4. Preserve vertex offset, first instance and correct shader driver parameter upload.
5. Synchronize producer shader writes, command stream preprocessing, and graphics execution.
6. Support Vulkan DGC entrypoints, feature/property reporting and per-layout requirements **correctly**, including preprocess/memory ownership.
7. Compile on Android AArch64, pass Vulkan conformance for the implemented subset, then verify CONTROL Resonant renders with the same graphics features as the original.

The reference shader and CPU encoder are tests of stream layout only. Do not publish/install a driver until all criteria have been implemented.
