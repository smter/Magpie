# Magpie / MagpieVideo 领域上下文

本仓库是 Magpie Experimental fork（GPLv3），现含两个应用方向：实时窗口缩放（magpie.exe）与离线视频 DLSSNR 处理（MagpieVideo，规划中）。术语以本上下文为准。

## 应用

**MagpieVideo**（暂名）:
与 magpie.exe 平级、同一发布目录的独立离线视频处理应用：输入一个视频文件，输出经 DLSSNR（含 Frame Guidance）同分辨率降噪的新视频文件。它是本仓库内的第二个 exe（sibling exe），不是 Magpie 的扩展模式。
_Avoid_: Magpie 模式、Magpie 插件

**缩放模式（scaling mode）**:
用户可见的「处理模式」概念。MagpieVideo v1 只含 DLSSNR 一个模式；config schema 沿用 Magpie 的 `scalingModes/effects/parameters` 结构。
_Avoid_: 滤镜、特效（effect 是内部单位，见下）

## 处理核心

**effect（效果）**:
Magpie effects 链的最小单元——HLSL compute 着色器或原生 SDK 后端（NativeEffectBackend）。MagpieVideo v1 不运行 effects 链，但复用其参数 schema（HLSL `//!PARAMETER` 元数据 + 配置里 `{参数名: float}` 映射）。

**DLSSNRFilter**:
NGX DLSS 神经同分辨率降噪过滤器（SDR）。MagpieVideo 的视觉处理核心：D3D11 RGBA8 同分辨率纹理进/出，依赖两个外部服务——DeviceResources（D3D11 设备）与 NgxD3D12Core（NGX 的 D3D12 设备）。
_Avoid_: 降噪滤镜、DLSSNR 效果

**Frame Guidance（时序引导）**:
由 NVOF 光流（motion）与 DepthAnythingV2 深度（depth）组成的时序引导信息，供 DLSSNR 做时间滤波。MagpieVideo v1 复用 Magpie.Core 的 FrameGuidanceService 与两个 provider。
_Avoid_: 光流、深度图（它们是引导的组成部分，不是引导本身）

## 处理语义

**帧流（frame pipeline）**:
解码 → GPU 转 RGBA8 → DLSSNR → 编码 的逐帧离线管线；它替代实时模式的 FrameSource（捕获）与 Presenter（呈现）。
_Avoid_: 渲染管线（渲染管线是实时缩放的前端/后端线程机制）

**离线语义（hard-fail）**:
MagpieVideo 的处理语义——DLSSNR 处理失败视为作业错误、中止并报告失败帧号，与实时模式的「失败即永久 pass-through」相对。
_Avoid_: 降级继续、跳过失败帧

**配置隔离**:
MagpieVideo 使用独立配置目录（`%LOCALAPPDATA%\MagpieVideo\config\v1`），不复用 Magpie 的配置；但与 Magpie 共用 Frame Guidance 引擎缓存目录（`%LOCALAPPDATA%\Magpie\FrameGuidance`）。
