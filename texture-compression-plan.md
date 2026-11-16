# Texture Compression (KTX2 / Basis Universal) -- Implementation Plan

> Working plan for GPU texture compression in GDTK.
> Status: phases 0-3 delivered (dependency, upload, loader, encoder, import). Phases 4-5 pending.
> Keep this file in sync while the work is in progress; delete it once the feature lands and the
> permanent description lives in `gdtk-overview.md`.

---

## 1. Goal

Give the engine a compressed texture path so that:

1. **Import** encodes every LDR texture it copies into the project to `.ktx2` (Basis Universal).
2. **Pack** encodes any texture that is still in an uncompressed format (hand copied files, older
   projects, files added outside the importer), so a shipped pak never carries a raw PNG atlas.
3. **Runtime** picks the format the GPU actually supports (BC / ASTC / ETC2), transcodes on the
   loading thread and uploads with `glCompressedTexImage2D`. A GPU with none of those formats
   gets an RGBA32 transcode, i.e. today's behaviour.

Why it is worth it, on a 2048x2048 albedo:

| Stage | Today | With KTX2 (ETC1S / UASTC) |
|---|---|---|
| Source file on disk in the project | PNG, 4-10 MB | KTX2, 0.4-1.2 MB (ETC1S) / 2-4 MB (UASTC) |
| Pak entry | PNG + zstd | KTX2 + zstd (already compressed, zstd gains little) |
| VRAM, desktop | RGBA8 = 16 MB | BC7/BC1 = 5.3 / 2.7 MB |
| VRAM, mobile | RGBA8 = 16 MB | ASTC 4x4 = 5.3 MB, ETC2 = 4 MB |

The two encode call sites go through one encoder that lives in the engine
(`ToolKit/Resources/TextureCompression.*`), so neither tool grows a third party dependency of its
own and the pack path can call it directly.

---

## 2. Review of the proposed answer

The proposal (Basis Universal + KTX2, UASTC for normal maps, ETC1S for albedo, format priority
ASTC -> BC7 -> ETC2 -> BC3) is the right direction for this engine. What has to change is the
part that was generic advice and not this codebase's reality:

| Proposed | Verdict here | Why |
|---|---|---|
| "Use the `basisu` CLI, or link the encoder" | **Link it, inside the engine** | Pack-time conversion must produce bytes in memory for `zipWriteInFileInZip`; a CLI would spawn one process per texture, stage temp files and ship another executable. `basis_parallel_compress()` is the batched, threaded, in-memory path. A separate static library would only duplicate the encoder into Import and Packer (4.2). |
| "ETC2 is core in GLES 3.0, extension in WebGL2" | Correct | Confirmed; Android can always fall back to ETC2, WebGL2 cannot. |
| "Mark sRGB/linear correctly at import, otherwise the GPU samples wrong" | **Different answer: the file's sRGB flag is not authoritative** | `Material::MakeSureItsDataTexture` (`Material.cpp:515`) flips normal and metallic-roughness textures from `FormatSRGB8_A8` to `FormatRGBA8` at load. sRGB-ness is decided per *use*, not per file, so the upload format must keep coming from `TextureSettings.InternalFormat`. The KTX2 DFD transfer-function flag is written for correctness but not read back. |
| "Transcoder is small, WASM build exists" | Correct, and it sets the platform split | Transcoder goes into `ToolKit` on every platform including wasm; the encoder is compiled for desktop only, so no web or Android binary carries it (4.2). |
| "Choose priority ASTC -> BC7 -> ETC2 -> BC3" | Adopted, as a backend capability list | The GL backend answers with the formats it can actually upload, in its own order. |
| Not mentioned: KTX2 internal zstd | **Turn it off** | basisu compiles its own zstd into the transcoder. `ToolKit` already links zstd (through minizip). Two zstd copies in one shared library is a duplicate-symbol link failure on MSVC and an interposition hazard on Linux. The pak already applies zstd to every entry, so KTX2-internal zstd is redundant for shipped data. |
| Not mentioned: `glGenerateMipmap` | Mip chain is baked at encode, never generated on GPU | Generating mips of a compressed format is driver-dependent (fine for ETC2/ASTC, shaky for S3TC/BPTC); the encoder already owns a good mip filter and a normal-map renormalisation pass. |
| Not mentioned: format enums | S3TC / BPTC / ASTC enums must be defined by the engine | Neither `Dependency/glad/gl.h` nor emscripten's `GLES3/gl32.h` declares them (only ETC2, which is ES 3.0 core). `TKOpenGL.h` already defines missing EXT enums by hand; the same pattern applies. |

One more trap the proposal could not know about: **the engine decides a texture's sRGB-ness twice**
(`Texture::Texture()` defaults to `FormatSRGB8_A8`, then the material may flip it), so a transcode
that copies the KTX2's flag into the GL format would look right on a first test and be wrong on
every normal map.

---

## 3. Facts the design is built on (verified in code)

1. **Textures are decoded inline, in `Load`, through stb_image.** `Texture::Load`
   (`Texture.cpp:76`) calls `FileManager::GetImageFile`, which reads from disk or from the pak
   and decodes with `ImageLoad`/`ImageLoadFromMemory` (`FileManager.cpp:56`, `Image.h:15`).
   `ImageLoadFromMemory` already exists, so a "read raw bytes, then decide" path needs no new
   decoder.
2. **`Init` uploads whatever CPU data exists.** `Texture::Init` (`Texture.cpp:100`) asserts on
   "no texture data", calls `backend->CreateTexture`, then `GenerateMipmaps` when the settings ask
   for it. `GLBackend::CreateTexture` (`GLBackend.cpp:1039`) has one 2D branch at line 1155 that
   calls `glTexImage2D` with `m_image`/`m_imagef`.
3. **Material textures are referenced by path, from XML.** `.material` files carry
   `<ResourceRef Type="Texture" File="Textures/foo.png">` entries, parsed in
   `Material::DeSerializeImp` via `GetTextureManager()->Create<Texture>(TexturePath(path))`.
   Renaming a texture file rewrites nothing, so a pack-time format change cannot rename entries:
   any `.png` reference in a scene, prefab, material or layer would break.
4. **The pack walks the whole resource tree, not only referenced files.**
   `FileManager::GetAllPaths` (`FileManager.cpp:511`) inserts every file under `Resources/`, then
   `GetAllUsedResourcePaths` adds the manager's storage on top. So a leftover next to an asset is
   packed too, and pack-time conversion sees both referenced and orphan files.
5. **Pak entries are zstd compressed at level -1** (`FileManager::AddFileToZip`, `FileManager.cpp:441`).
6. **Import runs out of process and moves its output by extension.**
   `App::Import` (`App.cpp:974`) spawns `Import.exe`, reads `out.txt` (written at
   `import.cpp:2033` from `g_usedFiles`) and routes each line by extension; images go through
   `SupportedImageFormat(ext)` -> `TexturePath` (`App.cpp:1176`). Adding `.ktx2` to
   `SupportedImageFormat` (`Util.cpp:721`) makes the move step work unchanged.
7. **Import already has a per-slot notion of texture usage.** `ImportMaterial`
   (`import.cpp:625`) calls `textureFindAndCreateFunc` once per assimp slot
   (`aiTextureType_DIFFUSE`, `_NORMALS`, `_METALLNESS`/`_UNKNOWN`, `_EMISSIVE`), which is exactly
   the information the encoder needs (UASTC + linear for normal maps).
8. **`MakeSureItsDataTexture` re-inits textures and decides whether a reload is needed by looking
   for CPU data** (`Material.cpp:551`). Compressed level data has to count as CPU data there, or
   every normal map is decoded twice.
9. **Backends can say what they support.** `IGraphicsBackend` is the single funnel for
   `CreateTexture` (`IGraphicsBackend.h:120`); `RenderSystem::IsNullBackend()`
   (`RenderSystem.h:117`) identifies the headless tools. Extension probing already exists as a
   with-or-without-`GL_` prefix compare over `glGetStringi` (`TKOpenGL.cpp:76`), which matches
   both desktop (`EXT_texture_compression_s3tc`) and browser (`WEBGL_compressed_texture_s3tc`)
   spellings.
10. **Dependencies are prebuilt per config on desktop and compiled from source for Web/Android.**
    `Dependency/CMakeLists.txt` has the two early-return blocks (Android at line 17, Emscripten at
    line 62) that build only what a game needs; desktop deps come from
    `Dependency/Intermediate/<Plat>/<Cfg>` through `Dependency/Config/<dep>-config.cmake` wrappers
    staged by `build_dependencies.py:91`.

---

## 4. Design decisions

### 4.1 One asset format, one loader, extension kept where it already exists

`Texture::Load` sniffs the first 12 bytes (`0xAB 0x4B 0x54 0x58 0x20 0x32 0x30 0xBB 0x0D 0x0A 0x1A 0x0A`):

* KTX2 magic -> parse the container, transcode, store compressed levels.
* anything else -> today's stb path, unchanged.

The importer writes real `.ktx2` files and references `.ktx2` (it authors the material itself, so
this is free). Pack-time conversion does **not** rename entries: it writes the KTX2 bytes under the
same entry name (`Textures/foo.png`), and the loader recognises them by content. This keeps every
existing `.material`/`.scene`/`.prefab`/`.layer` valid without a single XML rewrite, which is the
only option that does not risk corrupting serialized data. The extension in the pak becomes a
label, not a format - documented in `gdtk-overview.md`.

Alternative considered and rejected: rewriting `File="Textures/foo.png"` to `.ktx2` while adding
XML to the zip. It requires enumerating and text-editing every reference type (materials, sprites,
environment components, prefabs, layers, scenes), against files that may be compressed or
hand-edited, for no runtime benefit.

### 4.2 Encoder and transcoder both live in the engine

| Piece | Where | Platforms |
|---|---|---|
| Transcoder (`ktx2_transcoder`, `basist::*`) | `ToolKit` (PRIVATE link) | desktop, Android, Web |
| KTX2 parse + transcode | `ToolKit/Resources/Ktx2Texture.*` | desktop, Android, Web |
| Encoder (`basis_compressor`) | `ToolKit/Resources/TextureCompression.*` | desktop only (source filtered out of Web/Android) |

Upstream ships **one** archive per platform, not two (verified against `v2_50`): `basisu_encoder`
already contains `transcoder/basisu_transcoder.cpp`, so desktop links that single archive and gets
both halves with no duplicate `basist` symbols; Web/Android build a transcoder-only target from the
same source file in `Dependency/CMakeLists.txt`. `ToolKit/CMakeLists.txt` therefore branches on the
platform once:

```cmake
if(EMSCRIPTEN OR ANDROID)
  target_link_libraries(ToolKit PRIVATE basisu_transcoder)   # transcoder-only target
else()
  target_link_libraries(ToolKit PRIVATE basisu_encoder)      # upstream archive, both halves
endif()
```

A separate `Modules/TextureCompression` static library was considered and dropped:

* **It does not keep the encoder out of a shipped game.** On desktop a game links the same prebuilt
  `TK::ToolKit` the editor uses, so the encoder would sit in `ToolKit.dll` either way; on
  Web/Android it is excluded either way.
* **It costs indirection at the one call site that matters.** With the encoder in `ToolKit`,
  `FileManager::AddFileToZip` calls it directly. As a module, `ToolKit` cannot reference it, so the
  pack path needs a `std::function` transform hook installed by the Packer, a bytes cache on the
  tool side and a second link target in two CMakeLists - three moving pieces to say "compress this
  file before zipping it".
* **Tool disk footprint is worse.** Two static copies (`Import.exe`, `Packer.exe`) against one copy
  shared through `ToolKit.dll`.
* **Compile time is a wash.** The basisu encoder translation units compile once whichever target
  owns them; only the final link differs.
* **Precedent.** `FileManager::PackResources` is already an exported engine function that only the
  Packer calls. An exported `CompressImageFile` is the same kind of surface.

The price is desktop binary size. Phase 0 measured the *wiring* cost, which is zero: with no engine
source referencing a basisu symbol yet, the linker pulls nothing out of the 38 MB Debug archive and
`ToolKitd.dll` stays at 10,488,832 bytes (confirmed by a binary search for `basisu` strings). The
real number appears in phase 1 (the transcoder object, roughly 1-2 MB) and phase 2 (the encoder).
If a desktop publish ever finds it unacceptable, moving the two encoder files into a separate
library is mechanical - the API does not move - so the decision is cheap to reverse.

Excluding the encoder from Web/Android follows the `TK_VULKAN` source filter pattern in
`ToolKit/CMakeLists.txt`, plus a stub translation unit so that `FileManager` needs no `#ifdef`:

| File | Compiled when |
|---|---|
| `TextureCompression.cpp` | desktop (links `basisu_encoder`) |
| `TextureCompressionStub.cpp` | Web and Android (logs once, returns false) |

### 4.3 KTX2 without internal zstd

Built with `BASISD_SUPPORT_KTX2_ZSTD=0`; the encoder always writes
`m_ktx2_uastc_supercompression = KTX2_SS_NONE`. Rationale in section 2. Consequence to document:
a KTX2 produced by an external tool with `--zcmp` will not load; ours never are.

If standalone `.ktx2` files later need to shrink, the correct route is to point the transcoder at
the zstd the engine already links (exclude basisu's `Zstd/*.c`, add the existing include dir), not
to link a second copy.

### 4.4 sRGB stays a property of the *use*, not of the file

The transcode target only depends on (a) the block format family and (b) whether the source has
alpha. The GL internal format is chosen at upload from `TextureSettings.InternalFormat`:

| CompressedFormat | sRGB setting | linear setting |
|---|---|---|
| BC1 | `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` | `GL_COMPRESSED_RGB_S3TC_DXT1_EXT` |
| BC3 | `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT` | `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT` |
| BC5 | n/a (data) | `GL_COMPRESSED_RG_RGTC2` (reserved, unused in phase 1) |
| BC7 | `GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM` | `GL_COMPRESSED_RGBA_BPTC_UNORM` |
| ETC2_RGB | `GL_COMPRESSED_SRGB8_ETC2` | `GL_COMPRESSED_RGB8_ETC2` |
| ETC2_RGBA | `GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC` | `GL_COMPRESSED_RGBA8_ETC2_EAC` |
| ASTC_4x4 | `GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR` | `GL_COMPRESSED_RGBA_ASTC_4x4_KHR` |

A normal map therefore uploads its BC7/ASTC blocks through the linear enum after the material has
flipped its settings, exactly as a plain RGBA8 normal map does today. No re-encode, no second
transcode: only the internal format changes on re-`Init`.

### 4.5 Mip chain is baked at encode time

Encode sets `params.m_mip_gen = true` (with `mip_renormalize` for normal maps). Runtime uploads
level by level and skips `backend->GenerateMipmaps` when compressed levels are present. A KTX2
without mips still uploads, but `Texture::Init` then leaves the mipmap filter as set and the
sampler reads incomplete levels - the encoder always writes mips, so this is a warning, not a path.

### 4.6 Fallback: any backend that supports nothing gets RGBA32

`IGraphicsBackend::GetSupportedCompressedFormats()` defaults to an empty list. When the list is
empty the loader transcodes to `cTFRGBA32` and fills `m_image`, i.e. the existing `glTexImage2D`
path. That is what Vulkan (`TK_VULKAN`), the null backend, and any GPU/browser without a
compressed format do. Result: phase 1 needs zero Vulkan changes and cannot regress it.

### 4.7 Format priority

The backend returns its formats in upload preference order; the loader picks the first that
satisfies the alpha requirement.

| Backend | Order |
|---|---|
| Desktop GL (BC available) | BC7, BC3, BC1, ASTC 4x4 |
| Desktop GL (ASTC only, e.g. some Intel/Apple) | ASTC 4x4, BC7, BC3, BC1 |
| Android GLES3 | ASTC 4x4, ETC2_RGBA/ETC2_RGB (core, always present) |
| WebGL2 | ASTC 4x4, BC7, BC3, BC1, ETC2, then RGBA32 |
| Vulkan, NullBackend | (none) -> RGBA32 |

BC7 ahead of BC3/BC1 because it is the quality target both UASTC and ETC1S transcode to cheaply
and it carries alpha; BC1 only when nothing better exists and the source has no alpha.

### 4.8 Out of scope for this plan

* HDR / HDRI (`.hdr`, `float` textures): stays on `GetHdriFile`. UASTC HDR exists and is the
  natural follow-up.
* Cubemaps (`CubeMap::Load` reads six PNGs) and 2D arrays: KTX2 supports both, the loader does not
  yet.
* Compressed render targets, compressed UI atlases, PVRTC (dead, and UASTC cannot produce it).
* Vulkan compressed upload (needs `VkImage` + `vkCmdCopyBufferToImage` with a `VkFormat` map).
* A per-project compression settings UI. Phase 3/4 read defaults from code plus a command line
  switch; `EngineSettings` plumbing is a later, separate change.

---

## 5. Phases

Each phase is usable on its own: 0 unblocks, 1 makes the engine *read* KTX2, 2 produces it,
3 wires import, 4 wires pack, 5 verifies.

| Phase | Content | Size | Depends on |
|---|---|---|---|
| 0 | basisu dependency, CMake, build verification | S | - (delivered) |
| 1 | KTX2 loader + GL compressed upload + capability query | L | 0 (delivered) |
| 2 | Encoder (`TextureCompression.*`) - file, memory, batched | S | 0 (delivered) |
| 3 | Import integration (slot aware) | M | 1, 2 (delivered) |
| 4 | Pack integration (legacy files, no rename) | S | 1, 2 |
| 5 | Verification on Windows / Web / Android, docs | M | 3, 4 |

Size is relative effort, not a schedule.

### Phase 0 -- dependency and build (delivered)

What landed, and where reality differed from the sketch:

1. **Submodule.** `BinomialLLC/basis_universal` is forked into `Oyun-Teknolojileri` and added as
   `Dependency/basis_universal`, pinned to `v2_50` (`9bebe16`, the repo's spelling of v2.5 -- there
   is no `v2.5` tag). Fork master was identical to upstream master at fork time, so the pinned
   commit is present in the fork's object database and `git submodule update` works without
   fetching upstream. License: Apache-2.0. The fork is insurance, not a patch queue: the only basisu
   change this plan needs is a build flag, so no branch diverges from upstream.
2. **One archive, not two.** Upstream `v2_50` has no `basisu_transcoder` target; `basisu_encoder`
   already compiles `transcoder/basisu_transcoder.cpp` into itself. Desktop therefore links that one
   archive (transcoder + encoder, no duplicate `basist` symbols) and Web/Android build a
   transcoder-only target from the same source file. The block lives in `Dependency/CMakeLists.txt`
   with `BASISU_EXAMPLES/OPENCL/ZSTD/SAN/BUILD_PYTHON` off, output flattened into
   `TK_DEPENDECY_OUT_DIR`, and the `basisu` CLI target kept out of ALL with its runtime output
   redirected so a build never writes into the submodule tree.
3. `Dependency/Config/basisu-config.cmake` imports the archive as `basisu_encoder`, probe-per-config
   like `zstd-config.cmake`, and exports the two `BASISD_*` definitions plus the source include
   directories through the target interface.
4. Web and Android early-return blocks each add the transcoder-only `basisu_transcoder` target.
5. `CMakeLists.txt` (root) gains `find_package(basisu REQUIRED CONFIG)`; `ToolKit/CMakeLists.txt`
   branches once on platform (4.2).

Verification actually run:

| Check | Result |
|---|---|
| `build_dependencies.py --configs Debug` | OK, `basisu_encoderd.lib` (38 MB Debug) + wrapper staged, submodule tree clean |
| `build_gdtk.py --configs Debug` | OK, engine + Editor + Import + Packer link |
| `find_package` / includes / interface defines | proven by a throwaway executable: printed `BASISD_SUPPORT_KTX2=1 BASISD_SUPPORT_KTX2_ZSTD=0` and linked `basisu_transcoder_init()` |
| `ToolKitd.dll` size | unchanged at 10,488,832 bytes -- nothing references basisu yet, so the linker pulls no member out of the archive |

Not verified yet: the Web and Android transcoder-only targets (no toolchain run in this phase).

Original checklist, for the record:

1. Fork into `Oyun-Teknolojileri`, submodule at `Dependency/basis_universal`, pinned to `v2_50`.
2. `Dependency/CMakeLists.txt` block: `add_subdirectory`, options, output naming, CLI kept out of ALL.
3. `Dependency/Config/basisu-config.cmake` imported target with per-config locations.
4. Web/Android: transcoder-only target from source.
5. Root: `find_package(basisu REQUIRED CONFIG)`.
6. `ToolKit/CMakeLists.txt`: one platform branch for the link.
7. Size delta recorded (see the table above: zero until code references the archive).

### Phase 1 -- runtime (delivered)

What landed, and what the original sketch got wrong:

1. `Types.h` / `Util.cpp`: `.ktx2` joins the supported image formats, plus `CompressedFormat` and
   `TextureLevel` (a level's width/height are the source dimensions; a block level's buffer is
   `ceil(w/4) * ceil(h/4) * blockBytes`).
2. `IGraphicsBackend::GetSupportedCompressedFormats`, defaulted to an empty list so Vulkan and the
   null backend keep compiling and keep falling back to RGBA8 without a line of their own.
3. `ToolKit/Resources/Ktx2Texture.{h,cpp}`: `IsKtx2Data`, `Ktx2ReadInfo`, `Ktx2Transcode`,
   `Ktx2DecodeRgba8` over `basist::ktx2_transcoder`, with `basisu_transcoder_init()` behind a
   `std::once_flag`.
4. `Texture`: `m_compressedFormat` + `m_compressedLevels`, sniffing `Load` through
   `FileManager::ReadFileBuffer`, compressed-aware `Init`/`UnInit`/`Clear`/VRAM accounting, and a
   `Material::MakeSureItsDataTexture` that treats block data as client side data (otherwise every
   normal map is decoded twice).
5. `GLBackend`: `ProbeCompressedTextureFormats` once at context creation, and a
   `glCompressedTexImage2D` branch that picks the sRGB or linear internal format from
   `TextureSettings`.
6. `TKOpenGL.h`: the S3TC / BPTC / ASTC enums the headers do not carry, plus shared
   `HasGlExtension` / `EnableGlExtension` (three per-platform copies of the probe are gone).

Two things the plan predicted wrong, both found by running it:

* **Buffer sizing for the RGBA8 fallback.** The plan said the transcoder emits padded blocks. True
  for block formats, false for uncompressed ones: `transcode_slice` defaults the row pitch to
  `orig_width` and the row count to `orig_height`, so an uncompressed transcode is written at the
  source size. Sizing that buffer from the padded grid shifts every row and produces an image that
  looks plausible and is wrong. The first harness run reported a mean channel error of 44 against
  basisu's own decode; after the fix it is 0.
* **The GL probe cannot be string-only.** BPTC is core from 4.2 and S3TC with its sRGB enums from
  4.4, and a driver that has them that way does not list the extensions. On the NVIDIA 4.3 driver
  used here, `EXT_texture_compression_s3tc` is listed but neither `EXT_texture_compression_bptc`
  nor `EXT_texture_compression_s3tc_srgb` is, so the first probe reported zero formats on a fully
  capable GPU. The probe now reads `GL_MAJOR_VERSION` / `GL_MINOR_VERSION` first.

Verification actually run (throwaway harness under `tmp/ktx2-check`, not committed):

| Check | Result |
|---|---|
| Parse a 345x345 ETC1S and a UASTC container written by the basisu CLI | 9 levels, correct alpha/sRGB flags |
| BC7 transcode level sizes | exactly `ceil(w/4) * ceil(h/4) * 16` for all 9 levels |
| RGBA8 fallback against basisu's own RGBA32 decode | byte identical (mean abs 6.64 vs the source PNG, same number basisu reports) |
| Real GL 4.3 context, sRGB upload | no GL error, driver reports `compressed=1`, internal format `0x8E8D` |
| `ToolKitd.dll` size | 10,488,832 -> 13,333,504 (+2.8 MB Debug; the transcoder object) |

Still unverified at the end of this phase: the Web and Android transcoder-only targets, and the
`Texture::Load` path driven by a real asset (phase 3 covers both).

<details>
<summary>Original checklist, for the record</summary>

1. `Types.h`: `static const String KTX2(".ktx2")`, `SupportedImageFormat` includes it
   (`Util.cpp:721`); `enum class CompressedFormat : ubyte { None, BC1, BC3, BC5, BC7, ETC2_RGB,
   ETC2_RGBA, ASTC_4x4 };`.
2. `IGraphicsBackend.h`: `virtual const std::vector<CompressedFormat>&
   GetSupportedCompressedFormats() const { static const std::vector<CompressedFormat> none; return none; }`.
   Default keeps Vulkan and NullBackend source-compatible.
3. `ToolKit/Resources/Ktx2Texture.{h,cpp}`: container parse (`basisu::ktx2_transcoder`), info
   query, transcode to a `CompressedFormat`, transcode to RGBA8.
   * `basisu_transcoder_init()` behind a `std::once_flag`.
   * Level buffer size is `ceil(w/4) * ceil(h/4) * blockBytes` - **not** `w * h * bpp`; the
     transcoder emits padded blocks and `glCompressedTexImage2D` infers the size from the
     unpadded dimensions it is given.
   * Report `hasAlpha` from the KTX2 DFD, and honour a caller override (the encoder's
     `-force_alpha` equivalent).
4. `Texture.h/.cpp`: `CompressedFormat m_compressedFormat`, `std::vector<TextureLevel>
   m_compressedLevels`; `Load` sniffs and fills one representation or the other; `Init` counts
   compressed data as data and skips `GenerateMipmaps` when levels exist; `Clear`/`UnInit`
   release them; VRAM accounting uses the summed level sizes instead of
   `BytesOfFormat(InternalFormat)`.
5. `FileManager`: public `bool ReadFileBuffer(const String& path, std::vector<ubyte>& out)` that
   serves both the disk and the pak path (`ReadFileBufferFromZip` already exists privately).
   `Texture::Load` uses it and calls the KTX2 loader or `ImageLoadFromMemory`. This is
   behaviour-preserving: the pak path already decodes with `ImageLoadFromMemory`
   (`FileManager.cpp:774`) and the disk path with `ImageLoad`, which is the same stb decoder and
   the same vertical flip switch, so one extra copy of the bytes is the only cost.
6. `GLBackend`: capability probe once in `InitBackend` (S3TC, S3TC_sRGB, ETC, ASTC, BPTC; on
   Android ETC2 is unconditional; on Web call `emscripten_webgl_enable_extension` for each
   candidate before probing, and log the chosen list once). Cache the ordered vector; the query is
   read from loading threads, so it must never touch GL.
7. `GLBackend::CreateTexture`: compressed branch per level with `glCompressedTexImage2D`, internal
   format from `(format, sRGB)` table in 4.4.
8. `TKOpenGL.h`: define the missing enums (`GL_COMPRESSED_RGB_S3TC_DXT1_EXT`, ...,
   `GL_COMPRESSED_RGBA_ASTC_4x4_KHR`, `GL_COMPRESSED_RGBA_BPTC_UNORM`, sRGB variants) behind
   `#ifndef`, following the EXT-depth-clamp pattern.
9. `Material::MakeSureItsDataTexture` (`Material.cpp:551`): `flush` also considers
   `m_compressedLevels`; the "no image data" warning (`Material.cpp:567`) still fires only when
   there is genuinely nothing.
10. `FolderView` thumbnail extension list (`FolderView.cpp:503`) and any other extension table
    gain `.ktx2`.

</details>

### Phase 2 -- encoder (delivered)

`ToolKit/Resources/TextureCompression.{h,cpp}` plus `TextureCompressionStub.cpp`, filtered by
platform in `ToolKit/CMakeLists.txt` (the desktop build links `basisu_encoder`, which already
contains the transcoder). The API is `TK_API`, like `FileManager::PackResources`, because Import and
Packer call it across the DLL boundary.

```cpp
struct TextureCompressionOptions
{
  enum class Codec { ETC1S, UASTC };
  Codec Format         = Codec::ETC1S;   // named Format, see below
  int Quality          = 75;            // [1,100]
  int Effort           = 3;             // [0,10]
  bool Srgb            = true;
  bool NormalMap       = false;         // linear metrics, flat weights, mip renormalisation
  bool GenerateMipmaps = true;
};

bool CompressImageToKtx2(const ubyte* rgba8, int width, int height, const Options&, std::vector<ubyte>&);
bool CompressFileToMemory(const String& sourceImage, const Options&, std::vector<ubyte>&);
bool CompressFileToKtx2(const String& sourceImage, const String& destKtx2, const Options&);
bool CompressFiles(const std::vector<TextureCompressionJob>& jobs, int threadCount);
```

What the sketch got wrong or did not say:

* **`Codec Codec` does not compile.** A member with the same name as its type hides the type name
  inside the struct, so `TextureCompressionOptions::Codec::UASTC` stops resolving. The member is
  called `Format`.
* **No `basis_parallel_compress`.** It is for many small textures; here one texture is enough work
  for its own thread and the batch is parallelised *across* textures with `std::thread`. This also
  keeps the output deterministic: one image is always encoded single threaded (`m_multithreading`
  off, a 1 thread `job_pool`), which matters because Import and the Packer encode the same source
  with the same options and must agree byte for byte.
* **`get_output_ktx2_file()`** with `m_create_ktx2_file = true` and
  `m_write_output_basis_or_ktx2_files = false` is the in-memory path; the pack step needs bytes, not
  a temp file.
* **The row order is the encoder's job.** `glTexImage2D` reads the first row as the bottom row, and
  the engine has always satisfied that by flipping in stb (`ImageSetVerticalOnLoad`). Import and
  Packer run headless where that switch is off, so the encoder flips explicitly
  (`ImageLoadTopDown` + a manual row swap) instead of depending on host state.
* `basisu_comp.h` is the header (`basisu_enc.h` alone has neither `basis_compressor_params` nor
  `BASISU_MAX_IMAGE_DIMENSION`), and `encoder/` has to be on the imported target's include path.

Verification actually run (same throwaway harness):

| Check | Result |
|---|---|
| ETC1S encode of the test PNG | 47,193 bytes, quality 75 / effort 2 |
| Two encodes of the same input | byte identical (deterministic) |
| UASTC normal-map encode | 161,568 bytes, same size the basisu CLI produces for the same options |
| Read back through the phase 1 loader | 345x345, 9 levels, sRGB flag as configured |
| Decoded vs bottom-up reference (GL upload order) | mean abs 5.24, the codec's own error |
| Decoded vs file-order reference | mean abs 48.00 |

That last row is the point of the flip test: the container provably holds the rows in the order the
GL upload needs, not the order the file has.

Not verified in this phase: the stub build itself (it only compiles for Web/Android; the CMake
filter was confirmed by inspecting the generated project, and the stub is trivial).

<details>
<summary>Original checklist, for the record</summary>

* `basisu_encoder_init()` once; `basis_parallel_compress` for the batch entry point.
* `params.set_format_mode_and_quality_effort(mode, quality, effort)`,
  `params.set_srgb_options(srgb)`, `m_mip_gen`, `m_ktx2_uastc_supercompression = KTX2_SS_NONE`,
  `m_tex_type = cBASISTexType2D`, `m_check_for_alpha` (alpha detected -> forced), `m_pJob_pool`
  mandatory even single threaded.
* Source decoding is `stb_image`-based (`ImageLoad*` from the engine), not basisu's own wuffs
  readers, so exactly one PNG/JPG decoder decides pixel values.
* The stub implements the same entry points, logs once and returns false, so nothing above it needs
  a platform check.
* The `-normal_map` equivalent: `set_srgb_options(false)`, `set_ldr_srgb_channel_weights(false)`,
  `m_mip_renormalize = true`.
* Reject > `BASISU_MAX_IMAGE_DIMENSION` (16384) and 0-sized input with a `TK_ERR`.

</details>

### Phase 3 -- import (delivered)

`textureFindAndCreateFunc` in `import.cpp` now encodes instead of copying: the destination name
becomes `<name>.ktx2`, `CompressionOptionsForSlot(aiTextureType)` picks the codec, and the material
references the container. What changed against the sketch:

* **Codec per slot** as planned, with one addition: `aiTextureType_AMBIENT_OCCLUSION` and
  `_DIFFUSE_ROUGHNESS` also take the UASTC/linear branch, being data maps like metallic-roughness.
* **Alpha needs no scan.** The encoder is asked to check for alpha (`m_check_for_alpha`), so the
  plan's separate alpha pass would have duplicated work basisu already does.
* **DDS** keeps the PNG detour (`ConvertDdsToPng`, BC1/BC3/BC5) and the intermediate is deleted once
  the container exists. An undecodable DDS is still reported once and skipped.
* **Embedded textures** keep their intermediate file for the duration of the encode and it is
  removed afterwards, so the pak does not carry a second copy of every glTF texture.
* **Legacy source fallback** landed as planned: a re-import of a project that predates KTX2 encodes
  from the old `.png` already sitting in the texture folder.
* **Failure policy** landed as planned: copy the source, warn, reference the plain file.
* **`out.txt`** carries the `.ktx2` path, so `App::Import`'s move step routes it through
  `SupportedImageFormat` into `Resources/Textures` without a change there.
* **CLI**: `-tex etc1s|uastc|none`, `-tq 1..100`, defaulting to by-slot at quality 75.
* **Not batched.** Textures are encoded one at a time inside the material loop rather than collected
  and pushed through `CompressFiles`. It is simpler and cannot reorder work around the material that
  references it; the cost is that a large model imports slower than it could. The batch entry point
  is there for the pack step (phase 4), where nothing depends on order.

Verification actually run:

| Check | Result |
|---|---|
| Import a textured OBJ (345x345 PNG, diffuse slot) | exit 0 |
| Output tree | `Temp/Textures/ktx2test/tex.ktx2` (47,600 bytes), material, mesh, prefab; no leftover PNG |
| Material reference | `<ResourceRef Class="Texture" File="ktx2test/tex.ktx2"/>` |
| `out.txt` | lists `Temp/Textures/ktx2test/tex.ktx2` |
| Container read back | 345x345, 9 levels, sRGB |
| `Texture::Load` on the imported file | loaded, 345x345, RGBA8 fallback (no backend in the harness), image matches the bottom-up reference at 5.24 |

Two notes for whoever repeats this:

* A `Texture` constructed **on the stack** crashes the debug build in its destructor (the engine
  tracks every `Object` in a registry); a harness has to use `MakeNewPtr<Texture>()`. The engine
  itself never constructs one any other way.
* The imported container is *not* flipped relative to the source file, which is correct: it holds
  the bottom-up rows the GL upload wants, so comparing it against a top-down decode of the source
  reads as ~48 mean channel error and is not a failure.

<details>
<summary>Original checklist, for the record</summary>

1. `import.cpp`: `textureFindAndCreateFunc` (`import.cpp:629`) stops copying the source when the
   extension is an LDR image; it encodes to `<texDir>/<name>.ktx2` and references that.
   `HDR` keeps the old copy path.
2. Codec per slot: normal -> UASTC + normal map flags; metallic-roughness -> UASTC, linear;
   diffuse/emissive/base color -> ETC1S (quality 75-80, effort 2-3). A shared helper builds the
   options from the `aiTextureType` the lambda already receives.
3. Alpha: `stb_image` gives 4 channels always today (`reqComp = 4`), so determine "really has
   alpha" by scanning the alpha channel (a cheap pass) before choosing the codec, otherwise every
   opaque texture is encoded with an alpha channel.
4. DDS: keep `ConvertDdsToPng` as the first step (it already handles BC1/BC3/BC5) and feed the
   resulting PNG to the encoder, then delete the intermediate PNG. One less format to re-verify.
5. Embedded textures (`ImportTextures`, `import.cpp:1825`) keep writing their intermediate file -
   `ImportMaterial` runs after it and finds it - and that intermediate is deleted after encoding.
6. Legacy source fallback: when `<texDir>/<name>.ktx2` does not exist but the *old* destination
   (`<texDir>/<name>.png`, a previous import) does, encode from that file instead of looking in
   the FBX folder. Without this, re-importing a model whose textures already live in the project
   loses them: the current code skips the copy because the destination exists, and that check now
   looks at a `.ktx2` that was never written.
7. Failure policy: encoding fails -> copy the source as today, `TK_WRN` once per file, and the
   material references the uncompressed file. Import never fails because of compression.
8. `out.txt` gets the `.ktx2` path (so `App::Import`'s move step and its missing-file search keep
   working through `SupportedImageFormat`).
9. CLI: `-tex <etc1s|uastc|none>` and `-tq <1..100>` for overrides; `-tex none` restores today's
   behaviour. Editor passes nothing (defaults apply).
10. Batch: collect all textures of one imported file, encode with `CompressFiles` after
   `ImportMaterial` has enumerated them - or encode lazily inside the lambda and flush a batch at
   the end of each material. Lazy with a per-file cache keyed by source path is simpler and keeps
   duplicates from being encoded twice.

Acceptance (met by the verification table above): import an FBX with diffuse + normal +
metallic-roughness; `Resources/Textures` contains only `.ktx2`; the material opens and renders.

</details>

### Phase 4 -- pack

1. `FileManager::AddFileToZip` (`FileManager.cpp:441`) asks
   `TextureCompression::CompressImageFile` first; bytes coming back are written under the same
   entry name, no bytes means the file is zipped exactly as today. The options arrive through a
   plain `FileManager::SetPackCompressionOptions(...)` setter that the Packer fills in - a setter,
   not a callback, because the engine owns the encoder now.
2. `Packer` decides what to compress when packing: image extension, not `.ktx2`, not `.hdr`, size >=
   64x64, inside the project `Resources` folder (not `DefaultPath`), -> encode with the job's
   options. Encode in parallel before zipping starts, keeping a small path -> bytes cache filled by
   `CompressFiles` on first miss.
3. `--no-compress` flag for `Packer.exe`, and a summary line: `N textures encoded, M skipped,
   X MB -> Y MB`.
4. Import-independent: this also compresses hand-copied PNGs and projects imported before this
   feature existed.

Acceptance: pack a project whose Textures folder is plain PNG; the pak runs, and the log reports
the compression ratio. A pak packed with `--no-compress` still runs (no regression).

### Phase 5 -- verification and docs

1. Desktop GL: BC7 path, plus a forced fallback run (`--no-...`/env switch or a build without the
   extension) to exercise RGBA32.
2. Web: `emcmake` build through `Packer::WebPublish`; Chrome headless with SwiftShader logs the
   chosen format. SwiftShader may expose no compressed format - which is the fallback test, and
   the reason the fallback must be part of the acceptance, not an afterthought.
3. Android: emulator or device, ETC2/ASTC path, and the pak copy flow (`MinResources.pak.hash`).
4. Visual regression: a checker pattern with an "up" arrow, a normal map, and an alpha cutout, on
   all three platforms. The arrow catches the vertical flip convention.
5. Docs: `gdtk-overview.md` sections 5.2 (GL backend, capability query, enum definitions), 6.3
   (Texture / KTX2 levels), 11.1 (import encodes), 11.2 (pack converts), 12.1 (basisu dependency),
   14 (file lookup). Update both config wrappers notes in `Dependency/CMakeLists.txt` comments.
6. Delete this file once the feature is described in the overview.

---

## 6. File map

| File | Change |
|---|---|
| `Dependency/basis_universal/` | done: submodule, `Oyun-Teknolojileri` fork pinned to `v2_50` |
| `.gitmodules` | done: new submodule entry |
| `Dependency/CMakeLists.txt` | done: basisu block (desktop archive + Web/Android transcoder) |
| `Dependency/Config/basisu-config.cmake` | done: imported-target wrapper |
| `CMakeLists.txt` (root) | done: `find_package(basisu)` |
| `ToolKit/CMakeLists.txt` | done: one platform branch for the link (4.2) |
| `ToolKit/Resources/TextureCompression.{h,cpp}` | new: encoder |
| `ToolKit/Resources/TextureCompressionStub.cpp` | new: Web/Android no-op |
| `ToolKit/Resources/Ktx2Texture.{h,cpp}` | new: container + transcode |
| `ToolKit/Resources/Texture.{h,cpp}` | compressed levels, sniff in `Load`, upload in `Init`, stats |
| `ToolKit/Resources/Material.cpp` | `MakeSureItsDataTexture` data check |
| `ToolKit/Source/FileManager.{h,cpp}` | `ReadFileBuffer`, `SetPackCompressionOptions`, pack encode |
| `ToolKit/Source/Types.h`, `Util.cpp` | `.ktx2`, `CompressedFormat` |
| `ToolKit/Render/IGraphicsBackend.h` | capability query (defaulted) |
| `ToolKit/Render/OpenGL/GLBackend.{h,cpp}` | probe, `CreateTexture` branch |
| `ToolKit/Render/OpenGL/TKOpenGL.h` | compressed format enums |
| `ToolKit/Render/NullBackend.h` | inherits the default (no change expected) |
| `Utils/Import/import.cpp`, `CMakeLists.txt` | encode on copy, slot aware options (no link change) |
| `Utils/Packer/Packer.cpp`, `CMakeLists.txt` | compression options + `--no-compress` (no link change) |
| `Editor/Source/App.cpp` | nothing expected (extension routing already generic) |
| `Editor/UI/Window/FolderView.cpp` | `.ktx2` in the thumbnail extension list |
| `gdtk-overview.md` | sections 5.2 / 6.3 / 11.1 / 11.2 / 12.1 / 14 |

---

## 7. Risks and traps

1. **Vertical flip.** `stb_image` flips vertically based on a global switch set by the renderer
   (`ImageSetVerticalOnLoad`). Import runs headless and may not have that switch set. If the
   importer encodes unflipped pixels while the runtime loads flipped ones, every texture is upside
   down. Fix the convention explicitly at the start of phase 2 (encode the same orientation the
   runtime uploads) and test with an asymmetric image.
2. **Duplicate zstd.** Covered by 4.3; if it is ever enabled, expect `LNK2005` on MSVC and
   symbol interposition on Linux.
3. **Non-multiple-of-4 dimensions.** Block formats pad; the KTX2 keeps the true dimensions but the
   level data covers `ceil(w/4)*ceil(h/4)` blocks. Sizing the buffer as `w*h*bpp` corrupts the
   upload.
4. **`glGenerateMipmap` on compressed textures** is driver-dependent; the engine must not call it
   when levels were uploaded.
5. **WebGL needs the extension enabled**, not just listed: `emscripten_webgl_enable_extension`
   before probing, otherwise `glCompressedTexImage2D` fails with `INVALID_ENUM` and the texture
   reads black while every call "succeeds".
6. **Null backend / packer decodes everything** to fill the resource managers
   (`LoadAllScenes` -> `Texture::Load`). With KTX2 that becomes a transcode to RGBA32 for every
   texture, which is wasted work but no worse than today's PNG decode. A "skip pixel load when
   `RenderSystem::IsNullBackend()`" optimisation is possible later but needs its own pass over
   `m_image` consumers (starting with `MakeSureItsDataTexture`'s warning).
7. **Encode time.** First import of a large project becomes minutes (ETC1S effort 3, UASTC level
   2). Mitigated by batched parallel encoding and by `-tq`/`-te` defaults; surface a progress
   line per texture so it does not look hung.
8. **Pak hash / Android copy.** Converting textures changes the pak bytes on every pack; the
   `MinResources.pak.hash` flow already handles "changed pak" and is not affected.
9. **Editor icons and engine defaults stay PNG.** They are tiny, referenced by name from
   `TexturePath(..., true)`, and live under `DefaultPath`; pack-time conversion must skip that root.
10. **Alpha cutouts.** ETC1S/BC1 drop alpha unless alpha is forced; a foliage material would turn
    into an opaque quad.
11. **Desktop binary size.** The encoder adds ~2-4 MB to `ToolKit.dll` that every desktop game
    ships (4.2). Phase 0 records the real number; if it ever matters, moving the two encoder files
    into their own library is a mechanical change with no API move.

---

## 8. Open questions

1. ~~Submodule source~~ Decided: a `Oyun-Teknolojileri` fork of `BinomialLLC/basis_universal`,
   submodule at `Dependency/basis_universal`, pinned to `v2_50`. The fork exists so the dependency
   survives the upstream disappearing, not to carry patches (phase 0, item 1).
2. Should `Import` default to ETC1S for colour (small, some quality loss) or UASTC everywhere
   (safe, 3-4x bigger)? This plan defaults to ETC1S for colour, UASTC for data, with `-tex uastc`
   as the escape hatch.
3. Do we want a texture-compression block in `EngineSettings` (project level) in the same change,
   or as a follow-up? This plan keeps it a follow-up so phase 3/4 do not grow.
