# MagpieVideo 与 magpie.exe 同 solution 双 exe，链接 Magpie.Core 静态库

MagpieVideo（与 magpie.exe 平级的第二个离线视频处理 exe）采用**同一 solution 内新增独立 exe 工程**（`src/MagpieVideo`），通过 `ProjectReference` 链接 Magpie.Core 静态库，而不是独立仓库或复制源码。理由：① 两 exe 同目录发布构成 GPLv3 §5 aggregate、不新增义务（见「许可与分发矩阵」调查）；② 复用现有构建接线（Common.Pre/Post.props、Conan2 + NuGet、BuildOptions 特性开关、版本管线）成本最低；③ MSVC 对静态库按需拉取 obj，未被引用的单例/代码不会进入新 exe 二进制（`/OPT:REF`）。代价：新工程须复制 Magpie.vcxproj 中 NGX/D3D12 的 Link 依赖（`nvsdk_ngx_s.lib`、`D3D12.lib`）与运行时拷贝 target（`nvngx_dlssnr.dll`、`FrameGuidance\*`），发布脚本需开第二 exe 槽位。
