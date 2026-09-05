# 许可与分发矩阵调研（License & distribution matrix research）

> 调研对象：Magpie Experimental fork（`smter/Magpie`，分支 `experimental`，C++/WinRT，主仓库 GPLv3，见根目录 `LICENSE`）。
> 背景目标：规划一个与 `magpie.exe` 同目录发布的新独立可执行程序（离线视频处理：解码 → 复用 DLSSNR 后端降噪 → 编码输出），
> 可能引入 FFmpeg 做解码/编码。本文件只回答许可与再分发问题；技术栈问题见
> [`docs/research/video-pipeline-stack.md`](video-pipeline-stack.md)。
>
> 本文件为本 effort 约定的调研存放位置 `docs/research/license-distribution.md`。每条论断注明来源
> （仓库文件路径 + 行号，或一手 URL）。一手资料优先（官方许可文本 / 官方文档 / 仓库自身），不用二手转述。
>
> **免责声明**：本文是工程侧许可清单与风险梳理，不是法律意见。正式发布前须以随附各 SDK/运行库的
> 最新版许可原文为准（对齐 `docs/THIRD_PARTY_AND_REDISTRIBUTION.md:3-5` 的既有约定）。

---

## 目录

1. [仓库既有风险声明（本地一手证据）](#1-仓库既有风险声明本地一手证据)
2. [(a) FFmpeg：静态 vs 动态链接的许可义务](#a-ffmpeg静态-vs-动态链接的许可义务)
3. [(b) nvngx_dlssnr.dll 与 NGX SDK 条款](#b-nvngx_dlssnrdll-与-ngx-sdk-条款)
4. [(c) GPLv3 + NVIDIA 专有组件组合分发风险的延伸](#c-gplv3--nvidia-专有组件组合分发风险的延伸)
5. [(d) 打包：Build-Release.ps1 / RELEASE-WORKFLOW / build-manifest 需要容纳第二个 exe](#d-打包build-releaseps1--release-workflow--build-manifest-需要容纳第二个-exe)
6. [结论对推荐的影响](#6-结论对推荐的影响)
7. [附：来源索引](#7-附来源索引)

---

## 1. 仓库既有风险声明（本地一手证据）

### 1.1 主仓库许可

- 根目录 `LICENSE` 全文为 GPLv3（`LICENSE:1-2`「GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007」）。
- `LICENSE:669-674` 明确「The GNU General Public License does not permit incorporating your program into
  proprietary programs」——GPLv3 本身不允许把程序并入专有程序（对"并入/组合"的解释见 §4）。
- `README.md:118`：「The Magpie-derived source is licensed under GPLv3. Third-party SDKs, models, and runtimes
  remain under their respective licenses; see Third-party components and redistribution」。

### 1.2 `docs/THIRD_PARTY_AND_REDISTRIBUTION.md`（许可清单，最核心的既有声明）

- `:3-5` 免责声明：本文是工程侧清单，不构成法律意见；「某个组件可以下载或在本机使用，并不自动表示可以把它
  再次放进本项目源码或二进制 Release」。
- `:26` NVIDIA DLSS/NGX 与 DLSS Frame Generation 行：**「Unresolved/high risk when combined with GPLv3 Magpie;
  do not publish such a combined binary without a dedicated review or permission」**。
- `:27` NVIDIA DLSSNR experimental runtime 行：本地提供（可能是官方或社区修改版）、无公开 DLSSNR SDK 合同、
  **「Internal testing only until NVIDIA redistribution permission and GPL compatibility are reviewed」**。
- `:46-48` 社区修改版 `nvngx_dlssnr.dll` 是**独立 Release 资产，不属于项目源码**；仓库及 GitHub 自动生成的
  源码归档中不得包含该文件。
- `:54-60` 发布策略：只发布 GPLv3 源码与依赖获取/构建说明；专有后端在完成再分发权限与 GPL 兼容性核对前
  不进入公开可复现构建；每个二进制包保留 `LICENSE-Magpie.txt`、本文档、各厂商声明和 `build-manifest.json`。
- `:70-78` 发布前检查：`build-manifest.json` 记录提交/版本/功能开关/哈希；每个第三方二进制都要确认可再分发
  条款及必带声明；对该二进制履行 GPLv3 对应源码义务；**含 NVIDIA 后端的包必须单独完成兼容性/权限审核**。

### 1.3 `docs/EXPERIMENTAL_HANDOFF_ZH.md`

- `:20` 内置更新检查自 0.5.3-experimental 起关闭，应用不后台联网检查；社区修改版 `nvngx_dlssnr.dll` 不进源码仓库。
- `:49`（任务指定的段落）：「**GPLv3 与 NVIDIA 专有组件的组合分发存在未解决风险。** SDK、模型、wheel 和本机
  依赖目录不要提交；公开二进制前按许可证专项文档逐项审核。」

### 1.4 README 对 DLSSNR DLL 的披露（`README.md:83-98`）

- `:85` 主 Release 包内含**社区修改版 `nvngx_dlssnr.dll` 310.8.0.0**，面向 RTX 40/50 兼容；「It is not an
  untouched NVIDIA-signed file, and **Windows Authenticode reports a file-hash mismatch**」。
- `:87-91` 同 Release 附 `DLSSNR-DLL-Options-310.8.0.0.zip`：`NVIDIA-Original`（NVIDIA 签名原版）与
  `Community-RTX40-RTX50` 两个选项；替换前必须完全退出 Magpie。
- `:92` 第三方 DLL 不进源码仓库或 GitHub 源码归档。
- `:94-98` `NGX_OTA_Switch.bat`：可临时禁用系统级 NGX OTA 设置、结束堆积的 `nvngx_update.exe` 进程、或恢复
  NVIDIA 默认行为；需要管理员权限，影响其他使用 NGX 的程序。
- 打包进包的 `docs/README-EXPERIMENTAL-RELEASE.txt:42-48` 同样披露社区修改版 DLL 与 Authenticode
  file-hash mismatch，并指向独立 Options zip 中的 NVIDIA 原版。

### 1.5 代码侧事实（离线应用相关）

- `src/Magpie.Core/DLSSNRFilter.cpp`（详见 `docs/research/video-pipeline-stack.md:100-102`）：
  `InitializeSignedSnippet` 从 **exe 同目录**加载 `nvngx_dlssnr.dll` 并以特定
  `DLSSNR_SIGNED_SNIPPET_APPLICATION_ID` 初始化 → 离线应用**必须随包分发 `nvngx_dlssnr.dll`**，且
  video-pipeline-stack.md 已提示「按 LGPL/GPL 注意事项处理 NGX 组件」。

**小结（对推荐 #1/#4 的直接证据）**：仓库现行框架 =「GPLv3 源码正常发布 + 专有组件只作独立 Release 资产、
显式披露 Authenticode 不匹配、附 Options 包与厂商声明、发布前逐组件审核」。任何新分发方案都在这个框架内谈。

---

## (a) FFmpeg：静态 vs 动态链接的许可义务

### a.1 FFmpeg 自身的许可结构（一手：FFmpeg 官方 LICENSE）

- FFmpeg 官方 `LICENSE.md` / 仓库根 `LICENSE` 的表述：**FFmpeg 主体按 GNU LGPL v2.1 或更新版本授权**；但
  FFmpeg 包含很多可选的 GPL 组件与优化，**如果启用这些部分，FFmpeg 整体变为 GPL**。
  来源：<https://github.com/FFmpeg/FFmpeg/blob/fc02470c/LICENSE.md>、
  <https://source.ffmpeg.org/?p=ffmpeg.git;a=blob_plain;f=LICENSE>（发布时以你锁定的 FFmpeg 版本对应
  `LICENSE.md` 原文为准）。
- 构建开关（`configure --help` / INSTALL 文档的官方描述）：
  - `--enable-gpl`：allow use of GPL code, **the resulting libraries and binaries will be under GPL**；
  - `--enable-nonfree`：allow use of nonfree code, **the resulting libraries and binaries will be
    unredistributable**；
  - `--enable-version3`：upgrade (L)GPL to version 3。
    来源：<https://source.ffmpeg.org/?p=ffmpeg.git;a=blob_plain;f=INSTALL>（FFmpeg 官方 INSTALL 文档，
    configure 帮助文本同源）。
- 命令行工具与库的区别：`libav*` 库默认 LGPLv2.1+；`fftools/`（ffmpeg/ffplay/ffprobe 的源码）文件头为
  **GPLv2+ 声明**，因此 **ffmpeg.exe 这类 CLI 二进制按 GPL 对待**（即使库是 LGPL 默认构建）。
  来源：FFmpeg 源码 `fftools/*.c` 文件头（<https://source.ffmpeg.org/?p=ffmpeg.git;a=blob;f=fftools/ffmpeg.c>，
  发布前以实际版本核对）。**注意**：这条有历史歧义（个别构建/文档表述不一），决策前以你锁定的
  `LICENSE.md` 与 `fftools` 源码头为准；保险做法是把 CLI 当 GPL 组件处理。
- FFmpeg 许可讨论的权威上下文：<https://ffmpeg.org/legal.html>、LGPL 兼容性讨论见
  <https://trac.ffmpeg.org/ticket/1229>（LGPL 与平台兼容性的官方 ticket，佐证 LGPL 条款是分发时必须处理的点）。

### a.2 LGPL 的动态/静态链接义务（一手：GNU LGPLv2.1 原文）

- LGPLv2.1 §6 允许把「work that uses the Library」与 Library 组合分发，但必须满足其一：
  - **(a) 提供 relinking 材料**：随分发提供「work that uses the Library」的完整机器可读目标码/源码，
    使用户能修改 Library 后**重新链接**出包含修改后 Library 的可执行文件；
  - **(b) 使用合适的共享库机制**：运行时使用用户系统上已存在的库副本（而非把库函数复制进可执行文件），
    且用户替换为接口兼容的修改版后仍能正常工作；
  - (c)/(d) 书面要约 / 指定地点提供访问。
  来源：<https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>（§6）。
- 静态链接的经典合规路径：要么按 §6(a) 分发可重链接目标码，要么把组合后的整个作品按满足「允许修改与
  反向调试」的条款分发（GPL 即满足该条件）。**对 GPL 应用程序而言，LGPL 库静态链接通常被接受**——因为
  组合作品整体按 GPL 分发，§6 的例外条件成立；动态链接 + 随包携带 DLL 是 Windows 上最常见的稳妥做法
  （用户可替换 DLL，等价于 §6(b) 的语义）。

### a.3 对本项目的结论（关键判断）

新 exe「复用 DLSSNR 后端」意味着它派生自本仓库 GPLv3 源码 → **新 exe 自身是 GPLv3 作品**（派生作品必须
按 GPLv3 分发并附对应源码，`LICENSE:674` 的反面印证；详见 §4.3）。在这个前提下：

1. **FFmpeg 库（默认 LGPLv2.1+）与 GPLv3 应用组合没有许可冲突**：LGPL 允许与 GPL 程序组合；动态链接或
   静态链接均可（静态链接时 §6(a) 的 relink 义务由「组合作品按 GPL 分发」满足，但保守做法仍是动态链接
   或随包提供 relink 材料）。
2. **`--enable-gpl` 可用但会使库变为 GPLv2+**：GPLv2+（"or later"）与 GPLv3 兼容，GPLv3 应用链接 GPLv2+
   库没有问题；代价是 FFmpeg 组件整体按 GPL 分发（源码/要约义务）。
3. **`--enable-nonfree` 不要用**：官方明示 resulting binaries **unredistributable**（a.1），直接与
   「再分发」目标冲突（libfdk_aac 是典型触发项）。
4. **ffmpeg CLI（GPL）作为外部进程**：若把 ffmpeg.exe 当作子进程调用，属于「分离程序/聚合」而非链接
   （GPLv3 §5 aggregate，见 §4.2），CLI 本身的 GPL 义务（随附源码/要约）对本项目（GPLv3）容易满足；
   但建议优先用库内嵌，避免多一个 GPL 二进制与进程管理复杂度。
5. **再分发声明要求**：随包携带 FFmpeg `LICENSE.md`/LGPLv2.1 文本、保留版权声明；若用了 GPL 构建则附
   GPL 义务；FFmpeg 修改过的库要提供修改后源码。这与仓库既有「每组件随附许可/声明」策略一致
   （`THIRD_PARTY_AND_REDISTRIBUTION.md:54-60`）。
6. **专利风险（与版权许可正交，必须单独提示）**：H.264/HEVC/AAC 等编解码的专利授权（Via LA / MPEG LA
   体系）不在 FFmpeg 版权许可范围内；商业分发编码器/解码器可能还需专利许可。若只用 NVIDIA NVENC 硬件
   编码（AV1 无池专利问题相对小；H.264/HEVC 仍需核专利池条款），风险集中在 H.264/HEVC 流。

### a.4 风险清单（FFmpeg 部分）

| 风险 | 说明 | 缓解 |
| --- | --- | --- |
| 静态链接未提供 relink 材料 | 若新 exe 是**非 GPL**，静态链接 LGPL 库必须给 §6(a) 材料 | 本方案新 exe 是 GPLv3，静态链接 OK；仍建议动态链接兜底 |
| `--enable-nonfree` | 产物不可再分发 | 构建配置显式禁用 |
| 把 ffmpeg CLI 当 GPL 二进制分发 | 增加 GPL 源码/要约义务面 | 用库内嵌，或接受 GPL 义务并随附源码 |
| FFmpeg 声明缺失 | LGPL §6 要求保留许可文本 | 打包时复制 `LICENSE.md`/LGPL 文本 + THIRD-PARTY 行 |
| 专利（H.264/HEVC/AAC） | 与版权许可分离 | 发布前核专利池条款；NVENC 硬件编码可降低软件实现风险，但不免除码流专利 |

---

## (b) nvngx_dlssnr.dll 与 NGX SDK 条款

### b.1 组件性质与来源（本地证据 + 社区事实）

- `THIRD_PARTY_AND_REDISTRIBUTION.md:27`：DLSSNR runtime 由 NVIDIA 本地提供，**没有公开的 DLSSNR SDK
  合同**；测试过的文件「may be official or community-modified」。→ DLSSNR 不像 DLSS SR 那样有公开 SDK
  许可路径，法律地位主要靠 NVIDIA 一般性 NGX/驱动条款 + 实际分发实践。
- `README.md:85`：社区修改版 310.8.0.0 面向 RTX 40/50（原版仅官方支持的硬件/驱动组合），**不是 NVIDIA
  签名原文件，Authenticode 报 file-hash mismatch**。
- 社区事实（佐证该 DLL 的离线用法与修改分发生态）：OptiScaler 的 DLSSNR 分支
  （<https://github.com/Dagherbou/OptiScaler_DLSSNR>）、同维护者生态的 DaVinci Resolve DLSS OpenFX 滤镜
  （<https://github.com/SAOG0721/DaVinci-Resolve-DLSS5>，离线视频滤镜先例）。这些是**社区/二手证据**，
  只用于说明「离线处理场景复用 DLSSNR DLL」在生态中已有实践，不代表 NVIDIA 授权。

### b.2 NVIDIA EULA 要点（一手：NGX EULA / DLSS EULA / RTX SDKs License）

- NGX EULA（当前版）：<https://docs.nvidia.com/ngx/latest/ngx-eula/index.html>；
  PDF：<https://docs.nvidia.com/ngx/latest/pdf/NGX-EULA.pdf>；
  2019 版文本镜像（LicenseDB，便于核对条款结构）：<https://scancode-licensedb.aboutcode.org/nvidia-ngx-eula-2019.html>。
- NVIDIA RTX SDKs License（12 Apr 2021，覆盖多款 RTX SDK 下载）：<https://developer.download.nvidia.com/gameworks/NVIDIA_RTX_SDKs_License_12Apr2021.pdf>。
- 要点（以 LicenseDB 2019 文本与官方页为准，发布前必须对照你接受的最新版原文）：
  - **许可授予**：非独占、不可转让的授权，用于**开发应用程序**并把 SDK 组件整合进你的应用；SDK 本体不允许
    单独再分发，只允许「整合进应用后随应用分发」。
  - **限制**：禁止反向工程/反编译/反汇编、禁止修改或制作 SDK 组件的衍生作品、禁止移除版权与声明；组件必须
    原样使用。
  - **离线 vs 游戏内**：EULA 的授权对象是「应用程序」，**没有「仅限游戏内嵌」的字面限制**；但也没有「离线
    处理」豁免——离线视频工具与游戏一样受同一 EULA 约束。DLSSNR 本身无公开 SDK 合同（b.1），其使用依据
    是 NVIDIA 一般性条款与分发实践，风险敞口比公开 SDK 更大。
  - **个人 vs 商业**：NGX/DLSS EULA **不区分个人与商业使用**（没有个人使用豁免条款）；两者受同一协议约束。
    免费 ≠ 无限制。
  - **更新/OTA**：NVIDIA 可更新 NGX 组件；NGX runtime 随驱动提供并可被 NVIDIA 在线更新（见 b.3）。
  - **GPL/copyleft 冲突**：EULA 是专有协议，不授予把组件并入 copyleft 作品所需的源码/再授权权利；
    NVIDIA 官方专门维护「NGX LGPL」页面讨论 NGX 与 LGPL/GPL 的兼容问题
    （<https://docs.nvidia.com/ngx/latest/ngx-lgpl/index.html>、归档版
    <https://docs.nvidia.com/ngx/ngx-archived/ngx-100/ngx-lgpl/>）——这是 **NVIDIA 自己承认该组合存在
    许可兼容问题的官方一手证据**（页面措辞以实际阅读为准）。
  - **现实先例**：DLSS 进入 GPL 的 Blender 时，因许可顾虑只能做成独立专有插件而非并入 GPL 代码
    （<https://www.phoronix.com/news/NVIDIA-DLSS-Blender>；社区讨论
    <https://blenderartists.org/t/nvidia-rtx4000-series-and-blender/1405300>）。说明生态把「NVIDIA 专有
    组件与 GPL 代码同体分发」视为需要回避的组合。这与仓库 `THIRD_PARTY_AND_REDISTRIBUTION.md:26-27` 的
    「unresolved/high risk」判断互相印证。

### b.3 OTA 更新机制（一手 + 仓库证据）

- NGX 架构：NGX 组件（含 `nvngx_dlss*.dll` 一类功能 DLL）可由 NVIDIA 通过**驱动集成 / 在线更新（OTA）**
  提供与更新；`nvngx_update.exe` 是 NVIDIA 的 NGX 更新进程。一手：NVIDIA NGX 文档
  （<https://docs.nvidia.com/ngx/latest/index.html>）、NVIDIA Linux 驱动 README 第 38 章 NGX
  （<https://download.nvidia.com/XFree86/Linux-x86_64/535.54.03/README/ngx.html>）；社区侧对
  `nvngx_update.exe` 行为与「更新系统级 NGX 组件」的观察
  （<https://forums.fatsharkgames.com/t/the-nvidia-ngx-updater-process-is-preventing-the-darktires-official-launcher-from-running/112696>）。
- 仓库一手证据：`README.md:94-98` 的 `NGX_OTA_Switch.bat` 描述的就是「系统级 NGX OTA 设置 + 堆积的
  `nvngx_update.exe` 进程」问题——**本仓库已实际遇到 OTA 机制**。
- 对离线应用的直接影响：
  1. 若系统级 NGX OTA 开启，NVIDIA 可能更新系统 NGX 组件；应用目录里的 `nvngx_dlssnr.dll` 是否被 OTA
     覆盖取决于 NGX 的组件解析顺序与版本策略，行为不可完全由应用控制；
  2. 社区修改版 DLL 不在 NVIDIA 签名管理范围，OTA 更可能**绕过或拒绝**它，而不是管理它——版本漂移与
     「NVIDIA 原版被 OTA 覆盖、社区版兼容性丢失」都是实际风险（README 已要求替换前完全退出程序、恢复
     默认 OTA 设置）；
  3. 离线批处理长时间运行期间被 OTA 中途更新组件，会造成行为不可复现——发布说明需写清
     「关闭 OTA / 锁版本」的用户指引（`RELEASE-WORKFLOW.md:106-107` 已有「已知问题应提供可操作回退方法」
     的规范可循）。

### b.4 签名校验 / 完整性

- 官方原版 `nvngx_dlssnr.dll` 是 NVIDIA 签名的二进制；**修改后 Authenticode 签名与文件哈希不匹配**
  （`README.md:85`、`docs/README-EXPERIMENTAL-RELEASE.txt:44-48` 是仓库对事实的正式披露）。
- NGX 运行时有组件加载/版本校验行为（修改过的 `nvngx_dlss*.dll` 可能被拒绝加载或行为异常）——社区一手
  观察：DLSS-Enabler 的「NVIDIA Signature Checks」章节
  （<https://deepwiki.com/artur-graniszewski/DLSS-Enabler/5.1-nvidia-signature-checks>）、Guru3D
  「Using patched nvngx dll files」讨论（<https://forums.guru3d.com/threads/using-patched-nvngx-dll-files.449824/>）、
  NVIDIA 开发者论坛 NGX 加载失败案例（<https://forums.developer.nvidia.com/t/my-dx12-app-fails-to-load-nvngx-dlss-dll/223087>）。
- 社区修改版的分发风险（对推荐 #4 的直接证据）：
  1. **版权/合同风险**：对 NVIDIA 版权二进制做修改并再分发，既违反 EULA 的「禁止修改/衍生」条款（b.2），
     也缺乏 NVIDIA 授权；GPL 帮不了忙——DLL 不是 GPL 组件；
  2. **完整性/供应链风险**：非官方签名文件无法验证来源，用户拿到的「社区版」无法审计（无源码、无构建
     复现），存在被投毒的理论面；Windows 层面签名告警进一步放大信任成本；
  3. **平台互操作风险**：NGX 运行时/驱动的版本校验升级可能随时让修改版失效（b.3/b.4 前述）。

---

## (c) GPLv3 + NVIDIA 专有组件组合分发风险的延伸

### c.1 GNU 侧的一手依据

- GPLv3 §5「aggregate」：把 GPL 作品与「separate and independent works」放在同一存储/分发媒介上构成
  aggregate 时，**GPL 不适用于聚合中的其他部分**（本仓库 `LICENSE:235-243`）。
- GNU GPL FAQ：GPL 作品与专有模块**链接**成单个程序时，整个组合作品必须按 GPL 分发；「mere aggregation」
  与「combining two modules into one program」的界限取决于模块间是否有「intimate data communication or
  control flow」。来源：<https://www.gnu.org/licenses/gpl-faq.en.html>（条目：Combining two modules /
  Linking GPL-covered code with proprietary modules / Mere aggregation）。→ **「运行时动态加载专有 DLL 是否
  构成链接（combined work）」在 GPL 解释上就是争议点**，这正是仓库文档用「unresolved」的原因
  （`THIRD_PARTY_AND_REDISTRIBUTION.md:26`、`EXPERIMENTAL_HANDOFF_ZH.md:49`）。
- NVIDIA 侧一手证据（b.2）：NGX EULA 专有条款 + NVIDIA 官方「NGX LGPL」页面 + Blender 先例 → 组合的
  **实践性不兼容**（GPL 要求对组合作品提供源码/对应源码、允许修改与反向调试；NVIDIA EULA 禁止修改/反向
  工程且不提供组件源码）。两边叠加 = 未解决风险，不是「已证明违法」，而是「没有干净的合规路径」。
- 仓库的处置策略（§1.2）：不并入源码、不放进可复现构建、显式披露、逐组件审核、单独 Release 资产。

### c.2 延伸到新应用（三种情形逐一分析）

1. **同目录双 exe（magpie.exe + 新 exe）**：
   - 新 exe 派生自仓库 GPLv3 源码（复用 DLSSNR 后端）→ 新 exe 是 GPLv3 派生作品，必须按 GPLv3 分发并附
     对应源码（`LICENSE:222-228` 第 5 节「license the entire work as a whole under this License」）。
   - 两个 exe 同目录/同 ZIP 属于 §5 aggregate：**不因共处一目录给任一 exe 增加 GPL 义务**；但聚合不改变
     各自组件的义务——包内 GPLv3 对应源码/要约与 NVIDIA 专有组件的披露都要覆盖**两个** exe。
   - 若新 exe 是**全新原创、不派生**本仓库代码（本方案不成立，因为要复用 DLSSNR 后端），它可以是任意许可；
     但「与 GPLv3 的 magpie.exe 同包」仍需按 aggregate 处理，且共享 NVIDIA 组件不改变任何一方的许可状态。
2. **共用/不共用 nvngx_dlssnr.dll**：
   - 许可状态与「几个进程加载」无关：DLL 是 NVIDIA 专有运行时，每个副本受同一 EULA 约束。共用一份物理
     拷贝（两个 exe 同目录各自加载）**不新增许可义务**，也不消减既有风险；
   - 共用会**放大暴露面**：一个被修改的 NVIDIA 签名二进制现在有两个消费者；NGX OTA（b.3）若更新/覆盖该
     DLL，同时影响两个程序；发布说明、Options 包、哈希核对要覆盖两个 exe 的加载路径；
   - 不共用（各自目录各放一份）则包体重复、两处哈希，通常无必要——推荐共用一份并统一在包内声明。
3. **FFmpeg 加入**：
   - FFmpeg（LGPL/GPL）与 GPLv3 exe 兼容（§a.3），**不会加剧** NVIDIA/GPL 冲突；
   - FFmpeg 库与 NGX 组件互不链接（各自独立加载），不存在把 NVIDIA 组件「拖进」FFmpeg 许可的问题；
   - 唯一新增面是 FFmpeg 自身的声明/relink/专利义务（§a.4），全部在既有「逐组件审核」框架内。

### c.3 一句话总结

> 新 exe = GPLv3 派生作品 + LGPL FFmpeg（兼容）+ NVIDIA 专有 DLSSNR 运行时（既有未解决风险）。风险
> 不是新类别，而是**既有风险的第二份拷贝**；打包与披露必须把既有框架原样复制到新 exe 上，并新增
> 「两个消费者共用一份被修改的 NVIDIA 签名 DLL」的说明。

---

## (d) 打包：Build-Release.ps1 / RELEASE-WORKFLOW / build-manifest 需要容纳第二个 exe

### d.1 现状（`scripts/Build-Release.ps1`，行号为一手证据）

- `:218-225` `$requiredRuntimePaths` 硬编码单 exe 布局：`Magpie.exe / resources.pri / Microsoft.UI.Xaml.dll /
  TouchHelper.exe / Updater.exe / effects`——**没有第二个 exe 槽位**。
- `:190-216` MSBuild Rebuild 只构建 `Magpie.slnx`；第二个 exe 若在同一解决方案，需加入 `.slnx`/项目依赖，
  否则脚本不会构建它。
- `:244-247` 把 `bin\Release\x64` 下除 `.pdb/.lib/.exp` 与 `Magpie.next.exe` 外的内容全量拷入 staging——
  新 exe 与 FFmpeg DLL 只要落在 buildOutput 就会被自动带上；`Magpie.next.exe` 的排除逻辑（`:246`）说明
  仓库已有「第二个 exe 不进 Release」的显式开关先例。
- `:257-267` 可选组件裁剪已有范式（`-ExcludeTensorRTDepthRuntime` 移除 TensorRT runtime 与其许可目录）——
  FFmpeg/DLSSNR 的可选裁剪可照此加 switch。
- `:269-278` 许可/声明复制：`LICENSE → LICENSE-Magpie.txt`、`README-EXPERIMENTAL-RELEASE.txt →
  README-Experimental.txt`、`THIRD_PARTY_AND_REDISTRIBUTION.md → THIRD-PARTY-NOTICES.md`——**FFmpeg
  LICENSE/LGPL 文本、NVIDIA 相关声明需要加入这一复制段**。
- `:292-321` `build-manifest.json` 生成：`files[]` 是 staging 全量递归（path/bytes/sha256/fileVersion/
  productVersion，`:292-301`），新增 exe/FFmpeg DLL **自动入清单**；`featureOptions` 只读
  `BuildOptions.props.user` 的 `Enable*` 属性（`:280-290`）；schemaVersion=1（`:309`）。
- `:355-378` ZIP 条目为 `$PackageName/<相对路径>`——第二个 exe 自然落在 `Magpie.exe` 同层。

### d.2 需要的改动清单（按文件）

**`scripts/Build-Release.ps1`**
1. `requiredRuntimePaths`（`:218-225`）：新增第二 exe 文件名与 FFmpeg 库（`libavcodec/avformat/avutil/
   swscale` 等）、`nvngx_dlssnr.dll`（若主包继续内置）的必检项；
2. 构建段（`:195-216`）：确认第二 exe 项目在 `Magpie.slnx` 中并随 Rebuild 产出；
3. 可选组件 switch（对齐 `:257-267`）：新增 `-ExcludeFFmpeg`/`-ExcludeDLSSNRDll`（或统一
   `-ExcludeOptionalComponents`），并相应更新 featureOptions/README 文案；
4. 声明复制段（`:269-278`）：新增 FFmpeg `LICENSE.md`/LGPL 文本、NVIDIA DLSSNR/NGX 声明文件的复制；
5. 包内 README（`:271-276`）：增加「第二 exe 用法 + DLSSNR DLL 被两个程序共用」说明；
6. manifest（`:292-321`）：结构不变即可容纳新文件；建议**新增 `components[]` 或 `licenses[]` 映射**
   （文件 → 许可/声明文件）并 bump `schemaVersion`，否则「哪个 DLL 对应哪份许可」仍靠人肉核对
   （`THIRD_PARTY_AND_REDISTRIBUTION.md:75` 要求逐文件确认可再分发条款，机器可读映射直接服务该检查项）。

**`docs/experimental/RELEASE-WORKFLOW.md`**
1. 主包结构（`:112-120`）：把第二 exe 加入「完整主包」定义，明确它与 `Magpie.exe` 的目录关系与更新
   节奏（不能只替换单个 exe；对齐 `:118-119` 的「一起更新」精神）；
2. 可选附件范式（`:122-137` TensorRT 分卷 = 「解压后合并到 Magpie.exe 所在目录」的现成先例）：FFmpeg
   组件/DLSSNR DLL 若做成可选附件，直接套用该范式与命名/校验规则；
3. DLSSNR DLL Options 包（`:139-143`）：补充「同一 DLL 同时服务 Magpie 与离线处理 exe」的替换/回退说明
   与哈希核对；
4. 构建检查（`:154-163`）：新增「FFmpeg 组件许可/声明齐全、未启用 nonfree、静态链接时 relink 材料或
   GPL 对应源码」与「第二 exe 的 GPL 对应源码在包内」检查项。

**`docs/THIRD_PARTY_AND_REDISTRIBUTION.md`**
1. 组件矩阵新增 FFmpeg 行（LGPLv2.1+ / 可选 GPL；动态链接优先；禁止 nonfree；随附 LICENSE）；
2. 扩展 DLSSNR 行（`:27`）与 NVIDIA 行（`:26`）：「新离线 exe 同样加载该运行时，风险未解决、需单独审核」；
3. 新增「第二 exe（GPLv3 派生）」行：随包附对应源码/要约。

**build-manifest.json**：schemaVersion 1 → 2（或加 optional 字段）；files[] 自动增长；新增许可映射与
「DLSSNR DLL 为社区修改版、Authenticode 不匹配」的显式元数据（把 README 的披露搬进机器可读清单）。

---

## 6. 结论对推荐的影响

| # | 推荐 | 判定 | 证据（一句话） |
|---|------|------|------|
| 1 | 新 exe 与 magpie.exe 同目录发布、共用 DLSSNR 运行时，在既有风险声明框架内可行 | **CONFIRMED（附前提）** | 新 exe 派生自 GPLv3 仓库源码 → 按 GPLv3 分发并附对应源码；同目录属 GPLv3 §5 aggregate（`LICENSE:235-243`），不新增义务；共用一份 NVIDIA 专有 DLL 许可状态不变（§c.2）；前提是把既有「独立 Release 资产 + 披露 + 逐组件审核」框架（`THIRD_PARTY_AND_REDISTRIBUTION.md:26-27,46-48,54-60`）原样复制到新 exe，并为「两个消费者共用被修改 DLL」补说明。 |
| 2 | FFmpeg 引入后按 LGPL 合规做法（优先动态链接/合理声明）即可接受 | **CONFIRMED（需精化）** | FFmpeg 库默认 LGPLv2.1+，与 GPLv3 应用兼容（动态或静态均可，静态时 §6(a) relink 义务由 GPL 分发满足，LGPLv2.1 §6）；**硬约束是禁用 `--enable-nonfree`（产物不可再分发）**，并随包带 FFmpeg LICENSE/LGPL 文本；专利（H.264/HEVC/AAC）与版权许可分离，需单独核。 |
| 3 | scripts/Build-Release.ps1 与 build-manifest 需要扩展 | **CONFIRMED（事实）** | `requiredRuntimePaths` 只有单 exe 布局（`Build-Release.ps1:218-225`）；需加第二 exe/FFmpeg 必检、可选裁剪 switch（对齐 `:257-267`）、声明复制（`:269-278`）；manifest `files[]` 自动含新文件（`:292-301`），但建议新增组件→许可映射并 bump schema（`:309`）；RELEASE-WORKFLOW 的附件范式（`:122-143`）可直接套用。 |
| 4 | 社区修改版 nvngx_dlssnr.dll 的现有分发方式维持现状（不新增风险） | **ADJUSTED** | 「方式」可维持（独立 Options 包 + 显式 Authenticode 披露 + 不进源码 + OTA 开关，`README.md:83-98`、`THIRD_PARTY_AND_REDISTRIBUTION.md:46-48`），但「**不新增风险**」不成立：新增第二个消费者 = 被修改的 NVIDIA 签名二进制多一份加载面与包路径，且每次新分发都是 NVIDIA 主张权利的新机会（EULA 禁修改/衍生，b.2；NGX 签名/版本校验可能拒绝，b.4）；应把该 DLL 的分发范围与披露显式扩展到新 exe，并维持「优先推荐用户换用 NVIDIA 原版」的引导。 |

---

## 7. 附：来源索引

### 本地一手证据（仓库文件:行号）

- `LICENSE:1-2,222-228,235-243,669-674` — GPLv3；§5 整件授权与 aggregate；GPL 不允许并入专有程序。
- `docs/THIRD_PARTY_AND_REDISTRIBUTION.md:3-5,26-27,46-48,54-60,70-78` — 免责声明；NVIDIA 组合
  unresolved/high risk；DLSSNR 仅内部测试；社区 DLL 独立 Release 资产；发布策略；发布前检查。
- `docs/EXPERIMENTAL_HANDOFF_ZH.md:20,42,49` — 更新检查关闭；构建脚本与 manifest 描述；GPLv3+NVIDIA
  组合未解决风险。
- `README.md:83-98,118` — DLSSNR DLL choices（社区版 310.8.0.0、Authenticode mismatch、Options 包）；
  NGX OTA 工具；GPLv3 + 第三方许可。
- `docs/README-EXPERIMENTAL-RELEASE.txt:42-48,60-61` — 包内 DLSSNR DLL 披露与包内文档清单。
- `scripts/Build-Release.ps1:190-216,218-225,244-267,269-278,292-321,355-378` — 构建/布局/裁剪/声明/
  manifest/ZIP 结构。
- `docs/experimental/RELEASE-WORKFLOW.md:112-143,154-163,177` — 主包结构、TensorRT 分卷范式、DLSSNR
  Options 包规范、构建检查、v0.5.7 规范来源。
- `docs/research/video-pipeline-stack.md:100-102` — `InitializeSignedSnippet` 从 exe 同目录加载
  `nvngx_dlssnr.dll`；离线应用需随包分发该 DLL。

### 外部一手来源（URL）

- FFmpeg 官方 LICENSE：<https://github.com/FFmpeg/FFmpeg/blob/fc02470c/LICENSE.md>、
  <https://source.ffmpeg.org/?p=ffmpeg.git;a=blob_plain;f=LICENSE>；INSTALL/configure 开关：
  <https://source.ffmpeg.org/?p=ffmpeg.git;a=blob_plain;f=INSTALL>；legal：
  <https://ffmpeg.org/legal.html>；fftools 源码头：<https://source.ffmpeg.org/?p=ffmpeg.git;a=blob;f=fftools/ffmpeg.c>。
- GNU LGPLv2.1 原文（§6 relink/共享库机制）：<https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>。
- GNU GPL FAQ（组合 vs 聚合、专有模块链接）：<https://www.gnu.org/licenses/gpl-faq.en.html>。
- NVIDIA NGX EULA（当前版）：<https://docs.nvidia.com/ngx/latest/ngx-eula/index.html>；
  PDF：<https://docs.nvidia.com/ngx/latest/pdf/NGX-EULA.pdf>；2019 文本镜像：
  <https://scancode-licensedb.aboutcode.org/nvidia-ngx-eula-2019.html>。
- NVIDIA「NGX LGPL」页（官方承认 NGX 与 LGPL/GPL 的兼容问题）：<https://docs.nvidia.com/ngx/latest/ngx-lgpl/index.html>、
  <https://docs.nvidia.com/ngx/ngx-archived/ngx-100/ngx-lgpl/>。
- NVIDIA RTX SDKs License（12 Apr 2021）：<https://developer.download.nvidia.com/gameworks/NVIDIA_RTX_SDKs_License_12Apr2021.pdf>。
- NVIDIA NGX 文档 / Linux 驱动 README 第 38 章（OTA/组件更新机制）：
  <https://docs.nvidia.com/ngx/latest/index.html>、
  <https://download.nvidia.com/XFree86/Linux-x86_64/535.54.03/README/ngx.html>。
- NVIDIA 签名校验与修改 DLL 的社区一手观察：<https://deepwiki.com/artur-graniszewski/DLSS-Enabler/5.1-nvidia-signature-checks>、
  <https://forums.guru3d.com/threads/using-patched-nvngx-dll-files.449824/>、
  <https://forums.developer.nvidia.com/t/my-dx12-app-fails-to-load-nvngx-dlss-dll/223087>；
  `nvngx_update.exe` 行为：<https://forums.fatsharkgames.com/t/the-nvidia-ngx-updater-process-is-preventing-the-darktires-official-launcher-from-running/112696>。
- GPL + DLSS 的现实先例（Blender）：<https://www.phoronix.com/news/NVIDIA-DLSS-Blender>、
  <https://blenderartists.org/t/nvidia-rtx4000-series-and-blender/1405300>。
- 社区/二手（仅作生态背景，不作授权依据）：<https://github.com/Dagherbou/OptiScaler_DLSSNR>、
  <https://github.com/SAOG0721/DaVinci-Resolve-DLSS5>。
