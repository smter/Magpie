# 离线时序引导调研：NVOF / DAV2 / Frame Guidance（Temporal guidance offline research）

> 调研对象：Magpie Experimental fork（`smter/Magpie`，分支 `experimental`，C++/WinRT，GPLv3）。
> 背景目标：规划一个新的离线应用 —— 逐帧处理视频文件（帧序列干净、严格按序、帧率固定），复用 Magpie 的
> DLSSNR（`src/Magpie.Core/DLSSNRFilter.{h,cpp}`，同分辨率 SDR AI 降噪）以及 Frame Guidance
> （运动来自 `NvidiaOpticalFlowProvider`（NVOF），深度来自 `DepthAnythingV2Provider`（DAV2，ONNX/DirectML-TRT 推理））。
> 对照场景：Magpie 是实时窗口捕获（帧率不定、画面切屏/失焦、历史需要 reset）；离线场景帧序列干净、严格按序、帧率固定。
>
> 本文件为本 effort 约定的调研存放位置 `docs/research/temporal-guidance-offline.md`。每条论断注明来源
> （仓库文件路径 + 行号，或一手 URL）。一手资料优先（官方文档 / 源码 / 规范），不用二手转述。

---

## 目录

1. [调研方法与来源约定](#1-调研方法与来源约定)
2. [(a) NvidiaOpticalFlowProvider：输入输出契约、帧推进语义、输入限制](#a-nvidiaopticalflowprovider输入输出契约帧推进语义输入限制)
3. [(b) DLSSNRFilter：时序 / 历史 / reset 生命周期](#b-dlssnrfilter时序--历史--reset-生命周期)
4. [(c) DepthAnythingV2Provider：推理成本与离线建议](#c-depthanythingv2provider推理成本与离线建议)
5. [(d) ZeroFrameGuidanceProvider / FrameGuidanceTypes：Force Zero 的质量代价](#d-zeroframeguidanceprovider--frameguidancetypesforce-zero-的质量代价)
6. [(e) 输入分辨率缩放 + Residual Multiplier](#e-输入分辨率缩放--residual-multiplier)
7. [结论对推荐的影响](#7-结论对推荐的影响)

---

## 1. 调研方法与来源约定

- 代码侧全部用 `read` / `grep` 直接读本仓库源码；外部事实只采信一手来源：
  - NVIDIA Optical Flow SDK：`https://developer.nvidia.com/opticalflow/download`、
    NVOFA Programming Guide `https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-programming-guide/`、
    NVOFA Application Note `https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-application-note/index.html`、
    SDK 头文件 `nvOpticalFlowCommon.h`（`NV_OF_INIT_PARAMS` / `NV_OF_EXECUTE_INPUT_PARAMS` / `NV_OF_BUFFER_FORMAT_*` / grid size / perf level /
    `disableTemporalHints`），镜像：`https://gitee.com/mirrors_NVIDIA/NVIDIAOpticalFlowSDK/blob/master/nvOpticalFlowCommon.h`（官方库 `NVIDIA/NVIDIAOpticalFlowSDK`）。
  - NVIDIA DLSS（官方公开仓库，含 NGX 参数头文件与编程指南）：`https://github.com/NVIDIA/DLSS`、
    `https://github.com/NVIDIA/DLSS/tree/main/include`（`nvsdk_ngx_defs_dlssd.h` 等）、
    `https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf`；
    NVIDIA Streamline 门户：`https://developer.nvidia.com/rtx/streamline`。
  - Depth Anything V2（官方仓库）：`https://github.com/DepthAnything/Depth-Anything-V2`；
    官方 HF 组织：`https://huggingface.co/depth-anything`。
- 每条外部论断标注 URL；代码论断标注 `文件路径:行号`。二手来源（如 DeepWiki、OpenCV 类文档）仅在需要旁证 API 语义时引用并标注为二手。

---

## (a) NvidiaOpticalFlowProvider：输入输出契约、帧推进语义、输入限制

### a.1 输入输出契约

- **输入**：`BeginFrame(const FrameGuidanceFrame& frame, MotionVectorProviderOutput& output)`；
  要求 `frame.color`（ID3D11Texture2D）非空、`frame.sourceExtent == session 的 extent`、session 存在，否则返回失败。
  `src/Magpie.Core/NvidiaOpticalFlowProvider.cpp:599-606`。
- **输入格式（本实现硬性要求）**：初始化时查询 NVOF 支持的 input/output/cost 格式，必须同时满足
  输入 `DXGI_FORMAT_B8G8R8A8_UNORM`（= NVOF `NV_OF_BUFFER_FORMAT_ABGR8`）、输出流 `R16G16_SINT`（S10.5）、
  代价 `R8_UINT`，否则 `"NVOF required ABGR8/S10.5/R8 cost formats unavailable"` 并失败。
  `NvidiaOpticalFlowProvider.cpp:439-450`；`NV_OF_INIT_PARAMS.inputBufferFormat = NV_OF_BUFFER_FORMAT_ABGR8`（`NvidiaOpticalFlowProvider.cpp:476`）。
  → **不是单通道**：解码侧必须先产出 BGRA8（或 RGBA8 再转换）全彩纹理，不能把 Y/NV12 直接喂给 NVOF。
  NVOF API 本身还支持 NV12/R8 等其它输入格式（`nvOpticalFlowCommon.h` 的 `NV_OF_BUFFER_FORMAT_*`），但本仓库实现只走 ABGR8。
- **输出**：`MotionVectorProviderOutput.motion`（`DXGI_FORMAT_R16G16_FLOAT`，全源分辨率逐像素 float2）+
  `confidence`（`DXGI_FORMAT_R8_UNORM`）+ 元数据。`NvidiaOpticalFlowProvider.cpp:352-357, 650-663`。
- **运动向量约定**：`当前帧 → 前一帧 / 源像素`（log 明确 "convention=current-to-previous/source-pixels"，
  `NvidiaOpticalFlowProvider.cpp:496-501`）。NVOF 原始输出是每 grid 块一个向量，S10.5 定点（`DecodeS105`，`/32.0`，
  `NvidiaOpticalFlowProvider.cpp:98-105`），由 compute shader `Densify` 上采样到逐像素，并用前向-后向一致性
  计算置信度（`1 - cost`，双向时再乘前向-后向误差项），`NvidiaOpticalFlowProvider.cpp:14-87`。
- **网格尺寸**：初始化查询 `NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES`，优先 4、再 2、再 1（grid=4 时一块管 4×4 像素），
  `NvidiaOpticalFlowProvider.cpp:452-460`；`perfLevel = NV_OF_PERF_LEVEL_MEDIUM`（`NvidiaOpticalFlowProvider.cpp:468`）。
- **双向**：`predDirection = NV_OF_PRED_DIRECTION_BOTH`，不支持时退回 FORWARD（`NvidiaOpticalFlowProvider.cpp:474-483`）。
  双向时用前向+后向流做遮挡/一致性校验，输出更高的置信度。

### a.2 frameId / 帧推进语义

- **NVOF 本身不感知帧号**：它只拿「上一次 `BeginFrame` 的帧」作参考帧、拿当前帧作输入帧做两帧光流
  （ping-pong 双缓冲 `input[2]`，`NvidiaOpticalFlowProvider.cpp:562, 608-640`）。
  `frameId` 只是透传到元数据（`MakeMetadata`，`NvidiaOpticalFlowProvider.cpp:107-121`），不校验单调/连续。
- **首帧 / reset 后**：`historyValid=false` 时不跑 NVOF，直接把 Dense 输出清 0（零运动 + 零置信度）并置位历史，
  `NvidiaOpticalFlowProvider.cpp:610-614`。这是「第一帧无运动」的天然语义。
- **时序提示**：正常帧 `disableTemporalHints = (resetReason == None ? NV_OF_FALSE : NV_OF_TRUE)`，
  `NvidiaOpticalFlowProvider.cpp:615-620`。即只有 reset 帧才关闭 NVOF 驱动内部跨帧时序提示；干净连续序列
  始终开启，可提升精度/一致性（NVOF 文档中 temporal hints 面向「长度大于 2 帧的序列」，
  `nvOpticalFlowCommon.h` 的 `disableTemporalHints` / NVOFA Application Note）。
- **对输入帧的限制（离线逐帧消费是否可行）**：
  - 分辨率：session 在 `Initialize/Resize` 时按源 extent 固定，`BeginFrame` 校验 extent 一致（`:604-606`）；
    `Resize` = 销毁并重建 session，历史清零（`:674-681`）。**分辨率必须恒定**，离线逐帧处理满足。
  - 格式/设备：ABGR8、且与 filter 同一 D3D11 设备（纹理由 `CopyResource` 拷入 ping-pong，`:609`）。
  - 未发现额外的对齐约束（代码不校验奇偶/对齐，`NV_OF_INIT_PARAMS` 直接把宽高传给驱动，`:462-477`）；
    NVOF 自身约束以 NVOFA Programming Guide 为准，离线实现按「偶尺寸安全 + 初始化时用 `nvOFGetCaps` 验证」处理即可。
  - **重复帧**：两帧相同 → 算出零位移（合法但浪费一次 NVOF 调用）；**丢帧/间隔帧**：两帧间隔 >1 帧时，
    NVOF 给出的是「整个间隔的位移」，而 DLSSNR 把 MVec 解释为「每帧位移」，因此间隔帧会把运动向量放大为
    错误的每帧速度。→ **干净、严格按序、无重复、帧率固定的帧序列正是 NVOF 的理想输入**：
    位移恒等于每帧运动、temporal hints 全程开启、无浪费调用、无非单调帧号。
- **结论（a）**：原样逐帧消费解码视频帧可行，但必须：① 解码帧先转成 ABGR8/BGRA8 且与 DLSSNR 同一 D3D11 设备；
  ② 恒定分辨率；③ 严格按序、无重复、无丢帧（这正是离线场景的自然属性，比 Magpie 实时捕获更有利）。

---

## (b) DLSSNRFilter：时序 / 历史 / reset 生命周期

### b.1 输入契约与设置

- `DLSSNRSettings`（`src/Magpie.Core/DLSSNRFilter.h:9-24`）：
  `enableInputResolutionScaling=false`、`inputResolutionPercent=100`、`residualMultiplier=1.0`、
  `guidanceMode=0`（0=both/1=Force Zero/2=motion only/3=depth only，注释 `DLSSNRFilter.h:21`）、
  `depthInferenceInterval=4`（默认 4）。
- `GetFrameGuidanceRequirements`：恒 `zero=true`；按 guidanceMode 声明 motion/depth；
  `depthInferenceInterval` 透传给服务（`DLSSNRFilter.cpp:1511-1533`）→ Renderer 用它构造
  `DepthAnythingV2Provider(interval)`（`Renderer.cpp:1569-1574`），最终在工厂层把参数 clamp 到 `[1,8]`
  （`NativeEffectBackendFactory.cpp:95-97`）。
- 同分辨率硬性约束、SDR 格式约束、BGRA→RGBA 内置转换：`DLSSNRFilter.cpp:1557-1584`（与
  `docs/research/video-pipeline-stack.md` §2 一致）。

### b.2 历史 / reset 生命周期（`PARAM_RESET` 是唯一历史控制）

- 逐帧 Evaluate 前设置 `PARAM_RESET = impl.resetHistory || guidanceReset ? 1 : 0`
  （`DLSSNRFilter.cpp:1222`）。`impl.resetHistory` 在 `Initialize` 时置 `true`（`DLSSNRFilter.cpp:476`），
  首帧后清 `false`（`DLSSNRFilter.cpp:2054`）。→ **DLSSNR 在会话起始必然 reset 一次**，离线首帧也要。
- `guidanceReset = evaluateGuidance->requiresHistoryReset && lastGuidanceResetFrameId != frameId`
  （`DLSSNRFilter.cpp:1949-1950`），每帧去重（`DLSSNRFilter.cpp:2051-2053`）。
  `requiresHistoryReset` 由服务的元数据聚合而来（`FrameGuidanceService.cpp:673-676`），
  其源头是提供者 metadata 的 `resetReason != None`（`NvidiaOpticalFlowProvider.cpp:119`、
  `DepthAnythingV2Provider.cpp:303`、`ZeroFrameGuidanceProvider.cpp:90`）以及服务级 ProviderFailure 降级
  （`FrameGuidanceService.cpp:634-666`）。
- **什么时候必须 Reset**（离线也适用）：首帧；分辨率变化（DLSSNR `Resize` 会整体 `Initialize`，
  `DLSSNRFilter.cpp:1801-1807`；NVOF/DAV2 `Resize` 各重建 session/worker，`NvidiaOpticalFlowProvider.cpp:674-681`、
  `DepthAnythingV2Provider.cpp:1398-1418`）；ProviderFailure（NVOF 执行失败置 `historyValid=false`，
  `NvidiaOpticalFlowProvider.cpp:633-639`）；以及剪辑边界/切换输入（离线语义上应视为一次 reset）。
- **Magpie 独有的 reset 触发（离线要规避）**：
  - `LongPause`：两次「新捕获帧」墙钟间隔 ≥500ms 即 `ResetHistory(LongPause)`（`Renderer.cpp:1619-1624`）。
    离线若某帧处理 >500ms（引擎首次构建、TensorRT 引擎编译、首次 DAV2 推理等）就会误触发、清空整段历史。
  - `CaptureInterrupted`：光标可见/源失焦事件（`Renderer.cpp:274-294`）——GUI 专属，离线无此事件。
  - `SceneChange`：枚举已定义（`FrameGuidanceTypes.h:42`）但**当前代码没有任何场景切换检测**（grep 仅见枚举与
    名称表 `FrameGuidanceService.cpp:391-405`）——离线不需要它，也不存在它。
- **Evaluate 复用 / 重复捕获保护**：
  - DLSSNR 侧：`frameId == lastEvaluatedFrameId` 时跳过 Evaluate、直接复用上次输出（`DLSSNRFilter.cpp:1851-1860`）。
  - 服务侧：`BeginFrame` 对相同 `frameId` 直接返回缓存视图（`FrameGuidanceService.cpp:478-481`），
    NVOF/DAV2 不会被重复调用。
  - StepTimer 的「最低帧率强制」：无新捕获帧且超过 `1/minFrameRate` 时 `ForceNewFrame` 强制再渲染同一帧
    （`StepTimer.cpp:53-55`；`Initialize` `StepTimer.cpp:8-24`）——这正是上述重复捕获保护的来源，是**显示节奏**问题。
  - 离线严格递增 frameId 完全不会触发这些路径；它们可以原样保留（无害）或直接不实现。
- **Evaluate 失败 = 会话级永久禁用**：Evaluate 失败置 `impl.disabled=true`（`DLSSNRFilter.cpp:1983-1993`），
  此后该会话一律 pass-through（拷贝输入或 residual 合成，`DLSSNRFilter.cpp:2031-2043`）。
  **离线要特别处理**：单帧失败若沿用此逻辑，会静默断掉整段视频的降噪。建议离线侧失败后按 reset 重试而非永久禁用。

### b.3 MVec / Depth 约定（喂给 NGX 的语义）

- `PARAM_DEPTH_INVERTED = 1` 恒定（`DLSSNRFilter.cpp:1217`）；`FrameGuidanceTypes.h:130` 默认
  `depthInverted = true`；`FrameGuidanceD3D12Interop::Update` 校验
  `CurrentToPrevious / SourcePixels / RelativeInverse / depthInverted=true`（`FrameGuidanceD3D12Interop.cpp:51-57`）。
  → DLSS 期望「倒置深度（近=1）+ 源像素单位、当前→前一帧的运动向量」；NVOF 输出 + DAV2 归一化输出与之完全匹配。
- `PARAM_MVEC_SCALE_X/Y = 1.0`（`DLSSNRFilter.cpp:1215-1216`）；使用分辨率缩放时，引导降采样 shader 已把
  运动按 `target/source` 比例缩放（`DLSSNRFilter.cpp:199-200, 1258-1259, 1349-1350`），故传给 NGX 的运动始终是
  「降采样后分辨率下的像素/帧」。
- MVec/Depth 的 Subrect 用 `validRegion`（`DLSSNRFilter.cpp:1207-1214`）；全帧时等于全区域。
- **置信度（confidence）不进 NGX**：`SetEvaluateParametersUnsafe` 只设 `PARAM_MVEC / PARAM_DEPTH`
  （`DLSSNRFilter.cpp:1203-1206`）；confidence 仅用于 DAV2 的深度时域滤波（重投影权重）与 NVOF 稠密化，
  不是 DLSSNR 的直接输入。→ Force Zero 时置信度归零只影响 DAV2 深度滤波路径。

### b.4 离线固定帧率逐帧处理会否触发 Magpie 场景问题

- 触发面：上述 LongPause（墙钟）、重复捕获复用、服务 frameId 缓存、DAV2 墙钟门控（见 §c）都是 Magpie 实时产物。
  干净固定帧率下：① 不会命中重复帧路径；② 只要处理速率 > 2fps 且不被慢帧卡 >500ms，就不会误触发 LongPause；
  但**引擎/后端初始化慢或某帧极慢时仍可能触发** → 离线实现应绕过/禁用 LongPause 这类墙钟 reset。
- 无需新增 Magpie 式 reset 保护；需要的是「首帧 reset + 分辨率/边界 reset + 失败重试」。

---

## (c) DepthAnythingV2Provider：推理成本与离线建议

### c.1 模型与路径

- 模型：`FrameGuidance\DepthAnythingV2\model_fp16.onnx`（FP16 ONNX），SHA256 固定
  `2df6223f206b5164e21f664ace61dabeb9bb6a49b8b5a3e00510b4807d0f5b04`（`DepthAnythingV2Provider.cpp:23-26`）；
  日志明确 `"DAV2 Small FP16 ... opset=14"`（`DepthAnythingV2Provider.cpp:634-642`）。
  → 模型是 **Depth Anything V2 Small**（vit-s，官方模型表 ~24.8M 参数，FP16 ONNX 体积约几十 MB；
  官方仓库模型表：Small/Base/Large/Giant，`https://github.com/DepthAnything/Depth-Anything-V2`）。
- 推理分辨率：`MODEL_LONG_SIDE = 336`，源帧按最长边缩到 336、再对齐到 14 的倍数（`AlignPatch`，最小 14）
  （`DepthAnythingV2Provider.cpp:29, 307-310, 401-404`）。注释明确：DLSSNR 只消费低频深度引导，
  336 已足够压低 ViT token 网格，NVOF 重投影负责时域连续性（`:27-28`）→ **离线也无需更高推理分辨率**。
- 后端：TensorRT（进程级共享、FP16、engine 缓存于
  `%LOCALAPPDATA%\Magpie\FrameGuidance\TensorRTCache`，缓存键含 ORT 1.24.4/TRT 版本/driver/SM/输入尺寸，
  `:580-592`），失败回退 DirectML（ONNX Runtime DirectML，`:624-632`）；推理失败时 TensorRT→DirectML 二次尝试
  （`:901-913`）。异步管线：preprocess(归一化, 3 通道 R32F)→staging 回读→worker 线程推理→p02/p98 分位
  →EMA(α=0.05) 归一化 →postprocess `saturate((raw-p02)/(p98-p02))`（相对倒置深度 [0,1]，近=1）→时域滤波。
  （`DepthAnythingV2Provider.cpp:36-84, 745-797, 835-934, 987-1045, 1114-1174`。）
- 推理成本结构：每帧只承担 preprocess + 时域滤波（GPU 极轻）；真正的 ONNX/TRT 推理在 worker 线程与渲染并行。
  代码把 DAV2 的墙钟冷却下限设为 33ms（`cooldown = clamp(workerTotal*2, 33ms, 500ms)`，`:1043-1045`），
  暗示其单次推理 + 回读通常落在 ~10–30ms 量级（参考量级，非实测）。

### c.2 每帧 vs 间隔 + Depth Inference Interval 语义

- `depthInferenceInterval` 是**帧号间距的最小值**：`frameId - lastCaptureFrameId >= interval` 才安排下一次捕获
  （`DepthAnythingV2Provider.cpp:1109-1111`），构造时 clamp `[1,8]`（`:1248`）。默认 4（`DLSSNRFilter.h:23`）。
- 间隔帧之间深度**不重新推理**：时域 shader 用 NVOF 运动把上一帧深度重投影到当前帧（`inside && confidence≥0.05`
  时取重投影值，否则保持原位；`DepthAnythingV2Provider.cpp:116-121`）；有当前推理时才做
  `historyWeight = 0.85 * confidence * saturate(1 - residual*8)` 的混合并输出残差（`:123-128`）。
- **Magpie 的墙钟门控（离线无意义）**：
  - NGX GPU 预算门控：DLSSNR GPU 时间 ≥10.5ms（软）时若深度年龄 <250ms 就跳过捕获，≥16ms（硬）则年龄上限 500ms
    （`:30-33, 1089-1107`）——这是「实时下让位给 DLSSNR」的启发式。
  - 冷却：`≥33ms` 墙钟最小捕获间隔（`:1043-1045`）——高帧率内容（如 120fps）下会让实际捕获频率低于 `interval`
    帧数所表示的值（120fps 下 interval=4 = 33ms，正好贴住冷却；240fps 下会被冷却压到每 ~8 帧）。
  - 非单调帧号（重复帧）会强制重捕获（`:1110`）——Magpie 产物，离线不触发。
- **对任意视频内容的限制**：模型是单目相对深度（RelativeInverse），对任意画面内容都输出有效深度；
  但 DAV2 是训练数据驱动的单目估计，对运动模糊/极端反光/非常规内容精度下降——这是模型层面的通用限制
  （官方仓库说明），非 Magpie 代码限制。归一化按帧级 p02/p98（EMA）做，长镜头下深度分布稳定。
- **离线 interval 建议**：离线没有实时预算竞争，`interval=1`（逐帧推理）提供最新鲜深度且调度最简单；
  若保留 4，需**把调度从墙钟解耦为纯帧计数**（绕过 NGX 预算门控与 ≥33ms 冷却），否则行为不可确定。
  `interval` 的合法范围是 `[1,8]`。

---

## (d) ZeroFrameGuidanceProvider / FrameGuidanceTypes：Force Zero 的质量代价

- **Force Zero 是「有效零引导」而非错误**：零提供者创建与真实引导同格式、同 extent 的全零纹理
  （depth `R32_FLOAT`、motion `R16G16_FLOAT`、confidence `R8_UNORM`，清 0，`ZeroFrameGuidanceProvider.cpp:36-77`），
  元数据 `isZero=true`（`:79-92`）；`FrameGuidanceView` 的校验只查格式/帧号/extent/区域，不查数值
  （`FrameGuidanceTypes.h:105-115, 133-142`）。`SelectFrameGuidanceChannels` 在 produced 无效时回退 zero 且
  保持视图有效（`FrameGuidanceTypes.h:145-171`）。
- DLSSNR 里 `guidanceMode==1` 把三通道全部替换为零视图（`DLSSNRFilter.cpp:1821-1836`）。
- **质量代价依据**：DLSS 系时域降噪/超分把历史按运动向量重投影后与当前帧融合，并用深度处理
  遮挡/视差与去遮挡（disocclusion）；运动与深度是时域累积的核心输入（NVIDIA DLSS 编程指南
  `https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf`，
  参数契约见 `nvsdk_ngx_defs_dlssd.h` 与 DLSSNR 同族 `nvsdk_ngx_defs_dlssnr.h`，`https://github.com/NVIDIA/DLSS/tree/main/include`）。
  - MVec 归零 → 历史不重投影，等于「静止场景假设」：运动物体出现残影/拖影（ghosting），
    且跨帧信息无法对齐，时域累积收益大减。
  - Depth 归零（全 0 = 全远）→ 无遮挡/视差信息：运动物体边缘、遮挡切换处更容易残影；
    也无法靠深度判断前景/背景来抑制错误历史融合。
  - 因此 Force Zero 的代价是**纯时域伪影/降质**，不是崩溃或失效——作为参数选项保留无风险，
    且是 NVOF/DAV2 不可用（如非 NVIDIA 引导、驱动缺 nvofapi64、模型缺失）时的合法降级路径
    （服务会在 ProviderFailure 时自动落到 zero + reset，`FrameGuidanceService.cpp:634-666`）。

---

## (e) 输入分辨率缩放 + Residual Multiplier

- 语义（`DLSSNRFilter.cpp:100-308, 1441-1506`）：`enableInputResolutionScaling=true` 时，
  `inputResolutionPercent`（clamp 25–100，`:1574-1575`）决定 DLSSNR 网络输入尺寸 `impl.width/height`
  （`= source * percent`，`:1576-1583`）；色彩与引导都先降采样（色彩 box 滤波，引导降采样把运动按比例缩放、
  深度取最邻近=保守最大值，`:199-200`），在**降采样分辨率**上跑 NGX Evaluate，再把
  `降噪结果 − 降采样原图` 的残差用 Lanczos3 上采样加回**全分辨率原图**，残差强度由
  `residualMultiplier`（init clamp `[1,2]`，`:1543-1544`；合成 shader clamp `[0,4]`，`:1458`）控制。
  - 注释明确：即使 100%（不降尺寸），只要开了 scaling 就走残差重建路径，避免「同尺寸绕过」静默忽略
    Residual Multiplier（`:1447-1450`）。
  - NVOF/DAV2 仍在**全源分辨率**运行（引导在 filter 内部降采样），所以该选项只省 DLSSNR 网络侧成本，
    不省引导成本。
- 离线适用性：**保留为高级选项完全合理**——默认关闭（`enableInputResolutionScaling=false, 100%, 1.0`，
  `DLSSNRFilter.h:10-12`），行为即「全分辨率直接降噪」；长视频/预算受限时可降 % 提速并靠残差找回细节。
  注意残差重建在快速运动/高噪内容上可能放大残差伪影，`residualMultiplier` 默认 1.0 即可。

---

## 7. 结论对推荐的影响

| # | 推荐 | 判定 | 证据（一句话） |
|---|------|------|----------------|
| 1 | 离线逐帧复用 NVOF + DAV2 引导（等同 Magpie "Available"/both 模式）可行且质量最好 | **CONFIRMED** | NVOF 对两帧连续色彩输入直接产出源像素/每帧位移，干净严格按序的帧序列是它的理想输入（无重复零流、无间隔放大运动、temporal hints 全程开启），DAV2 深度在其间由 NVOF 重投影维持——离线固定帧率恰为此最有利场景（`NvidiaOpticalFlowProvider.cpp:608-640`、`DepthAnythingV2Provider.cpp:116-121,1109-1111`）。 |
| 2 | Force Zero 仍保留为参数选项 | **CONFIRMED** | 零引导是「有效视图」而非错误（`ZeroFrameGuidanceProvider.cpp:36-92`），其代价仅是时域残影/降质而非失效，且是 NVOF/DAV2 不可用时的自动降级路径（`FrameGuidanceService.cpp:634-666`），保留无风险（`DLSSNRFilter.cpp:1821-1836`）。 |
| 3 | Depth Inference Interval 默认 4 离线适用 | **ADJUSTED** | 语义是帧号最小间距 `[1,8]`（`DepthAnythingV2Provider.cpp:1109-1111,1248`），离线无实时预算竞争，`interval=1` 更简单且深度最新；若保留 4，必须把调度从 Magpie 的墙钟门控（NGX 预算 250/500ms + ≥33ms 冷却，`:1089-1107,1043-1045`）解耦为纯帧计数。 |
| 4 | 离线干净帧序列对 DLSSNR 时域历史有利、不需要 Magpie 的 reset/重复帧保护 | **ADJUSTED** | 重复捕获复用/服务 frameId 缓存/非单调重捕获均不触发（`DLSSNRFilter.cpp:1851-1860`、`FrameGuidanceService.cpp:478-481`），但**仍需**：首帧 reset（`DLSSNRFilter.cpp:476`）、分辨率/剪辑边界 reset、且必须**绕过 500ms 墙钟 LongPause**（`Renderer.cpp:1619-1624`）与**失败即永久禁用**（`DLSSNRFilter.cpp:1983-1993`）两个 Magpie 产物，否则慢帧/单帧失败会清空或断掉整段历史。 |
| 5 | 输入分辨率缩放 + Residual Multiplier 可保留为高级选项 | **CONFIRMED** | 默认关闭且管线自洽（降采样推理 + Lanczos3 残差重建 + 运动预缩放，`DLSSNRFilter.cpp:100-308,1441-1506,1574-1583`），离线作为速度/细节权衡的高级选项无损（`DLSSNRFilter.h:10-12`）。 |

**给离线实现的最低动作清单**（供规划参考，非本次范围）：
1. 解码帧 → 同一 D3D11 设备上的 ABGR8/BGRA8 纹理，恒定分辨率；
2. `guidanceMode=0`、`interval=1`（或纯帧计数调度 + `interval=4`）；
3. 禁用/绕过 LongPause 墙钟 reset 与 DAV2 墙钟预算/冷却门控；
4. 首帧与分辨率/剪辑边界触发 reset；Evaluate 失败按 reset 重试而非永久 pass-through；
5. 保留 `guidanceMode=1`（Force Zero）、`enableInputResolutionScaling` + `residualMultiplier` 为高级参数。
