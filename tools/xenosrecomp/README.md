# XenosRecomp, Naruto: Rise of a Ninja fork

Fork of [hedge-dev/XenosRecomp](https://github.com/hedge-dev/XenosRecomp) from `990d03b28a27b50277ee5d8d942e1c5f873869d1`. The upstream MIT license is in [LICENSE.md](LICENSE.md).

The changes live under `NARUTO_RECOMP` (defined in `XenosRecomp/CMakeLists.txt`), as the `UNLEASHED_RECOMP` paths of upstream:

- **Vertices decoded in the shader:** every vertex input is a `uint4` with the raw words of its element, and `vfetchNaruto` (in `shader_common.h`) decodes it by the element's descriptor in the shared constants (c20..c23, one per element, in fetch address order): the Xenos format, signed, integer, the DEC3N rule of `10_11_11` and the source of each component. It covers every format the title uses.
- **Vertex textures:** the descriptor indices of fetch constants 16 to 19 in the shared constants (c18 and c19), and 1D textures bound as 2D textures one texel high.
- **Internal resolution scale and user clip planes:** `ps_param_gen` in guest pixels (c17.zw), and the clip planes as `SV_ClipDistance` (c24..c29).
- **Alpha to coverage** for alpha-tested draws into multisampled targets, by a spec constant.
- The generated HLSL starts with `#define NARUTO_RECOMP`, so the `shader_common.h` pasted into it takes the same paths.

The dependencies (fmt, xxHash, zstd, smol-v, dxc-bin) are the submodules of the upstream checkout, in `thirdparty/` (`XENOS_RECOMP_THIRDPARTY_ROOT`). The shader cache is generated from the local `game/xeshader.sdb` by the build (`tools/native-renderer/shader-cache.cmake`) and stays out of git.
