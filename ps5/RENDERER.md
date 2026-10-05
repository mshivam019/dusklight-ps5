# PS5 rendering

The playable build uses native Dawn over Vulkan/RADV with a PS5 direct-display
surface. Aurora retains its GX and RmlUi renderers, and Tint compiles the
shaders on the console. No browser is involved.

The measured output is 3840x2160 at 59.94 Hz, with frame interpolation capped
to 60 FPS. This preserves the original game simulation rate.

A full direct Vulkan Aurora renderer remains future work: its packet, resource,
GX and UI interfaces currently expose WebGPU types. The earlier direct Vulkan
UI proof is retained in the graphics-prototype branch and local console backups.
It is separate from this release.
