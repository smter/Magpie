# 构建与工程结构调查：solution / 发布 / 依赖形态（Magpie → MagpieVideo）

> 目标：为「复用边界与整体架构」(#6) 的 (a)(b) 决策收集事实——现有 solution 的工程结构、发布构建、第三方依赖形态，以及在同一 solution 新增独立 exe 工程并链接 Magpie.Core 时会遇到的障碍。
> 本文件只做事实调研，不做设计建议。所有论断标注来源（仓库路径 + 行号）。
> 调查对象：当前 checkout（experimental 分支，HEAD 84d9f6ab）。

## TL;DR

- 解决方案已是 **`.slnx`（XML 格式）**，不是旧 .sln。平台 x64 / ARM64。工程：`Effects`（Utility，把 HLSL 源拷贝成 `effects/` 目录）、`Magpie.Core`（**静态库 .lib**）、`Magpie`（exe，DefaultStartup）、`TouchHelper`、`Updater`、`_ConanDeps`（Utility，跑 `conan install`）、`Shared.vcxitems`（共享头）。`Magpie.slnx:19-36`。
- **Magpie.Core 是静态库**（`Magpie.Core.vcxproj:20` `<ConfigurationType>StaticLibrary</ConfigurationType>`）。magpie.exe 通过 `ProjectReference` 引用它（`Magpie.vcxproj:728-731`），include 路径 `..\Magpie.Core\include`（`Magpie.vcxproj:84`），MSBuild 对静态库项目引用自动链接 .lib。**Magpie.Core 没有任何对 GUI 工程的引用**（无反向依赖）。
- 依赖 = **Conan 2（MSBuildDeps）+ NuGet + 手工指定的 SDK 目录**，不是 vcpkg。Conan 按工程 `conanfile.txt`（Magpie/TouchHelper/Updater 三个），`_ConanDeps` 工程扫描 `src/*/conanfile.txt` 并生成 `conandeps.props`（`_ConanDeps.vcxproj:41-124`）。Magpie.Core 显式导入 Magpie 的 conandeps.props 获得 fmt/spdlog/rapidjson 等（`Magpie.Core.vcxproj:30`）。NuGet：CppWinRT 3.0.260520.1、WinUI 2.8.7、WIL 1.0.260126.7、WebView2（`Magpie.vcxproj:3-4`）。SDK 目录（DLSS/NVOF/ONNX/TensorRT/XeSS/FSR 等）在 `src/BuildOptions.props:23-50` 用 `$(XxxDir)` 指定 + 特性开关，运行时 DLL 由 **Magpie.vcxproj 的 AfterTargets=Build 拷贝 target** 拷进 bin（`Magpie.vcxproj:732-780`）。
- **发布构建** `scripts/Build-Release.ps1`：停掉运行中的 Magpie → MSBuild Rebuild（带版本号属性）→ 校验必需运行时文件 → 把 `bin/x64/Release` 拷贝到 `release/<版本>/<包名>/`（剔除 .pdb/.lib/.exp/Magpie.next.exe）→ 清理 cache/logs → 可选剔除 `FrameGuidance\TensorRT` → 补 LICENSE/README/THIRD-PARTY-NOTICES → 生成 `build-manifest.json`（逐文件 sha256/版本）→ 归一化时间戳 → 打 zip（条目以 `<包名>/` 为前缀）。
- **新增同 solution exe 的坑（事实层面）**：① 链接 Magpie.Core 会把**全部** .obj 拉进来，连带其全局单例 `Logger::Get()`、`EffectCacheManager::Get()`、`ImGuiFontsCacheManager::Get()`、`ScalingWindow::Get()`；② Magpie.Core 硬编码路径：`%LOCALAPPDATA%\Magpie\FrameGuidance\TensorRTCache`（DAV2 引擎缓存）、`<exe>\FrameGuidance\DepthAnythingV2\model_fp16.onnx`、`<exe>\FrameGuidance\{TensorRT,DirectML}` 运行时目录；③ 日志 `logs\magpie.log` 相对路径 + `SetWorkingDir` 定为 exe 目录（`main.cpp:29-32,96,114-116`）；④ 初始化入口在 `wWinMain`：SetWorkingDir → InitializeLogger → `winrt::init_apartment` → `App::Get().Initialize`（其中 `AppSettings::Get().Initialize`，`App.cpp:148-152`）；⑤ NGX/D3D12 链接依赖（`nvsdk_ngx_s.lib`、`D3D12.lib`）与运行时拷贝 target 目前**只写在 Magpie.vcxproj**，新 exe 必须自行复制这些 Link 项与拷贝 target；⑥ 新 exe 若自己不带 conanfile.txt 则拿不到 conan 依赖，须像 Magpie.Core 那样导入 Magpie 的 conandeps.props 或自建 conanfile。

---

## (a) 解决方案结构

### 工程清单（`Magpie.slnx`）

| 工程 | 路径 | 产出形态 | 备注 |
|---|---|---|---|
| Effects | `src/Effects/Effects.vcxproj` | Utility（无二进制） | `CopyFileToFolders` 把 HLSL 源拷到输出 `effects/` 目录（`Effects.vcxproj:13-69`），含 `DLSSNR\DLSSNR_AI_Filter.hlsl` 等 |
| Magpie.Core | `src/Magpie.Core/Magpie.Core.vcxproj` | **StaticLibrary (.lib)** | `Magpie.Core.vcxproj:20`；含 EffectDrawer/DLSSNRFilter/Frame Guidance 等全部核心 |
| Magpie | `src/Magpie/Magpie.vcxproj` | Application (exe) | `Magpie.vcxproj:40`；DefaultStartup（`Magpie.slnx:23`） |
| TouchHelper | `src/TouchHelper/` | exe | 触控/UIAccess 辅助 |
| Updater | `src/Updater/` | exe | 自更新 |
| _ConanDeps | `src/_ConanDeps/_ConanDeps.vcxproj` | Utility | `conan install` 生成 conandeps.props（`_ConanDeps.vcxproj:60-125`） |
| Shared | `src/Shared/Shared.vcxitems` | 共享头 | Logger.h、CommonSharedConstants.h |

- 构建依赖（BuildDependency）：Magpie.Core→_ConanDeps；Magpie→Effects/TouchHelper/Updater/_ConanDeps（`Magpie.slnx:19-36`）。注意 **Magpie.Core 在 slnx 里没有指向 Magpie 的边**——核心层不依赖 GUI。

### Magpie.Core 怎么被链接

- `Magpie.vcxproj:728-731` `<ProjectReference Include="..\Magpie.Core\Magpie.Core.vcxproj">`。
- `Magpie.vcxproj:84` `<AdditionalIncludeDirectories>..\Magpie.Core\include;...`。
- Magpie.Core 头文件都放 `include/`（`Magpie.Core.vcxproj:35` `AdditionalIncludeDirectories>include`）。
- Magpie.Core 的 pch 只含 C++/WinRT 的 `Windows.*` 命名空间（`Magpie.Core/pch.h:42-56`），**不含任何 XAML/WinUI**。
- 所有工程共享：`Directory.Build.props`（C++20、禁 RTTI、/utf-8 等）、`src/Common.Pre.props`（BuildOptions、toolset v143/v145、clang-cl 支持）、`src/Common.Post.props`（特性宏、conan 导入、Shared 导入、HybridCRT）、`src/HybridCRT.props`（Release 用 `/MT` 静态 CRT，避免依赖 VCRUNTIME140.dll；`HybridCRT.props:17-26`）。

## (b) 发布构建（`scripts/Build-Release.ps1`）

流程（行号均为该脚本）：

1. 定位 MSBuild / Conan / CMake / Git（`Find-MSBuild` 等，22-100）。
2. 取版本（tag 或 `0.0.0-dev+短commit`），**要求工作树干净**（`AllowDirtySource` 可跳过），117-134。
3. 停掉运行中的 Magpie（`Stop-RunningMagpie`，163-183），避免输出目录被锁。
4. MSBuild Rebuild `Magpie.slnx`，`/p:MajorVersion/.../VersionString/CommitId`、`/p:ReproducibleBuild=true` 等（195-216）。
5. 校验必需运行时路径（`requiredRuntimePaths`）：`Magpie.exe`、`resources.pri`、`Microsoft.UI.Xaml.dll`、`TouchHelper.exe`、`Updater.exe`、`effects`（218-231）。
6. 把 `bin\x64\Release` 全部拷贝进 `release\<版本>\<包名>\`，**剔除 `.pdb` `.lib` `.exp` 和 `Magpie.next.exe`**（244-247）。
7. 清理 `cache`、`logs` 等本地运行残留（250-255）。
8. `-ExcludeTensorRTDepthRuntime` 时删除 `FrameGuidance\TensorRT` 与 `NVIDIA-TensorRT-Runtime-Licenses`（257-267）。
9. 补 `LICENSE-Magpie.txt`、`README-Experimental.txt`、`THIRD-PARTY-NOTICES.md`（269-278）。
10. 生成 `build-manifest.json`：schemaVersion=1、版本、commit、featureOptions（从 `BuildOptions.props.user` 的 `Enable*` 读）、逐文件 path/bytes/sha256/fileVersion（292-321）。
11. 时间戳归一化到源码 commit 时间（326-349）→ 打 zip（条目前缀 `<包名>/`，351-378）。

发布目录实际形态（bin 输出 + 上述补充）≈：`Magpie.exe`、`resources.pri`、`Microsoft.UI.Xaml.dll`、`TouchHelper.exe`、`Updater.exe`、`effects/`（HLSL 源）、`FrameGuidance/DepthAnythingV2/`（model_fp16.onnx + 许可）、`FrameGuidance/TensorRT/`、`FrameGuidance/DirectML/`、`nvngx_dlssnr.dll`、各 NVIDIA-*-LICENSE、`THIRD-PARTY-NOTICES.md`、`build-manifest.json`。

## (c) 第三方依赖

### Conan 2（MSBuildDeps 生成器）

- `src/Magpie/conanfile.txt`：fmt/12.1.0、spdlog/1.17.0（header_only + wchar_filenames + no_exceptions）、parallel-hashmap/2.0.0、rapidjson、kuba-zip、muparser、yas、imgui、rapidhash。
- `src/_ConanDeps/_ConanDeps.vcxproj`：`FindConanFiles` 任务扫描 `src/*/conanfile.txt`（`_ConanDeps.vcxproj:41-54`），为每个生成 `conandeps.props` 到 `obj/.../_ConanDeps/<工程名>/`；`BuildConanDeps` target 在 Build 前执行 `conan install <conanfile> -pr:a=<profile> --lockfile=conan-locks/<工程名>.lock --output-folder ... --build=missing`（60-124）。
- 各工程经 `Common.Post.props:113` 按自己的 `$(MSBuildProjectName)` 导入 conandeps.props（存在才导入）。**Magpie.Core 没有自己的 conanfile，改由 `Magpie.Core.vcxproj:30` 显式导入 `_ConanDeps\Magpie\conandeps.props`**（即复用 Magpie 的 conan 依赖）。

### NuGet（packages.config）

- `Magpie.vcxproj:3-4` 与 `862-877`：`Microsoft.Windows.CppWinRT 3.0.260520.1`、`Microsoft.UI.Xaml 2.8.7`、`Microsoft.Windows.ImplementationLibrary 1.0.260126.7`、`Microsoft.Web.WebView2 1.0.4078.44`。
- `Magpie.Core.vcxproj:3,202-211`：CppWinRT + WIL。

### 手工 SDK 目录 + 特性开关（`src/BuildOptions.props`）

- 特性开关默认全 `false`：`EnableDLSSNR`、`EnableNvidiaOpticalFlow`、`EnableDepthAnythingV2`、`EnableDLSSFrameGeneration`、FSR2/3、XeSS SR/FG、RTXVideoDenoise 等（`BuildOptions.props:18-50`）。本地用 `BuildOptions.props.user` 或命令行覆盖（`BuildOptions.props:60`）。
- SDK 目录：`DLSSSdkDir`、`DLSSNRRuntimeDir`、`NvidiaOpticalFlowSdkDir`、`DepthAnythingModelDir`、`OnnxRuntimeTensorRTDir`、`OnnxRuntimeDirectMLDir`、`DirectMLRuntimeDir`、`TensorRTRuntimeDir`、`FSR2SdkDir/RuntimeDir`、`FSR3SdkDir`、`XeSSSdkDir`、`VFXSdkDir/RuntimeDir/LicenseDir`（`BuildOptions.props:23-50`）。
- 特性宏与 include 目录由 `Common.Post.props:18-37` 按平台 x64 条件下发（如 `MP_ENABLE_DLSSNR` + `$(DLSSSdkDir)\include`）。

### 运行时 DLL 进 bin（`Magpie.vcxproj` AfterTargets=Build 拷贝 target）

- `CopyDLSSNRRuntime`：`$(DLSSNRRuntimeDir)\nvngx_dlssnr.dll` → OutDir（740-742）。
- `CopyFrameGuidanceDepthRuntime`：`model_fp16.onnx` + LICENSE → `FrameGuidance\DepthAnythingV2`；TensorRT 运行时（`nvinfer*_10.dll`、`nvonnxparser_10.dll` 等）→ `FrameGuidance\TensorRT`；ONNX DirectML + DirectML.dll → `FrameGuidance\DirectML`；附 Licenses（743-754）。
- 其余：DLSS SR/FG、FSR2/FSR3、XeSS、RTX Video（732-780）。
- 链接依赖（`Magpie.vcxproj:98-102`）：`$(DLSSSdkDir)\lib\Windows_x86_64\x64\nvsdk_ngx_s.lib`（DLSSSR/FG/NR 任一开启且 x64）、`D3D12.lib`（FG/NR/DAV2）、XeSS libs、`DelayLoadDLLs`（103）。

结论：**依赖管线 = Conan 2 管理 C++ 库 + NuGet 管理 WinRT/WinUI/WIL + 手工下载的 SDK（含 NGX DLSSNR、ONNX Runtime、TensorRT）经 MSBuild 拷贝 target 进 bin**；发布脚本不下载任何东西，只打包 bin。

## (d) 新增 exe 的坑（事实）

### Magpie.Core 是否反向依赖 GUI？

- **否**。Magpie.Core.vcxproj 无任何 ProjectReference；`Magpie.slnx:19-28` 依赖边只有 Magpie→Core（及 Effects/TouchHelper/Updater/_ConanDeps），无反向。Core 的 include 全部在 `Magpie.Core\include` + conan + Windows SDK；pch 只用 `winrt::Windows.*`（`Magpie.Core/pch.h:42-56`）。GUI 层（XAML/`winrt::Magpie::implementation`）只在 src/Magpie 里。

### 链接 Magpie.Core 会带进来的全局状态 / 单例

- `Logger::Get()`：Meyers 单例（`src/Shared/Logger.h:51-54`），spdlog 后端；Magpie.Core 与 Magpie 都用它（如 `DeviceResources.cpp:204`、`Renderer.cpp:855`）。
- `EffectCacheManager::Get()`（`EffectCacheManager.h:9`）、`ImGuiFontsCacheManager::Get()`（`ImGuiFontsCacheManager.h:9`）、`ScalingWindow::Get()`（`ScalingWindow.h:15`）——Magpie.Core 内的单例，随静态库整体链接进入新 exe。
- 静态库整体链接：未使用函数不会被链接器剔除（除非 /OPT:REF + 按 obj 粒度），意味着这些单例的实现与依赖都会被带进新 exe。

### 硬编码路径

- `%LOCALAPPDATA%\Magpie\FrameGuidance\TensorRTCache\<cacheKey>`：`DepthAnythingV2Provider.cpp:589-591`（`LocalAppDataPath()` 读 `%LOCALAPPDATA%` 环境变量，184-193；子串 `L"Magpie"`、`L"FrameGuidance"` 硬编码）。
- `<exe>\FrameGuidance\DepthAnythingV2\model_fp16.onnx`：`DepthAnythingV2Provider.cpp:24`（`MODEL_RELATIVE_PATH`）与 `:592`（`exeDirectory / MODEL_RELATIVE_PATH`）。
- `<exe>\FrameGuidance\TensorRT`、`<exe>\FrameGuidance\DirectML`：`DepthAnythingV2Provider.cpp:577,596,810`。
- 日志 `logs\magpie.log`（相对，随 `SetWorkingDir` 落在 exe 目录）：`CommonSharedConstants.h:20`、`main.cpp:29-32,96,114-116`；`LOGS_DIR=logs`（`CommonSharedConstants.h:19`）。
- 配置 `config\v4\config.json`（`%LOCALAPPDATA%\Magpie\config\v4\`，见 GUI 调查）：`CommonSharedConstants.h:25-26`、`AppSettings.cpp:1385-1417`。

### 日志 / 配置初始化入口与调用时机

- `wWinMain`（`src/Magpie/main.cpp:83-147`）顺序：`SetWorkingDir()`（96）→ `NormalizeArgumentsAndWaitForParent()`（97，Smooth Motion 兼容）→ `InitializeLogger(LOG_PATH)`（114-116，内部 `Logger::Get().Initialize(level, path, 500000, 1)`，73-81）→ `winrt::init_apartment(single_threaded)`（139）→ `App::Get().Initialize(arguments)`（141-144）→ `App::Run()`（146）。
- `App::Initialize`（`App.cpp:114-213`）：`AppSettings::Get().Initialize()`（148-152，失败即退出）、各 Service `Initialize`（175-186）。
- 新 exe 需自行复刻的初始化骨架：SetWorkingDir、Logger init、`init_apartment`、AppSettings（或自己的配置）init、XAML Islands 的 `WindowsXamlManager::InitializeForCurrentThread`（`App.cpp:131`）。

### 其余工程配置障碍

- NGX/D3D12 的 **Link 项只在 Magpie.vcxproj**（`Magpie.vcxproj:98-102`）——新 exe 链接 Magpie.Core 时不会自动得到 `nvsdk_ngx_s.lib`/`D3D12.lib`，必须复制这些条件 Link 依赖。
- NGX/ONNX 运行时 **拷贝 target 只在 Magpie.vcxproj**（`Magpie.vcxproj:732-780`）——新 exe 须自行复制 `CopyDLSSNRRuntime`/`CopyFrameGuidanceDepthRuntime` 等价物，否则发布目录缺 `nvngx_dlssnr.dll` 与 `FrameGuidance/*`。
- 新 exe 若不自带 conanfile.txt，`_ConanDeps` 不会为它生成 conandeps.props，`Common.Post.props:113` 的导入不成立 → 拿不到 fmt/spdlog 等；需像 Magpie.Core 那样显式导入 Magpie 的 conandeps.props，或自己加 conanfile.txt。
- `effects/` 目录必须随新 exe 发布（EffectsService 运行时解析 HLSL，`EffectsService.cpp`；`EFFECTS_DIR=effects`，`CommonSharedConstants.h:28`）。
- `resources.pri` + `Microsoft.UI.Xaml.dll` 是 WinUI 2.8 XAML Islands 运行必需，目前由 Magpie.vcxproj 的 Appx/PRI 管线与 NuGet 产出——新 exe（若复用 XAML 页面）需要同样的 PRI/`Microsoft.UI.Xaml.dll` 布局，`Build-Release.ps1:218-231` 的必需文件清单与拷贝逻辑需为第二 exe 扩展（与 #5「第二 exe 槽位」一致）。
- `Build-Release.ps1` 目前强假设单一主 exe 布局（`requiredRuntimePaths`、`Copy-Item bin`、manifest `files`），多 exe 发布需改脚本（事实，#5 已记录同结论）。

---

## 关键文件索引

- `Magpie.slnx`、`src/Common.Pre.props`、`src/Common.Post.props`、`src/BuildOptions.props`、`src/HybridCRT.props`、`Directory.Build.props`
- `src/_ConanDeps/_ConanDeps.vcxproj`、`src/Magpie/conanfile.txt`
- `src/Magpie.Core/Magpie.Core.vcxproj`、`src/Magpie/Magpie.vcxproj`、`src/Effects/Effects.vcxproj`
- `scripts/Build-Release.ps1`
- `src/Shared/Logger.h`、`src/Shared/CommonSharedConstants.h`
- `src/Magpie/main.cpp`、`src/Magpie/App.cpp`、`src/Magpie.Core/DepthAnythingV2Provider.cpp`
