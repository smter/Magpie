# GUI 复用可行性调研：WinUI 页面复用 与 配置 schema（Magpie → 独立视频处理应用）

> 目标：评估把 Magpie 主程序 GUI（src/Magpie，C++/WinRT + WinUI）及其配置 schema 复用到"与 magpie.exe 平级的独立离线视频处理应用"（GUI 类似 magpie.exe，v1 只实现 DLSSNR 一个缩放模式）。
> 本文件只做事实调研；不修改任何源码。所有论断均标注来源（仓库路径+行号，或 URL）。

## TL;DR

- 仓库实际 UI 栈是 **WinUI 2.8（Microsoft.UI.Xaml 2.8.7 NuGet）+ XAML Islands（DesktopWindowXamlSource）嵌在纯 Win32 窗口**里，**不是 WinUI 3 / Windows App SDK**（无 MSIX、无 WindowsAppSDK 依赖）。这对"复用页面"结论影响重大（见末尾判定 1）。
- 缩放模式与效果参数**不是 UI 硬编码**：参数元数据（名字/标签/默认/最小/最大/步长/类型）在运行时从 HLSL 源文件的 `//!PARAMETER` 注解解析而来；配置里存的只是 `{参数名: float}` 映射。因此参数面板可整体照搬。
- config.json schema 是版本化 JSON（`CONFIG_VERSION=4`），`scalingModes`/`effects`/`parameters` 结构与 DLSSNR 迁移（`experimentalDlssnrSettingsVersion`）都集中在 `AppSettings.cpp`，可整体搬到新应用，只需裁剪 Magpie 专属字段。
- "模式列表只含 DLSSNR"：缩放模式机制本身对数量无任何假设，列表内容完全由 `_scalingModes` 数组决定；新应用只需提供只含 DLSSNR 的默认配置（或替换 `_SetDefaultScalingModes`），无需改机制代码。
- 配置目录 `%LOCALAPPDATA%\Magpie\config\v4\config.json` 的拼装只集中在 `AppSettings::_UpdateConfigPath` / `FindOldConfig` 两处格式化字符串，改成 `%LOCALAPPDATA%\MagpieVideo` 牵连很小；但 **Magpie.Core 的 DLSSNR 深度模型路径硬编码了 `%LOCALAPPDATA%\Magpie\FrameGuidance`**（DepthAnythingV2Provider），复用它时需另行处理。
- 必须新写：处理进度 UI 与导出（编码）流程。文件选择对话框的底层封装（`FileDialogHelper`，IFileDialog）已存在可复用/扩展。

---

## (a) 页面 / 导航结构（可复用性）

### 顶层结构

- **入口**：`src/Magpie/main.cpp` — `wWinMain`（83 行），纯 Win32 消息循环（`App::Run`，App.cpp:215-233）。没有 WinUI 3 的 `Application` 生命周期。
- **App**：`src/Magpie/App.h:16` — `class App : public App_base<App, Markup::IXamlMetadataProvider>`。这是 WinUI 2/UWP XAML 的 metadata-provider 模式，不是 WinUI 3 的 `Microsoft.UI.Xaml.Application`。
  - `App.cpp:131` — `Hosting::WindowsXamlManager::InitializeForCurrentThread()`（XAML Islands 初始化）。
- **主窗口**：`src/Magpie/MainWindow.cpp` / `.h` — 继承 `XamlWindowT<MainWindow, RootPage>`（MainWindow.h:7），XAML Islands 托管在 `XamlWindow.h`：
  - `XamlWindow.h:61-65` — `DesktopWindowXamlSource` + `IDesktopWindowXamlSourceNative2::AttachToWindow`，`_xamlSource.Content(*content)`（内容是 RootPage）。
  - 大量 Win32 边框/标题栏/DPI 处理（MainWindow.cpp:166-665）。这些**与视频应用无关**，复用价值低。
- **导航骨架**：`src/Magpie/RootPage.xaml` + `.cpp`
  - `RootPage.xaml:192` — `NavigationView`（RootNavigationView）；`RootPage.xaml:347` — `<Frame x:Name="ContentFrame" />` 承载子页面。
  - 菜单项：Home（208 行）、ScalingModes（215 行）、Profiles 头（226 行）、Defaults（227 行）、NewProfile（232 行）、Footer About（338 行）。
  - `RootPage.cpp:137-176` — `NavigationView_SelectionChanged`：按 Tag 导航 `Home→HomePage`、`ScalingModes→ScalingModesPage`、`About→AboutPage`（156-164 行），`IsSettingsSelected→SettingsPage`（143-144 行），其余（profile 项）→ `ProfilePage`，参数为 `index - 4`（FIRST_PROFILE_ITEM_IDX=4，RootPage.cpp:38,172）。
  - 结论：**导航结构 = NavigationView + Frame + Tag 路由**，是标准 WinUI 模式；视频应用可整体沿用 RootPage 骨架，只改菜单项与路由表。
- **页面外壳**：`src/Magpie/PageFrame.xaml` — 复用价值最高的通用控件：
  - `PageFrame.xaml:23-83` — Header（Icon/Title/HeaderAction），`PageFrame.xaml:87-102` — ScrollViewer + `PageMaxWidth=1000` 的 MainContent 区。
  - 每个页面都用它做根（如 `ScalingModesPage.xaml:56`、`ProfilePage.xaml:9`、`HomePage.xaml:9`）。**视频应用可直接复用 PageFrame 做页面外壳**。

### 各页面职责与复用价值

| 页面 | 职责 | 复用判断 |
|---|---|---|
| `RootPage` | NavigationView + 路由 + profile 导航项 | 骨架可复用（改菜单/路由） |
| `HomePage` + `HomeViewModel` | 缩放热键、倒计时、通知图标、更新卡片（HomePage.xaml:15-60） | 与视频应用无关，不复用 |
| `ScalingModesPage` + `ScalingModesViewModel` | **缩放模式列表**：新建/重命名/复制/删除/拖拽排序/导入导出/每个模式的 effect 列表与参数面板 | **核心复用目标**（见 (b)） |
| `ProfilePage` + `ProfileViewModel` | 单个 profile（窗口匹配规则、捕获方式、缩放模式下拉、性能项…） | 视频应用无"profile/窗口规则"概念，仅"缩放模式下拉"逻辑可参考（ProfileViewModel.cpp:326-347） |
| `SettingsPage` + `SettingsViewModel` | 主题/语言/便携模式/打开配置目录等 | 少量可复用（主题/语言），大部分不适用 |
| `AboutPage` | 关于页 | 可复用 |
| 通用控件 | `SettingsCard`/`SettingsExpander`/`SettingsGroup`/`SimpleStackPanel`/`WrapPanel`/`TextBlockHelper` 等（src/Magpie/SettingsCard.* 等） | 高度可复用，与领域无关 |

---

## (b) 缩放模式与效果参数：定义与渲染

### 数据结构

- `src/Magpie/ScalingMode.h:6-23`
  - `EffectItem { std::wstring name; phmap::flat_hash_map<std::wstring,float> parameters; ScalingType scalingType; std::pair<float,float> scale; }`
  - `ScalingMode { std::wstring name; std::vector<EffectItem> effects; }`
- `EffectItem::operator EffectOption()`（ScalingMode.cpp:7-20）把 UI 结构转成 Core 结构 `EffectOption`（Magpie.Core/include/ScalingOptions.h:66-76，`parameters` 是 `flat_hash_map<std::string,float>`）。

### 参数元数据从哪来：**从 HLSL 注解解析，不是硬编码**

- `src/Magpie/EffectsService.cpp:50-101` — `EffectsService::Initialize()` 后台并行扫描 `effects\` 目录下所有 `.hlsl`，对每个 effect 调 `EffectCompiler::Compile(effectDesc, EffectCompilerFlags::NoCompile)`（68 行）解析元数据。
- `EffectCompilerFlags::NoCompile`（Magpie.Core/include/EffectCompiler.h:12）— "只解析输出尺寸和参数，供用户界面使用"。
- 解析结果 `EffectInfo`：`EffectsService.h:12-24` — `params` 是 `EffectParameterDesc` 向量。
- `EffectParameterDesc`（Magpie.Core/include/EffectDesc.h:63-67）— `{ name; label; std::variant<EffectConstant<float>,EffectConstant<int>> constant; }`，`EffectConstant{defaultValue,minValue,maxValue,step}`（55-61 行）。
- 注解来源示例 `src/Effects/DLSSNR/DLSSNR_AI_Filter.hlsl:8-110`：
  ```
  //!PARAMETER
  //!LABEL NR Preset (0 Default, ...)
  //!DEFAULT 0 / //!MIN 0 / //!MAX 3 / //!STEP 1
  int nrPreset;
  ```
  DLSSNR 共 11 个参数：enableInputResolutionScaling, inputResolutionPercent, residualMultiplier, nrPreset, style, intensity, localToneStrength, localStructureStrength, skinStructureStrength, useAutoMask, uiCorrection, guidanceMode, depthInferenceInterval（注意实际是 13 个）。

### 参数面板如何生成

- `src/Magpie/EffectParametersViewModel.cpp:56-146` — 对每个 `_effectInfo->params`：
  - `constant` 是 `EffectConstant<int>` 且 min=0/max=1/step=1 → 生成 `ScalingModeParameter` 的**布尔项**（CheckBox，97-108 行）。
  - 其它 → **浮点/整数滑动条项**（Slider + label + 值文本，79-94 / 110-124 行）。
  - 值来源：`scalingMode.effects[effectIdx].parameters` 里按参数名查找（71-77 行），找不到用 `constant.defaultValue`。
  - 修改回写 `_Data()[paramName] = value`（154-179 行）→ 触发 `LazySaveAppSettings`（防抖 1s，23-54 行）。
- 面板 XAML：`src/Magpie/ScalingModesPage.xaml:10-54` — `EffectParametersFlyout` DataTemplate（ItemsControl + CheckBox/Slider），由 `ScalingModeEffectItem` 暴露 `Parameters`/`HasParameters`（ScalingModeEffectItem.cpp:78-84）。
- `ScalingModeEffectItem.cpp:24-44` — 每个 effect 一行，`_effectInfo = EffectsService::Get().GetEffect(data.name)`；未知 effect 显示 "Unknown Effect (name)"。

### DLSSNR 参数名与运行时设置映射（验证配置键名）

- `src/Magpie.Core/NativeEffectBackendFactory.cpp:65-108` — 对 effect `DLSSNR\DLSSNR_AI_Filter`，从 `option.parameters`（`flat_hash_map<std::string,float>`）逐个读 `enableInputResolutionScaling`/`inputResolutionPercent`/`residualMultiplier`/`nrPreset`/`style`/`intensity`/`localToneStrength`/`localStructureStrength`/`skinStructureStrength`/`useAutoMask`/`uiCorrection`/`guidanceMode`/`depthInferenceInterval`，clamp 后填 `DLSSNRSettings`。
- `DLSSNRSettings`：`src/Magpie.Core/DLSSNRFilter.h:9-24`。
- 关键：**配置里存的参数键名 = HLSL 注解参数名 = 运行时 DLSSNRSettings 读取名**，三者一致，schema 直接复用。

### 预设文件里 DLSSNR 模式的形态

`presets/ScalingModes-v0.5.7-experimental.json:36-61`：
```json
{
  "name": "DLSSNR",
  "effects": [
    { "name": "FrameRate_Filter", "parameters": { "targetFrameRate": 60 } },
    {
      "name": "DLSSNR\\DLSSNR_AI_Filter",
      "parameters": {
        "nrPreset": 0, "style": 0, "intensity": 1, "localToneStrength": 1,
        "localStructureStrength": 1, "skinStructureStrength": -1,
        "useAutoMask": 0, "uiCorrection": 0, "guidanceMode": 0,
        "depthInferenceInterval": 4
      }
    }
  ]
}
```
注意：`enableInputResolutionScaling` 未列出（缺省为 default），说明**参数允许部分缺省**（Import 用 default 补）。该文件仅用于 Release 附带的"追加式导入"，不是程序启动时自动加载（README.md:39 说明由用户在缩放模式页手动导入）。

---

## (c) config.json schema 与迁移逻辑

### 版本与路径

- `AppSettings.cpp:27` — `static constexpr uint32_t CONFIG_VERSION = 4;`
- `AppSettings.cpp:28-29` — `EXPERIMENTAL_DLSSNR_SETTINGS_VERSION = 2;`、`EXPERIMENTAL_DLSS_SR_SETTINGS_VERSION = 1;`
- 路径拼装：`AppSettings.cpp:1411-1413` — `_configDir = fmt::format(L"{}\\Magpie\\{}\\v{}\\", localAppDataDir.get(), CommonSharedConstants::CONFIG_DIR, CONFIG_VERSION);`，`_configPath = _configDir / CONFIG_FILENAME;`
  - `CommonSharedConstants.h:25-26` — `CONFIG_DIR = L"config"`、`CONFIG_FILENAME = L"config.json"` → 即 `%LOCALAPPDATA%\Magpie\config\v4\config.json`。

### 根级 schema（AppSettings.cpp:557-697 `_Save`）

写入键：`language`、`theme`、`windowPos`、`shortcuts`、`countdownSeconds`、一批 bool（developerMode、disableTopmost、…）、`experimentalDlssnrSettingsVersion`（646 行）、`experimentalDlssSrSettingsVersion`（648 行）、`scalingModes`（651 行，委托 `ScalingModesService::Export`）、`profiles`（653-659 行）、`overlay`（661-685 行）。

`scalingModes` 序列化：`ScalingModesService.cpp:143-152`（Export）/ `102-141`（WriteScalingMode）：
- 每个 mode：`{ "name", "effects": [ { "name", "scalingType"?, "scale"?: {x,y}, "parameters"?: {...float} } ] }`；`HasScale()` 为 false 时不写 scalingType/scale，parameters 为空时不写。

反序列化：`ScalingModesService.cpp:154-312`（LoadScalingMode / Import）— 对配置加载（loadingSettings=true）容错：无效项跳过不失败（177-181、187-194、235-241、277-283 行）；`Import` 是**追加**语义（298-303 行 `settings.insert(...end())`），不是覆盖。

### DLSSNR 版本化迁移（AppSettings.cpp `_LoadSettings` 700-1020 行）

- 读取版本键：`AppSettings.cpp:701-706`。
- **DLSSNR v0→v1**（863-888 行）：对每个 `scalingMode.effects` 中 `effect.name == L"DLSSNR\\DLSSNR_AI_Filter"` 且 `guidanceMode==1.0`（Force Zero）的项改写为 `0.0`（Available）；只跑一次。
- **DLSSNR v1→v2**（890-906 行）：删除遗留参数键 `preset`（v0.5.7 起 nrPreset 从 Default=0 开始）。
- 版本推进：`_experimentalDlssnrSettingsVersion < EXPERIMENTAL_DLSSNR_SETTINGS_VERSION` 时置为当前值并 `_isConfigMigrationNeeded = true`（908-911 行）。
- **DLSS SR 迁移**（913-929 行）：`DLSS\DLSS_ZeroMV` → `DLSS\DLSS_SR` 重命名，独立版本键。
- 迁移后触发保存：`AppSettings.cpp:283-287`（Initialize 里 `_isConfigMigrationNeeded` → `SaveAsync`）。
- 默认缩放模式：`_SetDefaultScalingModes()`（AppSettings.cpp:1280-1342）在**配置不存在/为空**时写入 6 个模式（Lanczos/FSR/RTXVideo VSR Ultra/DLSSFG/XeSSFG/DLSSNR），`_defaultProfile.scalingMode = 0`（1341 行）。`ResetScalingModes()`（1344-1352 行）恢复同一组默认。

### 能否整体搬到新应用

- `AppSettings` 是单例（`AppSettings.h:88-91`），`_AppSettingsData`（AppSettings.h:18-84）承载全部字段。视频应用只需子集：`_scalingModes`、`_experimentalDlssnrSettingsVersion`（+ 主题/语言/窗口位置可选）。
- `_LoadSettings`/`_Save`/`ScalingModesService::Export/Import`/`JsonHelper` 都是自包含逻辑，可整体复制。**唯一强耦合点**：迁移逻辑与 Magpie 专属字段（profiles、overlay、shortcuts 等）混在同一函数里，需裁剪；DLSSNR 迁移本身只依赖 `_scalingModes` 数组，可抽成独立函数。
- 迁移以 `_scalingModes` 为遍历对象，**与模式数量无关**：只含 DLSSNR 一个模式时同样工作。

---

## (d) C++/WinRT 工程组织：Magpie.Core 引用、打包、依赖

### 工程拓扑（Magpie.slnx）

- `Magpie.slnx:19-36` — 项目：`src/Effects/Effects.vcxproj`、`src/Magpie.Core/Magpie.Core.vcxproj`、`src/Magpie/Magpie.vcxproj`（DefaultStartup）、`src/Shared/Shared.vcxitems`、`src/TouchHelper`、`src/Updater`、`src/_ConanDeps`。
- **Magpie.Core 是静态库**：`src/Magpie.Core/Magpie.Core.vcxproj:20` — `<ConfigurationType>StaticLibrary</ConfigurationType>`。
- **Magpie 以 ProjectReference 引用 Magpie.Core**：`src/Magpie/Magpie.vcxproj:728-731`：
  ```xml
  <ProjectReference Include="..\Magpie.Core\Magpie.Core.vcxproj">
    <Project>{0e5205ae-dfa9-4cb8-b662-e43cd6512e2a}</Project>
  </ProjectReference>
  ```
- 头文件路径：`Magpie.vcxproj:84` — `<AdditionalIncludeDirectories>..\Magpie.Core\include;...</AdditionalIncludeDirectories>`（`EffectCompiler.h`/`EffectDesc.h`/`ScalingOptions.h`/`Win32Helper.h` 等对外头在 `Magpie.Core/include/`）。
- Effects（HLSL）是独立项目 `src/Effects/Effects.vcxproj`；Magpie 对它有 BuildDependency（Magpie.slnx:24），`effects\` 目录部署到输出目录。

### 打包 / 部署

- **不是 .appx / MSIX**。`src/BuildOptions.props:10` — `<IsPackaged>false</IsPackaged>`。
- `AppContainerApplication=false`（Magpie.vcxproj:16）→ 编译为普通 exe；`ApplicationType=Windows Store` + 手动 `Microsoft.AppXPackage.Targets` 只为生成 resources.pri（Magpie.vcxproj:782-809）。
- WinUI 2.8 的 `Microsoft.UI.Xaml.dll` + `resources.pri` 在构建时从 NuGet 包的 appx 里解包提取（`src/WinUI.targets:41-83` ExtractWinUIRuntime），复制到输出目录。
- 发布脚本 `scripts/Build-Release.ps1:218-225` 要求运行时布局：`Magpie.exe, resources.pri, Microsoft.UI.Xaml.dll, TouchHelper.exe, Updater.exe, effects` → 打成**普通 zip 目录包**（非 MSIX）。
- 依赖：NuGet `packages.config`（src/Magpie/packages.config:3-6）— `Microsoft.UI.Xaml 2.8.7`、`Microsoft.Web.WebView2`、`Microsoft.Windows.CppWinRT 3.0.260520.1`、`Microsoft.Windows.ImplementationLibrary`；原生依赖走 Conan（`src/_ConanDeps/_ConanDeps.vcxproj`、各 `conanfile.txt`）。
- **结论：整体是"普通 exe + 同目录 dll/effects + resources.pri"的非打包应用，无 Windows App SDK 运行时依赖。**

---

## (e) 必须新写的部分

- **文件选择**：底层对话框封装**已存在** — `src/Magpie/FileDialogHelper.h/.cpp`（`OpenFileDialog(IFileDialog*, options)`，基于 IFileDialog/IShellItem，`FileDialogHelper.cpp:14-42`）。缩放模式导入/导出已用它（`ScalingModesViewModel.cpp:36-144`，IFileSaveDialog/IFileOpenDialog + `FOS_STRICTFILETYPES` + json 过滤器）。视频文件选择可复用该 helper，换扩展名过滤器即可；**仍属小改**而非重写。
- **处理进度 UI**：Magpie 是实时窗口缩放，无批处理/进度概念 → **全新**（ProgressBar/取消/帧计数/剩余时间）。
- **导出流程（路径选择 + 编码设置）**：Magpie 无文件输出管线（只写截图 `ScreenshotHelper`，且是内部分镜）→ **全新**（另需视频解码/编码管线，这超出 GUI 范畴但必须新写）。

---

## (f) 配置隔离：`%LOCALAPPDATA%\Magpie` → `%LOCALAPPDATA%\MagpieVideo` 的牵连

- 配置目录拼装的**全部位置**：
  - `AppSettings.cpp:1411-1412` — 非便携模式的 `Magpie\config\v4\`（唯一"Magpie"字面量所在）。
  - `AppSettings.cpp:1354-1383` `FindOldConfig` — 旧版 `Magpie\config\v3..v2` 与 `v1`（`L"\\Magpie\\{}\\v{}"`）。
  - `AppSettings.cpp:1386-1401` — 便携模式（配置文件在 exe 旁，不涉及 LOCALAPPDATA）。
  - 其余代码只通过 `AppSettings::ConfigDir()`（AppSettings.h:101-103）/ `_configPath` 访问（SettingsViewModel.cpp:121-124 用于"打开配置目录"）。
- **结论：改动仅集中在 `AppSettings::_UpdateConfigPath` + `FindOldConfig` 的若干格式化字符串**；把 `Magpie` 换成 `MagpieVideo`（同时可把 `CONFIG_DIR`/`CONFIG_VERSION` 常量化）即可，牵连很小。
- ⚠️ **注意（Magpie.Core 内另一处硬编码）**：`src/Magpie.Core/DepthAnythingV2Provider.cpp:184-187,590` — DLSSNR 的深度推理模型缓存路径硬编码为 `%LOCALAPPDATA%\Magpie\FrameGuidance\...`（`LocalAppDataPath() / L"Magpie" / L"FrameGuidance"`）。若新应用复用 DLSSNR 的 depth guidance，这处不会随 config 目录移动，需要单独改（或可接受与 Magpie 共享）。
- 其它运行态目录（logs/cache/effects/sources）相对 exe 工作目录（`CommonSharedConstants.h:19-30`；main.cpp:29-32 `SetWorkingDir` 设为 exe 目录），不受 LOCALAPPDATA 影响。

---

## 外部事实：WinUI 2 / XAML Islands / WinUI 3（Windows App SDK）

- **WinUI 2** = UWP XAML 控件库，通过 NuGet `Microsoft.UI.Xaml`（2.x）分发，可配合 XAML Islands（`DesktopWindowXamlSource`）在桌面 Win32 应用中承载。仓库用的是 2.8.7（packages.config:3）。WinUI 2.8 release notes: https://learn.microsoft.com/windows/apps/winui/winui2/release-notes/winui-2.8
- **XAML Islands**：用 `WindowsXamlManager` + `DesktopWindowXamlSource` 在既有桌面应用里托管 XAML 内容（对应本仓库 `App.cpp:131`、`XamlWindow.h:61-65`）。文档: https://learn.microsoft.com/windows/apps/desktop/modernize/host-controls-existing-desktop-apps
- **WinUI 3** = Windows App SDK 的一部分，独立于 UWP，用 `Microsoft.UI.Xaml.Application`/`Window`，可打包或非打包（非打包需 Windows App Runtime 或自包含）；命名空间/托管方式与本仓库不同。部署选项: https://learn.microsoft.com/windows/apps/package-and-deploy/deploy-overview
- 本仓库**没有**引入 Windows App SDK（packages.config 无 Microsoft.WindowsAppSDK；无 MSIX；`XamlWindow` 用的是 `Windows::UI::Xaml::Hosting` 而非 `Microsoft.UI.Xaml.Hosting`）。

---

## 结论对推荐的影响

1. **新应用用 WinUI 3 / C++/WinRT，复用 Magpie 页面与控件（尤其 ScalingModesPage 与参数面板）→ ADJUSTED**
   证据：仓库实际是 **WinUI 2.8（Microsoft.UI.Xaml 2.8.7）+ XAML Islands（`DesktopWindowXamlSource`，XamlWindow.h:61-65）嵌 Win32 窗口**，非 WinUI 3/Windows App SDK（packages.config:3；无 WindowsAppSDK 依赖）。页面/控件在**同一栈**下可高度复用（ScalingModesPage、参数面板、PageFrame、SettingsCard 族），但若坚持 WinUI 3 需整体移植 XAML（命名空间、Application/Window 模型、托管方式）——应要么跟随 Magpie 用 WinUI 2.8 + XAML Islands，要么把 WinUI 3 移植单独评估。

2. **复用 config.json schema（scalingModes/effects + DLSSNR 参数）与迁移逻辑 → CONFIRMED**
   证据：schema 是版本化 JSON，`scalingModes`/`effects`/`parameters`（float map）结构与 DLSSNR 迁移（`experimentalDlssnrSettingsVersion`，AppSettings.cpp:863-911）自包含且以 `_scalingModes` 为遍历对象；参数键名与 `DLSSNRSettings` 读取名一致（NativeEffectBackendFactory.cpp:65-108）；`ScalingModesService::Export/Import` 可整体复用（ScalingModesService.cpp:102-312）。

3. **"模式列表只含 DLSSNR"通过默认配置只有 DLSSNR 模式实现（无需改模式机制代码）→ CONFIRMED**
   证据：机制对模式数量无任何假设，列表 = `_scalingModes` 数组（ScalingMode.h:20-23；ProfileViewModel.cpp:326-336 只是遍历名字）；`profile.scalingMode` 越界回退 -1（AppSettings.cpp:1087）。只需把默认配置/`_SetDefaultScalingModes` 换成只含 DLSSNR（AppSettings.cpp:1280-1342）即可，无需改模式机制。

4. **配置隔离用独立目录（如 %LOCALAPPDATA%\MagpieVideo）且牵连可控 → CONFIRMED**
   证据：LOCALAPPDATA 配置路径拼装只集中在 `AppSettings::_UpdateConfigPath`（AppSettings.cpp:1411-1412）与 `FindOldConfig`（1354-1383）几处格式化字符串，其余经 `ConfigDir()` 访问；改动面小。注意 Magpie.Core 另有硬编码 `%LOCALAPPDATA%\Magpie\FrameGuidance`（DepthAnythingV2Provider.cpp:590），复用到 DLSSNR 深度引导时需单独处理。

5. **文件选择/进度/导出流程为新写部分 → ADJUSTED**
   证据：文件打开/保存对话框封装已存在（`FileDialogHelper`，FileDialogHelper.cpp:14-42，已被缩放模式导入/导出使用），可复用/扩展；真正全新的是**处理进度 UI** 与**导出（编码）流程**——Magpie 是实时窗口缩放，无批处理与文件输出管线。
