# 视频解码与编码技术栈调研（Video pipeline stack research）

> 调研对象：Magpie Experimental fork（`smter/Magpie`，分支 `experimental`，C++/WinRT + WinUI 3，GPLv3）。
> 背景目标：规划一个新的独立离线视频处理应用 —— 用户选择一个视频文件 → 应用复用 Magpie 的 DLSSNR 后端
> （`src/Magpie.Core/DLSSNRFilter.{h,cpp}`，D3D11 纹理进/出、同分辨率 SDR、仅 NVIDIA RTX）做降噪 →
> 输出新的视频文件。仓库当前没有任何 FFmpeg / Media Foundation 依赖。
>
> 本文件为本 effort 约定的调研存放位置 `docs/research/video-pipeline-stack.md`。每条论断注明来源
> （仓库文件路径 + 行号，或一手 URL）。一手资料优先（官方文档 / 源码 / 规范），不用二手转述。

---

## 目录

1. [调研方法与来源约定](#1-调研方法与来源约定)
2. [代码侧事实：DLSSNRFilter 接入契约（本仓库一手证据）](#2-代码侧事实dlssnrfilter-接入契约本仓库一手证据)
3. [(a) 解码：FFmpeg(libav) vs Windows Media Foundation + D3D11VA](#a-解码ffmpeglibav-vs-windows-media-foundation--d3d11va)
4. [(b) 编码：FFmpeg + NVENC 关键参数与质量优先默认值](#b-编码ffmpeg--nvenc-关键参数与质量优先默认值)
5. [(c) 输入覆盖：容器 / 编解码 / 音频直通](#c-输入覆盖容器--编解码--音频直通)
6. [(d) 输出默认值建议](#d-输出默认值建议)
7. [结论对推荐的影响](#7-结论对推荐的影响)

---

## 1. 调研方法与来源约定

- 代码侧用 `read` / `grep` 直接读本仓库源码；外部事实只采信一手来源：
  - FFmpeg 官方文档：`https://ffmpeg.org/ffmpeg.html`、`https://ffmpeg.org/ffmpeg-codecs.html`、`https://ffmpeg.org/ffmpeg-formats.html`、`https://ffmpeg.org/general.html`。
  - FFmpeg 源码 / doxygen：`libavutil/hwcontext_d3d11va.h`、`libavutil/hwcontext.h`（`av_hwframe_transfer_data`）、`libavcodec/nvenc.c`（doxygen 镜像 `https://ffmpeg.org/doxygen/trunk/`，源码镜像 `https://sources.debian.org/src/ffmpeg/`）。
  - NVIDIA 开发者文档：`https://docs.nvidia.com/video-technologies/video-codec-sdk/`（NVENC 应用说明、Using FFmpeg with NVIDIA GPU）、`https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new`。
  - Microsoft Learn：`https://learn.microsoft.com/`（D3D11 视频解码、`D3D11_CREATE_DEVICE_VIDEO_SUPPORT`、Media Foundation、IMFDXGIBuffer）。
- 每条外部论断标注 URL；代码论断标注 `文件路径:行号`。

---

## 2. 代码侧事实：DLSSNRFilter 接入契约（本仓库一手证据）

### 2.1 DLSSNRFilter 输入 / 输出形态（`src/Magpie.Core/DLSSNRFilter.cpp`）

- **同分辨率约束（硬性）**：`Initialize` 首先检查 `inputDesc.Width/Height == outputDesc.Width/Height`，不满足即报错
  `"DLSSNR requires same-resolution input/output"` 并返回失败。`src/Magpie.Core/DLSSNRFilter.cpp:1553-1562`。
- **格式约束（SDR、8bit）**：输入只接受 `DXGI_FORMAT_R8G8B8A8_UNORM`（RGBA8）或 `DXGI_FORMAT_B8G8R8A8_UNORM`（BGRA8），
  输出必须为 `R8G8B8A8_UNORM`；否则报 `"DLSSNR SDR path unsupported formats"` 失败。`DLSSNRFilter.cpp:1563-1570`。
- **BGRA→RGBA 转换已内置**：输入为 BGRA 时置 `convertInputToRgba=true`（`DLSSNRFilter.cpp:1584`），
  用 compute shader `ConvertToRgba`（`COLOR_CONVERT_HLSL`，`DLSSNRFilter.cpp:87-98`）把 BGRA 拷到内部
  RGBA shared 纹理（建 shader 逻辑 `DLSSNRFilter.cpp:1624-1646`，逐帧执行 `PrepareInput` `DLSSNRFilter.cpp:1286-1297`）。
  即：**解码侧若能直接产出 BGRA8 纹理，无需自行写颜色转换**；若解码输出是 NV12，仍需 YUV→RGB 这一步（见 §3.2）。
- **D3D11 设备来自 `DeviceResources`**：`impl->device11 = resources.GetD3DDevice()`（ID3D11Device5）、
  `impl->context11 = resources.GetD3DDC()`（ID3D11DeviceContext4），`DLSSNRFilter.cpp:1549-1550`；
  内部还经 `NgxD3D12Core` 拿 D3D12 设备（`DLSSNRFilter.cpp:1586-1590`）。
- **内部桥接已用共享句柄**：`CreateSharedTexture` 用 `D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE`
  创建 RGBA8 纹理，再 `CreateSharedHandle` + `device12->OpenSharedHandle` 打开为 D3D12 资源，
  `DLSSNRFilter.cpp:879-919`。→ 仓库已示范「D3D11 纹理 ↔ DXGI 共享句柄 ↔ D3D12」的桥接模式。
- **输入纹理必须与 filter 同设备**：逐帧 `PrepareInput` 用 `CopyResource(sharedInput11, input)`
  （RGBA 路径，`DLSSNRFilter.cpp:1283`；fallback `DLSSNRFilter.cpp:1889`）。`CopyResource` 要求两资源同属一个
  D3D11 设备。→ **解码纹理必须创建在 `DeviceResources` 的同一 D3D11 设备上**，否则要先经 DXGI 共享句柄
  `OpenSharedResource1` 到该设备（见 §3.2 坑 3）。

### 2.2 Draw 调用契约（`src/Magpie.Core/NativeEffectBackend.h`）

```cpp
struct NativeEffectDrawContext {
    ID3D11Texture2D* input;  // RGBA8/BGRA8，同分辨率
    ID3D11Texture2D* output; // RGBA8
    FrameGuidanceFrameId frameId;                 // uint64
    const FrameGuidanceView& frameGuidance;       // 可退化到 zero
    const FrameGuidanceView& zeroFrameGuidance;
};
```
`NativeEffectBackend.h:8-14`；`Draw(const NativeEffectDrawContext&)` `NativeEffectBackend.h:34`。

- Magpie 调用点：`Renderer.cpp:1649-1666`（`input = effectDrawer.GetTexture(0)`、`output = GetOutputTexture()`、
  `frameId = _capturedFrameId`、guidance 来自 `FrameGuidanceService::GetConsumerViews`）。
- 效果纹理为 `DXGI_FORMAT_R8G8B8A8_UNORM`、建在 backend 的 `DeviceResources` 设备上（`Renderer.cpp:1321-1328`）。

### 2.3 Frame Guidance：离线可用 zero 引导（重要）

- `DLSSNRFilter::GetFrameGuidanceRequirements()` 返回 `{ .zero = true }`（`DLSSNRFilter.cpp:1511-1533`），
  类头注释明确「valid zero-filled motion/depth textures are used as explicit temporal guides」
  （`DLSSNRFilter.h:26-28`）。
- `Draw` 里 `SelectGuidance` 会把无效/不可用的 guidance 回退到 `zeroFrameGuidance`（`DLSSNRFilter.cpp:1813-1842`）。
- zero view 的构造方式在 `FrameGuidanceService.cpp`：motion=R16G16_FLOAT、depth=R32_FLOAT、confidence=R8_UNORM
  三张全零纹理，metadata `valid=true, isZero=true`，且 `frameId/sourceExtent/validRegion` 必须与输入一致
  （`FrameGuidanceService.cpp:112-121` metadata 构造、`180-212` 纹理创建与清零、`320-343` zero view 组装、
  `612-619` `_zeroView` 组装；有效性校验 `FrameGuidanceTypes.h:105-114,133-142`）。
- **离线必须注意**：DLSSNR 是时域滤波，内部有自维护的历史（`resetHistory` 首帧 true、之后 false，
  `DLSSNRFilter.cpp:1951,2054`）。零引导（motion=0）等价于把画面当作静止，运动物体可能出现拖影/去噪不足；
  仓库内其实有 DepthAnythingV2 + 光流 motion provider（`DepthAnythingV2Provider.cpp` 等），离线应用若要更高质量
  可考虑复用，但这超出本次解码/编码栈范围，仅作风险提示。

### 2.4 其它接入要点

- **frameId 必须单调且唯一**：`Draw` 对重复 `frameId` 直接跳过计算并复用上一次结果
  （`DLSSNRFilter.cpp:1851-1860`）。离线按 0,1,2,… 递增即可。
- **线程/栅栏**：filter 内部 D3D11↔D3D12 用共享 fence 同步；逐帧需要 `Drain()` 收尾
  （`DLSSNRFilter.cpp:1809-1811`）。
- **DLSSNR 降级 = 直通**：初始化失败或运行失败后 `disabled=true`，`Draw` 退化为
  `CopyResource(output, sharedInput11)` 直通（`DLSSNRFilter.cpp:1883-1891,2039-2043`）——对离线批处理意味着
  「尽力而为」，输出文件在无 RTX 时会是未降噪原画。
- **signed snippet**：`InitializeSignedSnippet` 从 exe 同目录加载 `nvngx_dlssnr.dll` 并以特定
  `DLSSNR_SIGNED_SNIPPET_APPLICATION_ID` 初始化（`DLSSNRFilter.cpp:1146-1195`）。→ 离线应用需随包分发
  `nvngx_dlssnr.dll`，并按 NGX/GPL 注意事项处理组件分发。
- **参数映射**：`DLSSNRSettings` 字段与 UI 参数的映射在 `NativeEffectBackendFactory.cpp:65-108`
  （`enableInputResolutionScaling / inputResolutionPercent / residualMultiplier / nrPreset / style / intensity /
   localToneStrength / localStructureStrength / skinStructureStrength / useAutoMask / uiCorrection /
   guidanceMode / depthInferenceInterval`）。
  `guidanceMode` 默认 0（需要 motion+depth，可全零）；设为 1 则只颜色降噪。

### 2.5 D3D11 设备创建事实（决定解码纹理如何接入）

`src/Magpie.Core/DeviceResources.cpp`：

- 设备用 `D3D11CreateDevice` 创建，feature level 11_1/11_0（`DeviceResources.cpp:163-201`）。
- **创建标志**：`D3D11_CREATE_DEVICE_BGRA_SUPPORT`（`DeviceResources.cpp:169`），
  DEBUG/SINGLETHREADED/PREVENT_INTERNAL_THREADING_OPTIMIZATIONS 按需（`DeviceResources.cpp:171-185`）。
- **没有 `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`** → Magpie 现有设备**不能**直接做 D3D11 硬件视频解码
  （`ID3D11VideoDevice/ID3D11VideoDecoder` 需要该标志，见 §3.2 坑 1 的 Microsoft Learn 证据）。
  → 离线应用要么给自建设备加上该标志，要么用一个独立的解码 D3D11 设备 + DXGI 共享句柄把解码纹理送到
  `DeviceResources` 设备。
- 对外接口：`ID3D11Device5* GetD3DDevice()`、`ID3D11DeviceContext4* GetD3DDC()`、`IDXGIFactory7*`、
  `IDXGIAdapter4*`（`DeviceResources.h:15-18`）。

### 2.6 依赖确认

- 全仓库 grep（不区分大小写）`ffmpeg|libav|avcodec|avformat|swscale|avutil|MediaFoundation|MFStartup|
  IMFSourceReader|IMFTransform|MFCreate` 与 `nvenc|nvcodec|dxva|d3d11va|ID3D11VideoDevice|ID3D11VideoDecoder`
  → **零匹配**。构建用 MSVC `.vcxproj` + Conan（`src/_ConanDeps`），无视频编解码依赖。→
  「仓库当前没有任何 FFmpeg / Media Foundation 依赖」属实。

---

## (a) 解码：FFmpeg(libav) vs Windows Media Foundation + D3D11VA

### a.1 FFmpeg D3D11VA 硬件解码 → D3D11 纹理直喂 DLSSNRFilter：可行

1. **解码帧即 D3D11 纹理**：FFmpeg 的 `d3d11va` hwaccel（`AV_HWDEVICE_TYPE_D3D11VA`，覆盖
   H.264/HEVC/AV1/MPEG-2/VC-1 的 `*_d3d11va` 解码器）解码输出为 `AV_PIX_FMT_D3D11VA_VLD` hwframe，
   其底层对象就是 `ID3D11Texture2D*`（`AVFrame->data[0]` 直接 reinterpret 即可）；
   多平面格式（如 NV12）用同一纹理的不同 array slice 存放各平面。
   一手来源：libavutil/hwcontext_d3d11va.h 的 doxygen
   （https://ffmpeg.org/doxygen/trunk/hwcontext__d3d11va_8h.html ，镜像
   https://patches.ffmpeg.org/doxygen/trunk/hwcontext__d3d11va_8h.html ）；
   d3d11va hwaccel 源码 https://sources.debian.org/src/ffmpeg/7:3.2.5-1%7Ebpo8+1/libavcodec/d3d11va.c/ 。
   权威佐证：NVIDIA 官方「Using FFmpeg with NVIDIA GPU」文档明确给出 d3d11va 解码 + nvenc 编码的
   GPU 直通用法 https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/ffmpeg-with-nvidia-gpu/index.html
   （13.1 索引页 https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/#ffmpeg-with-nvidia-gpu）。

2. **可以把现有 D3D11 设备注入 FFmpeg**：`AVD3D11VADeviceContext` 暴露公开字段 `device`（ID3D11Device）
   与 `device_context`（ID3D11DeviceContext）；标准做法是 `av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA)`
   → 填这两个字段指向 `DeviceResources` 的现有设备 → `av_hwdevice_ctx_init()`。
   一手来源：hwcontext_d3d11va.h doxygen（同上 URL）——d3d11va 是少数支持「用户提供既有设备」的 hwcontext，
   该能力是社区大量 D3D11 集成（OBS/播放器）的基础。
   → **只要离线应用给设备加上 `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`（§3.2 坑 1），解码纹理与
   DLSSNRFilter 就天然同设备，`CopyResource` 直接可用**。

3. **`av_hwframe_transfer_data` 语义**：把 hwframe（GPU 纹理）拷贝到系统内存 `AVFrame`（软件格式，
   如 NV12），是「需要 CPU 访问」时的下采样路径；doxygen 原文为 “Copy data to or from a hw surface”，
   目标帧未分配 buffer 时函数会自行分配（要求目标格式/尺寸与 hwframe 的软件格式一致）。
   一手来源：libavutil/hwcontext.h doxygen
   https://ffmpeg.org/doxygen/trunk/group__lavu__hwcontext.html
   （函数文档亦可见于 https://docs.rs/rusty_ffmpeg/0.6.0/.../fn.av_hwframe_transfer_data.html 的 1:1 绑定注释，
   以及 FFmpeg 源码 `libavutil/hwcontext.c`）。
   → 对本应用，**不需要**走这条 CPU 路径：GPU 纹理直接喂 DLSSNRFilter，输出纹理直接喂 NVENC，全程零 CPU 拷贝。

4. **零拷贝 CLI 直通路径（可行性样板）**：`ffmpeg -hwaccel d3d11va -hwaccel_output_format d3d11 -i in.mp4
   -c:v h264_nvenc out.mp4` 解码帧保持 D3D11 纹理、nvenc 直接消费（nvenc 编码器接受 `AV_PIX_FMT_D3D11`
   hwframe，编码器 hwdevice 用同一个 d3d11va 设备）。
   一手来源：FFmpeg 官方 wiki HWAccelIntro
   https://trac.ffmpeg.org/wiki/HWAccelIntro（`-hwaccel_output_format d3d11` 一节），
   以及 NVIDIA「Using FFmpeg with NVIDIA GPU」文档（同上 URL，给出 d3d11va→nvenc 的组合示例与 AQ 建议）。

### a.2 已知坑（本应用视角）

1. **`D3D11_CREATE_DEVICE_VIDEO_SUPPORT` 标志是硬前提**：Microsoft Learn 对 `D3D11_CREATE_DEVICE` 标志的
   定义明确该标志用于启用视频解码/处理 API（`ID3D11VideoDevice`/`ID3D11VideoDecoder` 等）；未设置时创建
   视频解码器会失败。一手来源：
   https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_create_device_flag ，
   相关接口 https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nn-d3d11-id3d11videodevice 。
   → 与 §2.5 直接呼应：离线应用需要自建带 `D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT`
   的 D3D11 设备（不能照搬 Magpie 当前 `DeviceResources` 的创建参数）。
2. **NV12→RGB 转换不可避免**：解码输出是 NV12（d3d11va 的软件格式），而 DLSSNRFilter 只收 RGBA8/BGRA8
   （§2.1）。三条可选路径：(i) 用 D3D11 Video Processor（`ID3D11VideoProcessor`，需 VIDEO_SUPPORT 标志）在 GPU
   上 NV12→BGRA；(ii) 自写 compute shader（仓库已有 `ConvertToRgba` 可改写成 NV12 采样版本）；(iii)
   `av_hwframe_transfer_data` 下载到 CPU + `swscale` 转 BGRA 再上传（慢，离线可接受但没必要）。
   FFmpeg 本身没有 D3D11 的 GPU 颜色转换滤镜（scale 系滤镜只覆盖 cuda/vaapi/qsv/vulkan/opencl），
   所以 GPU 侧转换要自己做——这是本管线唯一需要新写的 GPU 环节。
3. **设备亲和性 / 共享**：`CopyResource` 仅限同设备；跨设备必须走 `D3D11_RESOURCE_MISC_SHARED_NTHANDLE` +
   `OpenSharedResource1`（仓库在 `DLSSNRFilter.cpp:879-919` 已示范同类句柄流程）。优先方案是「解码设备 ==
   filter 设备」而非跨设备共享。
4. **BGRA vs RGBA 顺序**：DLSSNRFilter 两者都收（§2.1），BGRA 时内部自带转换；解码侧如果自己转 RGB，
   输出 BGRA 或 RGBA 均可。
5. **时间戳**：离线无显示时序，直接按帧序递增 `frameId` 即可（§2.4）。

### a.3 Windows Media Foundation 对比：可行但更绕，且 AV1 支持弱

- **D3D11 纹理路径存在但间接**：MF 用 `IMFSourceReader` 解码，拿 D3D11 纹理要 `IMFMediaBuffer →
  IMFDXGIBuffer::GetResource`（得到 `ID3D11Texture2D*`），还需 `MFCreateDXGIDeviceManager` +
  `MF_SOURCE_READER_D3D_MANAGER` 属性挂 DXGI 设备管理器，并用 `MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING`
  让 SourceReader 输出可直接渲染的格式。一手来源：
  https://learn.microsoft.com/en-us/windows/win32/api/mfobjects/nn-mfobjects-imfdxgibuffer ，
  https://learn.microsoft.com/en-us/windows/win32/medfound/mf-source-reader-enable-advanced-video-processing 。
  相比 FFmpeg 的 `AVFrame->data[0]` 直接就是纹理，MF 多一层 buffer 包络，逐帧管线控制（解码器实例、
  hwdevice、格式协商）也更绕。
- **AV1 非内置**：MF 不内置 AV1 解码；需要安装微软商店的「AV1 Video Extension」
  （https://apps.microsoft.com/detail/9mvzqmzj9zvd ），且硬件解码依赖显卡厂商 MFT 是否提供 AV1 解码器。
  FFmpeg 的 `av1_d3d11va` 则随 FFmpeg 自带（§a.1）。
- **成熟度结论**：MF 的 SourceReader 面向「快速播放/转码」场景，对「逐帧拿到 GPU 纹理 → 外部 filter →
  再编码」的自定义管线支持较生硬；FFmpeg libav 在同一 hwdevice 上串起 d3d11va 解码与 nvenc 编码是成熟、
  文档完备的路径（NVIDIA 官方文档即按 FFmpeg 演示），因此 MF 不是更优选择（最终判定见 §7 推荐 5）。

---

## (b) 编码：FFmpeg + NVENC 关键参数与质量优先默认值

### b.1 选项语义（一手：https://ffmpeg.org/ffmpeg-codecs.html 的 h264_nvenc / hevc_nvenc / av1_nvenc 节；
选项实现见 libavcodec/nvenc.c）

| 选项 | 语义（按官方文档） | 质量优先建议 |
|---|---|---|
| `-preset` | `p1`~`p7`（p1 最快、p7 最慢/质量最高；旧别名 slow/medium/fast） | `p7` |
| `-tune` | `hq`（默认）、`ll`、`ull`、`lossless` | `hq`（默认即可） |
| `-rc` | `constqp` / `vbr` / `cbr` / `cbr_ld_hq` / `cbr_hq` / `vbr_hq` / `lossless` | `vbr`（配 `-cq`）|
| `-cq` | “Set target quality level (0 to 51, 0 = automatic, 1 = lowest quality, 51 = highest quality)” | 见下方方向性说明 |
| `-b:v 0` | CQ 模式下把码率上下限置 0，让编码器纯按目标质量跑（FFmpeg 2020-05 起在 CQ 模式自动清零平均/最大码率：https://ffmpeg.org/pipermail/ffmpeg-devel/2020-May/263050.html ） | 与 `-rc vbr -cq` 搭配，双保险 |
| `-spatial_aq` / `-temporal_aq` | 空间/时域自适应量化，默认 0 | 均设 `1` |
| `-rc-lookahead` | 前看帧数（0=自动） | `20`~`32` |
| `-g` | GOP 大小（默认 250） | 按帧率 `2×fps` 或默认 |
| `-bf` / `-b_ref_mode` | B 帧数与参考帧模式 | 按需开 B 帧（`-bf 3`）提升压缩 |
| `-profile` | h264: baseline/main/high 等；hevc: main/main10/rext | `high`（H.264）|
| `-pix_fmt` | nvenc 原生吃 NV12；8bit SDR 用 `yuv420p`/`nv12`；HEVC/AV1 可 `p010le`（10bit） | 8bit 源用 `yuv420p`（DLSSNR 输出即 8bit SDR）|

**`-cq` 方向性（重要，易踩坑）**：FFmpeg 的 nvenc 帮助文本为
“Set target quality level (0 to 51, 0 = automatic, 1 = lowest quality, 51 = highest quality)”
（即**数字越大质量越高**，与 x264 CRF 相反；来源 ffmpeg-codecs.html + nvenc.c 选项表）。
而 NVIDIA 的 NVENC 指南在常量 QP（`cqLevel`）语境下描述为「1 最高、51 最低」，两套刻度方向相反，
是社区长期混淆点；FFmpeg `-rc vbr -cq` 走的是 `enableTargetQuality/targetQuality` 路径（目标质量）。
一手来源：https://docs.nvidia.com/video-technologies/video-codec-sdk/11.0/nvenc-application-note/index.html
（Rate Control 章节：VBR/CQ/ConstQP 模式说明）。
→ **建议**：以 FFmpeg 文档字面为默认（`-cq` 值从 19~25 起步实测校准）；若要确定性质量可改用
`-rc constqp -qp 18`（qp 越小质量越高，方向无争议）。AV1 nvenc 的 CQ 范围与 H.264/HEVC 不同
（FFmpeg 曾专门修正 av1_nvenc 的 CQ 范围：https://ffmpeg.org/pipermail/ffmpeg-devel/2024-May/328283.html ），
选 AV1 时需另行确认。

### b.2 NVIDIA 一手文档

- **「Using FFmpeg with NVIDIA GPU」**（NVIDIA 官方 FFmpeg 集成指南，含解码/编码/滤镜/质量建议）：
  https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/ffmpeg-with-nvidia-gpu/index.html
  （13.1：https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/#ffmpeg-with-nvidia-gpu ）。
- **NVENC Application Note**（RC 模式、CQ、预设、10bit、多编码器限制等）：
  https://docs.nvidia.com/video-technologies/video-codec-sdk/11.0/nvenc-application-note/index.html 。
- **Video Encode and Decode GPU Support Matrix**（哪块卡支持 H.264/HEVC/AV1 编解码、10bit 与否）：
  https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new 。

### b.3 结论：CBR / VBR / 无损之间**不必**取舍

- NVENC 提供 `constqp / vbr(+CQ) / cbr / lossless` 等独立 RC 模式（§b.1），「质量优先」的默认是
  **VBR + 目标质量 CQ**（`-rc vbr -cq N -b:v 0`），不是 CBR 也不是无损。
- **无损不是必须**：`-rc lossless`（或 `-qp 0`）可用，但输出体积爆炸（远超 x264 无损），且 NVENC 无损
  实际是「视觉无损」而非比特级无损；仅在极少数归档场景使用。→ 质量优先默认走 VBR+CQ。

### b.4 建议的质量优先默认值（CLI 形态）

```
-c:v h264_nvenc -preset p7 -tune hq -rc vbr -cq 20 -b:v 0 \
  -spatial_aq 1 -temporal_aq 1 -rc-lookahead 32 -profile:v high
```
（HEVC 同理换 `hevc_nvenc`；若日后想要 10bit 输出可 `-pix_fmt p010le`，但 DLSSNR 输出是 8bit RGBA8，
10bit 化对 8bit 源的收益有限——降 banding 可选，非必须。）

---

## (c) 输入覆盖：容器 / 编解码 / 音频直通

### c.1 容器与编解码（一手：https://ffmpeg.org/ffmpeg-formats.html 、https://ffmpeg.org/general.html ）

- 解封装（demuxer）：`mov,mp4,m4a,3gp,3g2,mj2`、`matroska,webm`、`mpegts`、`mxf`、`avi` 等内置支持。
- 解码器：`h264`、`hevc`、`av1`、`vp9`、`mpeg2video`、`vc1`、`prores`、`ffv1` 等内置；配合 d3d11va
  hwaccel 即硬件解码（§a.1）。→ mp4/mkv、H.264/HEVC/AV1 全覆盖，这是选 FFmpeg 的核心收益之一。

### c.2 音频直通（copy 流）

- **`-c:a copy`（= `-acodec copy`）语义**：不重编码，直接把音频包（packet）原样拷入输出容器；
  FFmpeg 文档 `-c[:stream_specifier] codec` 明确 `copy` 是「stream is not re-encoded」的特殊值。
  一手来源：https://ffmpeg.org/ffmpeg.html （`-c[:stream_specifier] codec` 一节）。
- **约束：输出容器必须支持该音频编码**：
  - mp4：AAC、MP3、AC-3/E-AC-3、Opus（mp4a）、FLAC（新版本）等可直通；DTS/TrueHD/老格式不行。
  - mkv/matroska：几乎任何音频（AAC/MP3/AC-3/DTS/TrueHD/FLAC/Opus/Vorbis/PCM）都可直通。
  - 不匹配时 `-c:a copy` 会报错 → 方案：换 mkv 容器，或 `-c:a aac -b:a 192k` 重编码音频。
  （容器音频格式支持列表见 https://ffmpeg.org/ffmpeg-formats.html 各 muxer 的 “audio codecs” 字段。）
- **流选择**：`-map 0:v -map 0:a?` 选视频+（可选的）音频；`-sn -dn` 丢字幕/数据流。
  对本应用：视频必须重编码（DLSSNR 处理），音频 `-c:a copy` 直通即可。

---

## (d) 输出默认值建议

### d.1 同分辨率 / 同帧率：可行且简单

- **同分辨率**：DLSSNRFilter 硬性要求输入=输出分辨率（§2.1，`DLSSNRFilter.cpp:1557-1562`），
  且默认 `enableInputResolutionScaling=false`（`DLSSNRFilter.h:10`）→ 不做缩放滤镜就是同分辨率；
  CLI 侧不需要 `-s`/`scale`。
- **同帧率**：解码帧序即输出帧序；FFmpeg 默认保持输入帧率。CLI 侧如需显式保证可用
  `-fps_mode passthrough`（或 `cfr`）与 `-r`（强制输出帧率）——`-fps_mode` 定义见
  https://ffmpeg.org/ffmpeg.html （`-fps_mode[:stream_specifier]` 一节：auto/cfr/vfr/passthrough/drop）。
  注意：滤镜图改动时间戳，本管线只有颜色转换无重采样，时间戳天然保持。

### d.2 编码器 / 质量档 / 容器

- 默认编码器：`h264_nvenc`（H.264，兼容性最好），质量优先档见 §b.4；`hevc_nvenc` 作为可选项（体积更小，
  RTX 全系支持）。
- 默认容器：**mp4**（H.264/HEVC + AAC 直通最稳、播放器兼容最好）；若音频无法直通 mp4（如 DTS/TrueHD）
  或编码器选 AV1，则退到 **mkv**。
- 自定义应用（非 CLI）：nvenc 编码器可用 `AV_HWDEVICE_TYPE_D3D11VA` hwdevice 直接收 D3D11 纹理
  （`AV_PIX_FMT_D3D11` hwframe），与解码同设备零拷贝（§a.1 第 4 条 + NVIDIA FFmpeg 文档）。
  DLSSNR 输出 RGBA8 纹理 → 由 nvenc 路径做 RGB→NV12 转换（FFmpeg 内部处理）或先经 D3D11 VideoProcessor。

---

## 7. 结论对推荐的影响

| # | 推荐 | 判定 | 证据（一句话） |
|---|------|------|----------------|
| 1 | 解码用 FFmpeg(libav) + D3D11VA 硬件解码，解码帧直接以 D3D11 纹理/DXGI 共享资源喂给 DLSSNRFilter | **CONFIRMED（带两个前提）** | d3d11va 解码帧就是 `ID3D11Texture2D*`，且能把既有设备注入 FFmpeg（hwcontext_d3d11va.h doxygen）；仓库 filter 本身收 D3D11 纹理并已示范共享句柄桥接（`DLSSNRFilter.cpp:879-919`）。前提①：离线应用自建设备时加 `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`（Magpie 现有设备缺此标志，`DeviceResources.cpp:169`；MS Learn 标志页）；前提②：解码 NV12 需 GPU 侧转 BGRA/RGBA8 后再进 filter（filter 只收 RGBA8/BGRA8，`DLSSNRFilter.cpp:1563-1570`）。 |
| 2 | 编码用 FFmpeg + NVENC（H.264/HEVC） | **CONFIRMED** | nvenc 接受 D3D11 纹理输入（FFmpeg HWAccelIntro + NVIDIA「Using FFmpeg with NVIDIA GPU」）；质量优先默认 `-preset p7 -tune hq -rc vbr -cq N -b:v 0 -spatial_aq 1 -temporal_aq 1`（ffmpeg-codecs.html 选项表）。 |
| 3 | 音频流直通复制 | **CONFIRMED** | `-c:a copy` 为包拷贝不重编码（ffmpeg.html `-c` 一节）；唯一约束是输出容器须支持原音频编码（mp4 对 AAC/MP3/AC-3/Opus 可，DTS/TrueHD 需 mkv 或转 AAC，ffmpeg-formats.html）。 |
| 4 | 输出默认同分辨率同帧率，默认编码器 H.264、质量优先 | **CONFIRMED** | 同分辨率由 filter 硬性契约保证（`DLSSNRFilter.cpp:1557-1562`）且默认无缩放（`DLSSNRFilter.h:10`）；同帧率用 `-fps_mode passthrough`/`cfr`（ffmpeg.html）；默认 `h264_nvenc` + §b.4 质量档，容器 mp4。 |
| 5 | Media Foundation 不是更优选择 | **CONFIRMED** | MF 拿 D3D11 纹理要 `IMFDXGIBuffer::GetResource` + DXGI 设备管理器 + Advanced Video Processing 属性（MS Learn），比 FFmpeg 的 `AVFrame->data[0]` 直接是纹理更绕；MF 不内置 AV1（需商店 AV1 Video Extension），FFmpeg `av1_d3d11va` 自带；NVIDIA 官方 FFmpeg 集成文档成熟。 |

---

## 附：本地一手证据索引（仓库文件 + 行号）

- `src/Magpie.Core/DLSSNRFilter.h:26-28,41-47` — 同分辨率 SDR DLSSNR；Initialize(input/output) 签名。
- `src/Magpie.Core/DLSSNRFilter.cpp:879-919` — D3D11 shared 纹理 + D3D12 OpenSharedHandle 桥接。
- `src/Magpie.Core/DLSSNRFilter.cpp:1535-1799` — Initialize：格式/分辨率校验、shared 纹理、snippet 加载。
- `src/Magpie.Core/DLSSNRFilter.cpp:1844-2077` — Draw：输入准备、栅栏同步、输出。
- `src/Magpie.Core/DeviceResources.h:15-18` — 设备接口。
- `src/Magpie.Core/DeviceResources.cpp:162-201` — D3D11 设备创建（BGRA_SUPPORT，无 VIDEO_SUPPORT）。
- `src/Magpie.Core/NativeEffectBackend.h:8-14` — DrawContext。
- `src/Magpie.Core/NativeEffectBackendFactory.cpp:65-108` — DLSSNRSettings 参数映射。
- `src/Magpie.Core/FrameGuidanceService.cpp:112-121,180-212,320-343,612-619` — zero guidance 构造。
- `src/Magpie.Core/FrameGuidanceTypes.h:89-142` — FrameGuidanceView/Resource/Metadata 结构与校验。
- `src/Magpie.Core/Renderer.cpp:1321-1328,1649-1666` — backend 纹理与 Draw 调用。
- 全仓库 grep：无 ffmpeg/libav/Media Foundation/NVENC/DXVA 引用。
