# 渲染与帧流架构调查：EffectDrawer / DLSSNRFilter / Frame Guidance（Magpie → MagpieVideo）

> 目标：为「复用边界与整体架构」(#6) 的 (c)(f) 决策收集事实——实时帧处理主链路、EffectDrawer/NativeEffectBackend 接口、DLSSNRFilter 输入输出、Frame Guidance 协作与离线需绕过的实时行为、离屏先例、D3D11 设备归属。
> 本文件只做事实调研，不做设计建议。所有论断标注来源（仓库路径 + 行号）。附关键头文件接口摘录。
> 调查对象：current checkout（experimental 分支）。

## TL;DR

- **帧主链路（实时）**：FrameSource（捕获源：DesktopDuplication / GraphicsCapture / DwmSharedSurface / GDI）→ [后端线程] `Renderer::_BackendRender` → effects 链（`EffectDrawer` 或 `NativeEffectBackend`）→ `_CreateSharedTexture`（共享纹理）→ [前端线程] `_FrontendRender` → `PresenterBase`（AdaptivePresenter / CompSwapchainPresenter）呈现。Renderer 拥有两套 `DeviceResources`（前端/后端）+ `FrameGuidanceService` + `NgxD3D12Core`（`Renderer.h:149-174`）。
- **DLSSNRFilter 挂接方式**：作为 `NativeEffectBackend` 挂在 effects 链的某一环。`_BuildEffects`（`Renderer.cpp:824-939`）对每个 effect 建一个 `EffectDrawer`（HLSL compute）+ 可选一个原生 backend（`CreateNativeEffectBackend`，878-891）；DLSSNR 匹配 effect 名 `"DLSSNR\\DLSSNR_AI_Filter"`（`NativeEffectBackendFactory.cpp:65`）。原生 backend 的输入 = `effectDrawer.GetTexture(0)`，输出 = `GetOutputTexture()`（883-884）。
- **DLSSNRFilter 输入/输出**：D3D11 纹理，**输入输出必须同分辨率**（`DLSSNRFilter.cpp:1557-1562`）；输入格式 `R8G8B8A8_UNORM` 或 `B8G8R8A8_UNORM`（后者自动转 RGBA），输出必须 `R8G8B8A8_UNORM`（1563-1570, 1584）；可选输入分辨率缩放 25–100%（默认 100 = 同分辨率，1574-1583）。外部服务：`DeviceResources&`（D3D11 设备/上下文）+ `NgxD3D12Core&`（NGX 的 D3D12 设备）+ 自建 D3D12 命令队列/列表（1592-1607）+ D3D11↔D3D12 共享纹理（1609-1618）。
- **Frame Guidance 协作**：`FrameGuidanceService`（Renderer 成员）持有 providers：`SetMotionVectorProvider(NvidiaOpticalFlowProvider)`、`SetDepthProvider(DepthAnythingV2Provider(interval))`（`Renderer.cpp:1563-1574`，编译开关 `MP_ENABLE_NVIDIA_OPTICAL_FLOW`/`MP_ENABLE_DEPTH_ANYTHING_V2`）。每新捕获帧 `BeginFrame`（1631-1632）→ `_Produce`：先 motion 后 depth（`FrameGuidanceService.cpp:576-663`）→ 消费方用 `GetConsumerViews(frameId, extent)` 取 `FrameGuidanceView`（depth=R32_FLOAT、motion=R16G16_FLOAT、confidence=R8_UNORM，`FrameGuidanceTypes.h:137-139`）。
- **需绕过的实时行为（事实）**：① **LongPause 墙钟 reset**：距上帧捕获 ≥ 500ms 就 `ResetHistory(LongPause)`（`Renderer.cpp:1618-1624`）；② **Evaluate 失败即永久 pass-through**：`DLSSNRFilter::Draw` 的 `fail()` 置 `impl.disabled=true`（"disabled=true fallback=pass-through-next-frame"，1861-1868），EvaluateFeature 失败（SEH 或非成功）也置 disabled（1983-1993），之后每帧只把输入拷到输出；③ **Depth Inference 墙钟/GPU 预算门控**：DAV2 决策里除帧计数（`frameId - lastCaptureFrameId >= interval`，1111）外还有 `nextCaptureTime` 墙钟（1108）与 NGX GPU 预算软/硬门控（1095-1106）——离线应走纯帧计数路径（interval=1 或显式调度）；④ **重复帧复用**：`lastEvaluatedFrameId==frameId` 直接跳过 Evaluate（1851-1860）——离线若帧 id 唯一不会触发。
- **离屏先例**：**无**。仓库没有 test/ 单测目录；`tools/` 只是开发工具。最接近的是 `EffectDrawer::DrawForExport(desc, passIdx)`（`EffectDrawer.h:38`，配合 `Renderer::TakeScreenshot` 导出中间 pass），但仍在实时 Renderer 内、需要 HWND。Renderer::Initialize 强依赖 HWND（`Renderer.h:25`）与捕获型 FrameSource。
- **GPU 设备归属**：`DeviceResources` 由 Renderer 内部创建（`_frontendResources.Initialize(true)` `Renderer.cpp:171`、`_backendResources.Initialize(false)` `Renderer.cpp:1482`），EffectDrawer/DLSSNRFilter 只接收 `DeviceResources&`，不建设备。**设备 flag 只有 `D3D11_CREATE_DEVICE_BGRA_SUPPORT`（+DEBUG / +SINGLETHREADED / +PREVENT_INTERNAL_THREADING_OPTIMIZATIONS），没有 `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`**（`DeviceResources.cpp:169-185`）——离线 D3D11VA 解码需自建设备时补这个 flag（与 #2 结论一致）。

---

## 1. 帧处理主链路（实时模式）

### 线程模型与类归属

- `ScalingWindow`（窗口模式宿主，单例 `ScalingWindow.h:15`）拥有 `Renderer`；`Renderer::Initialize(HWND hwndAttach, OverlayOptions&)`（`Renderer.h:25`）。
- Renderer 双线程（`Renderer.h:149-238` 注释「只能由前台线程/后台线程访问」）：
  - **前端**：`DeviceResources _frontendResources`、`std::unique_ptr<PresenterBase> _presenter`（149-150）。
  - **后端**：`DeviceResources _backendResources`、`BackendDescriptorStore`、`std::unique_ptr<FrameSourceBase> _frameSource`、`FrameGuidanceService _frameGuidanceService`、`NgxD3D12Core _ngxD3D12Core`、`std::vector<EffectDrawer> _effectDrawers`、`std::vector<std::unique_ptr<NativeEffectBackend>> _nativeEffectBackends`、`std::unique_ptr<DLSSFrameGenerator>`（168-177）。
- 前端/后端通过共享 D3D11 纹理 + KeyedMutex 环形槽通信（`_backendSharedTextures`、`_frontendSharedTextures`、MAX_SHARED_TEXTURE_SLOTS=4，155-206）。

### 调用链

1. `Renderer::_InitBackend`（`Renderer.h:98`）：`_backendResources.Initialize(false)`（`Renderer.cpp:1482`）→ `_InitFrameSource()`（创建 FrameSource 子类，101）→ `_BuildEffects()`（1554）→ FrameGuidanceService 装配（1559-1580）→ `CreateFence`（1582）→ `_CreateSharedTexture(outputTexture)`（1598）→ `_frameSource->Start()`（1605）。
2. 每帧：`FrameSource::Update()` → `FrameSourceState::NewFrame`（`FrameSourceBase.h:34`），`GetOutput()` 返回 `ID3D11Texture2D*`（40-42）；含重复帧检测（`FrameSourceBase.h:79-94`）。
3. `Renderer::_BackendRender(effectsOutput, isNewCaptureFrame)`（`Renderer.cpp:1613-…`）：新捕获帧时做 LongPause 检查（1618-1624）、`++_capturedFrameId`（1626）、`_frameGuidanceService.BeginFrame(frameId, color, requirements)`（1631-1632）；然后 ClearState、`_UpdateDynamicConstants`、effects 循环（1647-1671）：每个 effect 若有原生 backend 则 `GetConsumerViews` + `backend->Draw(drawContext)`，否则 `effectDrawer.Draw(profiler)`。
4. 呈现：`_CreateSharedTexture` 把 effects 输出放入共享槽，前端线程 `_FrontendRender` 取槽、`_PublishBackendTexture`、由 `_presenter`（AdaptivePresenter / CompSwapchainPresenter）呈现。

### FrameSource 具体类（`Magpie.Core.vcxproj` 清单）

`DesktopDuplicationFrameSource`、`GraphicsCaptureFrameSource`、`DwmSharedSurfaceFrameSource`、`GDIFrameSource`（`Magpie.Core.vcxproj:53-63`）。接口见 `FrameSourceBase.h`：`Initialize(DeviceResources&, BackendDescriptorStore&)`（30）、`Start()`（32）、`Update()`（34）、`GetOutput()`（40）。

## 2. EffectDrawer / NativeEffectBackend

### EffectDrawer（HLSL compute 效果，`EffectDrawer.h`）

```cpp
bool Initialize(const EffectDesc& desc, const EffectOption& option,
    DeviceResources& deviceResources, BackendDescriptorStore& descriptorStore,
    ID3D11Texture2D** inOutTexture) noexcept;                 // 28-34
void Draw(EffectsProfiler& profiler) const noexcept;          // 36
void DrawForExport(const EffectDesc& desc, uint32_t passIdx) const noexcept; // 38
bool ResizeTextures(const EffectDesc& desc, const EffectOption& option,
    DeviceResources&, ID3D11Texture2D** inOutTexture) noexcept; // 40-45
ID3D11Texture2D* GetOutputTexture() const noexcept;           // 47-49 (_textures[1])
ID3D11Texture2D* GetTexture(uint32_t idx) const noexcept;     // 51-53
```
- 输入纹理以 `inOutTexture` 引用传递（链式），成员 `_textures[0]=输入, [1]=输出`。用 muparser 求参表达式（`EffectDrawer.h:9,96`）。
- `DrawForExport` 按 passIdx 渲染指定 pass——用于截屏导出，是「单帧单 pass 离屏渲染」的最接近既有路径（见 §5）。

### NativeEffectBackend（原生 SDK 效果契约，`NativeEffectBackend.h`）

```cpp
struct NativeEffectDrawContext {
    ID3D11Texture2D* input = nullptr;
    ID3D11Texture2D* output = nullptr;
    FrameGuidanceFrameId frameId = 0;
    const FrameGuidanceView& frameGuidance;
    const FrameGuidanceView& zeroFrameGuidance;
};                                                                // 8-14
class NativeEffectBackend {
    virtual FrameGuidanceRequirements GetFrameGuidanceRequirements() const noexcept; // 23
    virtual bool Drain() noexcept { return true; }               // 26
    virtual bool Resize(DeviceResources&, ID3D11Texture2D* input, ID3D11Texture2D* output) noexcept = 0; // 28-32
    virtual bool Draw(const NativeEffectDrawContext& context) noexcept = 0; // 34
};
```

### effects 链的组织与 DLSSNR 挂接

- 每个缩放模式 = `options.effects`（`std::vector<EffectOption>`）。`_BuildEffects`（`Renderer.cpp:824-939`）并行 `CompileEffect` 出 `EffectDesc`，然后对每个 effect：
  - `_effectDrawers[i].Initialize(desc, effects[i], _backendResources, _backendDescriptorStore, &inOutTexture)`（867-873）；
  - `CreateNativeEffectBackend(effects[i].name, effects[i], _backendResources, _ngxD3D12Core, _effectDrawers[i].GetTexture(0), _effectDrawers[i].GetOutputTexture())`（878-884）——若名字匹配某原生 SDK 效果则返回 backend，存进 `_nativeEffectBackends[i]`（891）。
- `CreateNativeEffectBackend`（`NativeEffectBackendFactory.cpp:33-186`）：按 effect 名分派——`"DLSSNR\\DLSSNR_AI_Filter"` → `DLSSNRFilter`（65-108）；`Diagnostics\\FrameGuidance_*` → FrameGuidanceDiagnostics；DLSS/FSR2/FSR3/XeSS/RTXVideo 各自分支；不认识返回 `{recognized:false}`。
- 渲染循环里同一 index 的 EffectDrawer 与 NativeEffectBackend 二选一执行（`Renderer.cpp:1649-1671`）。

## 3. DLSSNRFilter（输入输出 / 外部服务）

```cpp
struct DLSSNRSettings {                      // DLSSNRFilter.h:9-24
    bool enableInputResolutionScaling = false;
    uint32_t inputResolutionPercent = 100;
    float residualMultiplier = 1.0f;
    int preset = 0; int style = 0;
    float intensity = 1.0f;
    float localToneStrength = 1.0f; float localStructureStrength = 1.0f;
    float skinStructureStrength = -1.0f;
    bool useAutoMask = false; bool uiCorrection = false;
    int guidanceMode = 0;                     // 0 both, 1 Force Zero, 2 motion, 3 depth
    uint32_t depthInferenceInterval = 4;
};
class DLSSNRFilter final : public NativeEffectBackend {   // DLSSNRFilter.h:29
    FrameGuidanceRequirements GetFrameGuidanceRequirements() const noexcept override;
    bool Drain() noexcept override;
    bool Initialize(DeviceResources& resources, NgxD3D12Core& ngxCore,
        ID3D11Texture2D* input, ID3D11Texture2D* output,
        const DLSSNRSettings& settings) noexcept;          // 41-47
    bool Resize(DeviceResources&, ID3D11Texture2D* input, ID3D11Texture2D* output) noexcept override;
    bool Draw(const NativeEffectDrawContext& context) noexcept override;
};
```

- 构造/初始化输入校验（`DLSSNRFilter.cpp:1553-1584`）：同分辨率；输入 RGBA8/BGRA8、输出 RGBA8；`sourceWidth/Height` 取自输入纹理 desc；`width/height` = 分辨率百分比（默认同分辨率）。`convertInputToRgba` 处理 BGRA8 输入。
- NGX 侧：`ngxCore.Acquire(resources, "DLSSNR")`（1586）→ 取 `ngxCore.Device()`（D3D12 设备）→ 自建 D3D12 command queue/allocator/list（1592-1607）→ `CreateSharedTexture` 建 D3D11 共享纹理对（1609-1618）→ NGX `NVSDK_NGX_D3D12_CreateFeature` + `NVSDK_NGX_D3D12_EvaluateFeature`（`impl.snippetEvaluateFeature`/`NVSDK_NGX_D3D12_EvaluateFeature`，1164-1171, 1965-1970）。
- `GetFrameGuidanceRequirements`（1511-1533）：`{zero:true}` + 按 `guidanceMode` 加 motion/depth + `depthInferenceInterval`。这是给 FrameGuidanceService 的「需要哪些引导」声明。
- 设置来源：effect 参数（HLSL `//!PARAMETER`）经 `NativeEffectBackendFactory.cpp:66-98` 映射成 `DLSSNRSettings`。

## 4. Frame Guidance（协作 / 需绕过的实时行为）

### 谁持有、如何协作

- `FrameGuidanceService`（`FrameGuidanceService.h:15`）是 Renderer 成员（`Renderer.h:171`），非全局单例。
- 装配（`Renderer.cpp:1559-1580`）：`CollectFrameGuidanceRequirements`（汇总各 backend + DLSSFG 的需求）→ 有需求时 `SetMotionVectorProvider(make_unique<NvidiaOpticalFlowProvider>())`（1565-1566，`#ifdef MP_ENABLE_NVIDIA_OPTICAL_FLOW`）、`SetDepthProvider(make_unique<DepthAnythingV2Provider>(requirements.depthInferenceInterval))`（1571-1573，`#ifdef MP_ENABLE_DEPTH_ANYTHING_V2`）→ `Initialize(_backendResources, _frameSource->GetOutput(), requirements)`（1576-1579）。
- 每帧（`FrameGuidanceService.cpp:576-663 _Produce`）：motion provider 先跑（产出 motion+confidence），depth provider 用 frame.motionGuidance 再跑（可重投影上一帧深度）。`BeginFrame(frameId, color, requirements)`（`FrameGuidanceService.h:31-35`）。
- 消费：`GetConsumerViews(frameId, targetExtent)` → `{produced, zero}`（`FrameGuidanceService.h:41-44`）；`FrameGuidanceView` 三个通道格式固定：depth `R32_FLOAT`、motion `R16G16_FLOAT`、confidence `R8_UNORM`（`FrameGuidanceTypes.h:137-139`）。`SelectFrameGuidanceChannels` 按 useMotion/useDepth 裁剪（`FrameGuidanceTypes.h:145-171`）。
- `FrameGuidanceResetReason` 枚举含 `Initialize/Resize/SceneChange/CaptureInterrupted/DeviceRecreated/LongPause/ProviderFailure`（`FrameGuidanceTypes.h:37-46`）。

### 具体实时行为（离线需绕过/复用）

1. **Depth Inference Interval**：`DLSSNRSettings.depthInferenceInterval`（默认 4）→ `requirements.depthInferenceInterval`（`DLSSNRFilter.cpp:1531`）→ `DepthAnythingV2Provider(interval)` 构造（`Renderer.cpp:1572-1573`），interval clamp 1–8（`DepthAnythingV2Provider.cpp:1246-1248`）。调度核心（`DepthAnythingV2Provider.cpp:1095-1112`）：
   - **帧计数**：`reset || lastCaptureFrameId==MAX || frame.frameId <= lastCaptureFrameId || frame.frameId - lastCaptureFrameId >= inferenceInterval`（1109-1111）——纯帧号增量，离线友好。
   - **墙钟/预算门控（实时特有，离线应跳过）**：`now < nextCaptureTime → 不捕获`（1108）；`NGX_GPU_SOFT/HARD_BUDGET_MS` + `lastNgxGpuBudgetMs` + 深度年龄上限（1097-1105）。
   - 异步管线：`QueueJob`/`RunInference`（worker 线程）、`ApplyResult`、`lastInferenceFrameId`、`skippedInferenceCount`（735-1035, 1226-1239）。
2. **LongPause 墙钟 reset**：`Renderer.cpp:1618-1624`——新捕获帧时，若距上帧 ≥ 500ms → `ResetHistory(FrameGuidanceResetReason::LongPause)`。离线逐帧没有「暂停」，此分支自然不触发，但若场景切换需显式 `ResetHistory(SceneChange)`（服务暴露 `ResetHistory`，`FrameGuidanceService.h:40`）。
3. **Evaluate 失败即永久 pass-through**：`DLSSNRFilter::Draw` 的 `fail()` lambda 置 `impl.disabled=true` 并记录 `"disabled=true fallback=pass-through-next-frame"`（1861-1868）；EvaluateFeature 本身失败（SEH 异常码或 `!NGXSucceeded`）也置 `disabled=true`（1983-1993）；`impl.disabled` 后每帧只把输入拷贝到输出（用 `sharedInputSrv` 合成，2031-2035）。**离线不应静默 pass-through**，应把失败当作业错误上报。
4. **重复帧复用**：`Draw` 开头 `lastEvaluatedFrameId==frameId` → 只计数不 Evaluate（1851-1860）——实时里同一捕获帧会被渲染多次，离线帧 id 唯一即不触发。
5. 应复用的：`FrameGuidanceService` 全量（BeginFrame/_Produce/GetConsumerViews/零引导）、`NvidiaOpticalFlowProvider`、`DepthAnythingV2Provider`（帧计数调度路径）、`FrameGuidanceD3D12Interop`（D3D11↔D3D12 引导纹理共享）、`NgxD3D12Core`。

## 5. 离屏先例

- **没有** test/ 目录（仓库顶层无 tests；`tools/` 下是 `MPVHookTextureParser`、`WindowCase` 开发工具，非离屏渲染测试）。
- 最接近的既有能力：`EffectDrawer::DrawForExport(desc, passIdx)`（`EffectDrawer.h:38`）+ `Renderer::TakeScreenshot(effectIdx, passIdx, outputIdx)`（`Renderer.h:70-74`，`_TakeScreenshotImpl` 141-144）——导出指定 effect/pass 的中间纹理，但仍在实时 Renderer 上下文、需要 HWND 与捕获源。
- `Renderer::Initialize(HWND hwndAttach, ...)`（`Renderer.h:25`）与 `_InitFrameSource` 都绑定窗口/屏幕捕获；**没有无窗口使用 EffectDrawer/DLSSNRFilter 的现成路径**，离线形态需自建设备 + 自驱帧循环。

## 6. GPU 设备

- 设备归属：**Renderer 内部创建两套 `DeviceResources`**（前端 `Renderer.cpp:171`，后端 `1482`），传给 EffectDrawer/FrameGuidance/各 backend；`NgxD3D12Core`（NGX 的 D3D12 设备）也是 Renderer 成员（`Renderer.h:174`，`NgxD3D12Core.h:12-21` Acquire/Release/Device）。
- 创建参数（`DeviceResources.cpp:162-201`）：feature level 11_1/11_0（163-166）、`D3D_DRIVER_TYPE_UNKNOWN` + 指定 adapter（190-193）、flag 组合：
  - `D3D11_CREATE_DEVICE_BGRA_SUPPORT`（169）；
  - Debug 层可用时 +`D3D11_CREATE_DEVICE_DEBUG`（171-173）；
  - 前端设备或非 WGC 捕获时 +`D3D11_CREATE_DEVICE_SINGLETHREADED`（175-177）；
  - CompSwapchain 时 +`D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS`（178-185）。
  - **没有 `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`**——离线路线要自建设备并给 D3D11VA 解码用，必须自行加此 flag（与 #2 结论一致）。

---

## 关键文件索引

- `src/Magpie.Core/Renderer.{h,cpp}`、`ScalingWindow.h`、`DeviceResources.{h,cpp}`
- `src/Magpie.Core/EffectDrawer.h`、`NativeEffectBackend.h`、`NativeEffectBackendFactory.{h,cpp}`
- `src/Magpie.Core/DLSSNRFilter.{h,cpp}`、`NgxD3D12Core.h`
- `src/Magpie.Core/FrameGuidanceService.{h,cpp}`、`FrameGuidanceProvider.h`、`FrameGuidanceTypes.h`、`FrameGuidanceD3D12Interop.{h,cpp}`
- `src/Magpie.Core/NvidiaOpticalFlowProvider.{h,cpp}`、`DepthAnythingV2Provider.{h,cpp}`
- `src/Magpie.Core/FrameSourceBase.h`、`PresenterBase.h`、`AdaptivePresenter.h`、`CompSwapchainPresenter.h`
