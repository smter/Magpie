# GUI 与配置层调查：XAML Islands 页面 / 参数面板 / config schema（Magpie → MagpieVideo）

> 目标：为「复用边界与整体架构」(#6) 的 (d)(e) 决策及「新应用 GUI 原型」(#7) 收集事实——GUI 工程结构、DLSSNR 参数面板、配置系统、FileDialogHelper、复用耦合点、进度 UI 先例。
> 本文件只做事实调研，不做设计建议。所有论断标注来源（仓库路径 + 行号）。与已关闭的 #4「GUI 复用可行性」调研（research/gui-reuse）互补，本文件加深页面清单、参数面板、FileDialogHelper、耦合点、进度 UI 细节。
> 调查对象：current checkout（experimental 分支）。

## TL;DR

- **宿主**：纯 Win32 窗口 + XAML Islands（WinUI 2.8）。入口 `src/Magpie/main.cpp` `wWinMain`（83-147）→ `winrt::init_apartment(STA)`（139）→ `App::Get().Initialize`（`App.cpp:114`）：`WindowsXamlManager::InitializeForCurrentThread`（131）+ `MainWindow`（`XamlWindowT<MainWindow, RootPage>`）+ `DesktopWindowXamlSource`（`XamlWindow.h:61-65`）。`App::Run` 是裸 `GetMessage` 循环（`App.cpp:215-233`）。
- **导航**：`RootPage.xaml` `NavigationView` + `Frame`，Tag 路由：Home / ScalingModes / About（+动态 Profiles/Defaults/NewProfile）（`RootPage.xaml:208-344`；路由 `RootPage.cpp:137-176`）。页面外壳 `PageFrame.xaml`（Header + ScrollViewer，PageMaxWidth=1000）可整体复用。
- **DLSSNR 参数面板不是硬编码**：参数元数据（LABEL/DEFAULT/MIN/MAX/STEP）在运行时从 HLSL `//!PARAMETER` 注解解析（DLSSNR 参数全在 `src/Effects/DLSSNR/DLSSNR_AI_Filter.hlsl:8-110`：enableInputResolutionScaling / inputResolutionPercent / residualMultiplier / nrPreset / style / intensity / localToneStrength / localStructureStrength / skinStructureStrength / useAutoMask / uiCorrection / guidanceMode / depthInferenceInterval）。编辑 UI 在 `ScalingModesPage.xaml:9-55` 的 `EffectParametersFlyout` DataTemplate（`EffectParametersViewModel` + `ScalingModeParameter`），通用（CheckBox 布尔 / Label+Slider 浮点，TwoWay）——**任何缩放模式共用同一面板**，DLSSNR 只是喂给它解析出的参数。配置里只存 `{参数名: float}`。
- **配置系统**：`AppSettings`（`src/Magpie/AppSettings.{h,cpp}`）单例 `AppSettings::Get()`。`CONFIG_VERSION=4`（`AppSettings.cpp:27`）；路径 = `%LOCALAPPDATA%\Magpie\config\v4\config.json`（`_UpdateConfigPath`，`AppSettings.cpp:1385-1417`；`CONFIG_DIR="config"`、`CONFIG_FILENAME="config.json"`，`CommonSharedConstants.h:25-26`）；迁移循环 v3→v2（`AppSettings.cpp:1355-1375`）。**全局单例，非注入**。
- **FileDialogHelper**：`src/Magpie/FileDialogHelper.h`——`static OpenFileDialog(IFileDialog*, FILEOPENDIALOGOPTIONS)` → `optional<filesystem::path>`，薄封装 IFileDialog，可复用。
- **复用耦合点（新 exe 复用页面/ViewModel 必须切断/替代）**：① 全部服务是 `Xxx::Get()` 全局单例，页面直接调用（AppSettings / EffectsService / ScalingService / AdaptersService / ShortcutService / UpdateService / ToastService / LocalizationService / NotifyIconService）；② `App::Get()` 单例 + `App.xaml` 资源 + 自定义控件 DependencyProperty 注册（`App.cpp:161-167`）；③ `WindowsXamlManager`/DispatcherQueue 初始化；④ EffectsService 依赖 exe 旁的 `effects/` 目录并直接调 Magpie.Core；⑤ 本地化 `APP_RESOURCE_MAP_ID="Magpie/Resources"`（`CommonSharedConstants.h:43`）；⑥ 硬编码路径：配置 `%LOCALAPPDATA%\Magpie\config\v4`、日志 `logs\`（随 CWD=exe 目录）、Magpie.Core 的 `%LOCALAPPDATA%\Magpie\FrameGuidance`；⑦ `MainWindow/XamlWindowT` 是重度 Win32 无边框窗口实现（`XamlWindow.h`），复用价值低；⑧ 单实例 mutex/提权（`App.cpp:332-377`）；⑨ Profile/缩放模式概念绑 ScalingService，离线应用大多不适用。
- **进度 UI 先例**：只有 HomePage 的定时 ProgressRing（30px，`HomePage.xaml:70-77`，`HomeViewModel.cpp:59` TimerProgressRingValue）与 AboutPage 的更新检查 ProgressRing/ProgressBar（`AboutPage.xaml:44,70-79`）。**没有长任务进度条/处理中 UI 先例**——离线导出的进度 UI 是新写的（与 #4/#7 一致）。

---

## 1. GUI 工程结构

### 宿主搭建

- `src/Magpie/main.cpp:83-147`：`wWinMain` → `SetWorkingDir()` → `InitializeLogger` → `winrt::init_apartment(single_threaded)`（139）→ `App::Get().Initialize(arguments)`（141）→ `App::Run()`（146）。
- `App`（`App.cpp`）：全局单例 `App::Get()`（94-101）；`App::Initialize`（114-213）：`IncreaseTimerResolution`、`InitMessages`、单实例检查（`_CheckSingleInstance`，332-377）、建 `MainWindow`（126）、`EffectsService::Get().Initialize()`（128）、`Hosting::WindowsXamlManager::InitializeForCurrentThread()`（131，XAML Islands 初始化）、`DispatcherQueue`（135）、`LocalizationService::Get().EarlyInitialize`（146）、`AppSettings::Get().Initialize()`（148-152）、自定义控件 DependencyProperty 注册（161-167）、Theme/本地化/Toast/Adapters/Shortcut/Scaling/Update/ThemeHelper/NotifyIcon 各 Service Initialize（173-186）。
- `MainWindow` = `XamlWindowT<MainWindow, RootPage>`（`MainWindow.h:7`）；XAML Islands 装配在 `XamlWindow.h:54-77`：`DesktopWindowXamlSource` + `IDesktopWindowXamlSourceNative2::AttachToWindow(this->Handle())` + `_xamlSource.Content(RootPage)`。大量 Win32 无边框/DPI/边框处理在 `XamlWindow.h:120-523`（复用价值低）。
- `App::Run`（215-233）：裸 `GetMessage` 消息循环，不是 XAML 的 Application 生命周期。

### 导航组织

- `RootPage.xaml`：`muxc:NavigationView`（RootNavigationView）+ `<Frame x:Name="ContentFrame">`。菜单：Home（`Tag="Home"`，208-214）、ScalingModes（`Tag="ScalingModes"`，215-225）、Profiles Header + Defaults + NewProfile（226-232，动态）、About（`Tag="About"`，339-344）。
- 路由：`RootPage.cpp:137-176` `NavigationView_SelectionChanged` 按 Tag 导航：Home→HomePage、ScalingModes→ScalingModesPage、About→AboutPage、IsSettingsSelected→SettingsPage、其余（profile）→ProfilePage（参数 index-4，FIRST_PROFILE_ITEM_IDX=4，`RootPage.cpp:38`）。
- 页面外壳 `PageFrame.xaml`：Header（Icon/Title/HeaderAction）+ ScrollViewer + `MainContent`（`PageMaxWidth=1000`），各页面以它为根（`ScalingModesPage.xaml:56` 等）——通用外壳，可复用。

### 页面清单（`src/Magpie/`，来自 `Magpie.vcxproj` Page/ClInclude）

- 页面：`RootPage`、`HomePage`、`ScalingModesPage`、`ProfilePage`、`SettingsPage`、`AboutPage`、`ToastPage`（`Magpie.vcxproj:626-681`）。
- 控件/样式：`PageFrame`、`TitleBarControl`、`CaptionButtonsControl`、`ShortcutControl`、`ShortcutDialog`、`SettingsCard`、`SettingsExpander`、`SettingsGroup`、`BlueInfoBarStyle`、`KeyVisual`、`WrapPanel`、`SimpleStackPanel`（`Magpie.vcxproj:110-296`）。
- ViewModel：`HomeViewModel`、`ScalingModesViewModel`、`ScalingModeItem`、`ScalingModeEffectItem`、`EffectParametersViewModel`、`ProfileViewModel`、`NewProfileViewModel`、`SettingsViewModel`、`AboutViewModel`（`Magpie.vcxproj:149-255`）。

## 2. DLSSNR 参数面板

- **参数来源 = HLSL `//!PARAMETER` 注解**：DLSSNR 参数声明在 `src/Effects/DLSSNR/DLSSNR_AI_Filter.hlsl:8-110`，每个参数带 `//!LABEL/DEFAULT/MIN/MAX/STEP`：
  - `enableInputResolutionScaling`（0/1）、`inputResolutionPercent`（100, 25–100）、`residualMultiplier`（1, 1–2）、`nrPreset`（0, 0–3）、`style`（0, 0–2）、`intensity`（1, 0–2）、`localToneStrength`（1, 0–2）、`localStructureStrength`（1, 0–2）、`skinStructureStrength`（-1, -1–2）、`useAutoMask`（0/1）、`uiCorrection`（0/1）、`guidanceMode`（0, 0–3）、`depthInferenceInterval`（4, 1–8）。
- 编译期 `EffectCompiler` 解析注解 → `EffectDesc`；`NativeEffectBackendFactory.cpp:66-98` 把这些参数名映射成 `DLSSNRSettings` 字段。
- **编辑 UI（通用）**：`ScalingModesPage.xaml:9-55` `EffectParametersFlyout` DataTemplate，`x:DataType="local:EffectParametersViewModel"`，`ItemsControl ItemsSource="{x:Bind Params, Mode=OneTime}"`；每个 `ScalingModeParameter`（`EffectParametersViewModel.idl:2-13`：IsBoolean/IsFloat/IsVisible/BooleanValue/Value/Label/ValueText/Minimum/Maximum/Step）→ `CheckBox`（布尔）或 `Label+ValueText+Slider`（浮点，TwoWay）。
- `EffectParametersViewModel`（`EffectParametersViewModel.idl:15-20`）：`ScalingModeIdx`、`EffectIdx`、`Params`（IVector<IInspectable>）——按当前缩放模式的 effect 参数生成。**面板不感知具体模式**，DLSSNR 只是其参数元数据的一个实例；「模式列表只含 DLSSNR」无需改面板机制。

## 3. 配置系统

- 类：`AppSettings`（`src/Magpie/AppSettings.h/.cpp`），全局单例 `AppSettings::Get()`；读取/解析在 `AppSettings.cpp`（~212-330，Load + 迁移），写回 `WriteTextFile(configPath, json)`（691）。
- 版本化：`CONFIG_VERSION = 4`（`AppSettings.cpp:27`）；迁移循环 `for (version = CONFIG_VERSION-1; version >= 2; --version)` 从旧目录找并迁移（`AppSettings.cpp:1355-1375`）。
- 路径：`_UpdateConfigPath`（`AppSettings.cpp:1385-1417`）——便携模式用 exe 目录，否则 `FOLDERID_LocalAppData` + `CONFIG_DIR("config")` + `CONFIG_VERSION` → **`%LOCALAPPDATA%\Magpie\config\v4\config.json`**（`CommonSharedConstants.h:25-26`）。
- schema 内容（据 #4 调研与源码）：`scalingModes` / `effects` / `parameters`（`{参数名: float}`）结构 + `experimentalDlssnrSettingsVersion` 版本化迁移（`AppSettings.cpp` 内处理）——可整体搬，裁剪 Magpie 专属字段即可。
- 访问方式：**全局单例，非依赖注入**；页面/ViewModel 直接 `AppSettings::Get()`。

## 4. FileDialogHelper

- 位置：`src/Magpie/FileDialogHelper.h`（实现 `FileDialogHelper.cpp`）。
- 接口（`FileDialogHelper.h:6-11`）：
  ```cpp
  struct FileDialogHelper {
      static std::optional<std::filesystem::path> OpenFileDialog(
          IFileDialog* fileDialog, FILEOPENDIALOGOPTIONS options = 0) noexcept;
  };
  ```
- 用途：调用方构造 `IFileOpenDialog`/`IFileSaveDialog`，本 helper 负责 `Show` + `GetResult` + `GetDisplayName`（`FileDialogHelper.cpp:30-37`）。调用点：`HomeViewModel.cpp:242,261`、`ScalingModesViewModel.cpp:62,106`、`ProfileViewModel.cpp:168`（导出/导入配置、选择文件）。

## 5. 复用耦合点（新 exe 复用这些页面/ViewModel 须切断/替代）

1. **全局服务单例**（页面/ViewModel 直接 `Xxx::Get()`）：`AppSettings::Get()`（ProfileViewModel、SettingsViewModel 等）、`ScalingService::Get()`（开始/结束缩放，`ScalingService.cpp:206`）、`EffectsService::Get()`（`EffectsService.cpp`，解析 `effects/` 目录、调 Magpie.Core 的 EffectCacheManager/EffectCompiler）、`AdaptersService`、`ShortcutService`、`UpdateService`、`ToastService`、`NotifyIconService`、`LocalizationService`。新 exe 要复用页面，必须实例化/替换这些服务；实时缩放相关（ScalingService/ShortcutService/NotifyIconService/UpdateService/AdaptersService）在离线应用多数不适用。
2. **`App::Get()` 全局单例 + `App.xaml` 资源**：主题、转换器、样式来自 App.xaml；`SettingsCard`/`SettingsExpander`/`SettingsGroup`/`ControlSizeTrigger`/`IsEqualStateTrigger`/`IsNullStateTrigger`/`TextBlockHelper` 的 DependencyProperty 须在启动时注册（`App.cpp:161-167`）。
3. **XAML Islands 初始化**：`WindowsXamlManager::InitializeForCurrentThread` + DispatcherQueue 是前置条件（`App.cpp:131-135`）；`DesktopWindowXamlSource` 装配（`XamlWindow.h:61-65`）。
4. **EffectsService 对 Magpie.Core 的直接调用 + `effects/` 目录依赖**：运行时从 exe 旁 `effects/` 读 HLSL 解析参数/编译（`EffectsService.cpp`；`EFFECTS_DIR="effects"`，`CommonSharedConstants.h:28`）。
5. **本地化**：`APP_RESOURCE_MAP_ID = L"Magpie/Resources"`（`CommonSharedConstants.h:43`），resw 资源随工程打包。
6. **硬编码路径**：配置 `%LOCALAPPDATA%\Magpie\config\v4\config.json`（`AppSettings.cpp:1412`）；日志 `logs\magpie.log`（相对，CWD=exe 目录，`main.cpp:29-32`）；Magpie.Core 的 `%LOCALAPPDATA%\Magpie\FrameGuidance\TensorRTCache`（`DepthAnythingV2Provider.cpp:589-591`）。新应用若要配置隔离（新目录），改 `AppSettings::_UpdateConfigPath` 与 Core 的 LOCALAPPDATA 子串（牵连小，见 #4）。
7. **`MainWindow/XamlWindowT`**：重度 Win32 无边框/DPI/边框绘制实现（`XamlWindow.h`），复用价值低；新 exe 可自建窗口宿主，仅复用 `RootPage`/`PageFrame`/各页面。
8. **单实例/提权 mutex**（`App.cpp:332-377`）与「Smooth Motion 重启」`--wait-for-pid` 逻辑（`main.cpp:38-71`）——Magpie 特有。
9. **Profile 概念**（Profiles = 针对窗口缩放的配置，`RootPage.cpp` 路由 index-4）与 ScalingModes 导出（`ScalingModesPage`）——绑实时缩放/窗口语义，离线应用用不上或需重做。

## 6. 进度 UI 先例

- `HomePage.xaml:70-77`：`ProgressRing`（30px），`Value` 绑 `HomeViewModel.TimerProgressRingValue`（`HomeViewModel.cpp:59`，缩放启动倒计时环形进度）——是**定时器**，不是任务进度。
- `AboutPage.xaml:44`（ProgressRing 16px）、`:70-79`（ProgressBar，注释提到 0.999 防 Layout cycle）——更新检查的进度/状态。
- **结论**：仓库没有长耗时作业（如导出/处理）的进度条或「处理中」页面先例；离线应用的进度 UI 与导出流程是净新增（与 #4「必须新写进度 UI 与导出流程」一致，供 #7 参考）。

---

## 关键文件索引

- `src/Magpie/main.cpp`、`App.{h,cpp}`、`App.xaml`、`MainWindow.{h,cpp}`、`XamlWindow.h`、`RootPage.xaml/.cpp`、`PageFrame.xaml`
- `src/Magpie/ScalingModesPage.xaml`、`EffectParametersViewModel.idl`、`ScalingModeEffectItem.{idl,cpp}`、`ScalingModesViewModel.cpp`
- `src/Magpie/AppSettings.{h,cpp}`、`FileDialogHelper.{h,cpp}`
- `src/Magpie/EffectsService.cpp`、`HomeViewModel.cpp`、`AboutPage.xaml`、`HomePage.xaml`
- `src/Shared/CommonSharedConstants.h`
- `src/Effects/DLSSNR/DLSSNR_AI_Filter.hlsl`
