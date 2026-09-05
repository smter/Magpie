# v1 直调 DLSSNRFilter，不引入 effects 链

MagpieVideo v1 直调 `DLSSNRFilter`（D3D11 RGBA8 同分辨率纹理进/出），不复用 Magpie 的 EffectDrawer/NativeEffectBackend effects 链基础设施。理由：完整 effects 链耦合 Renderer/ScalingWindow 的实时机制（HWND、捕获源、双线程、Presenter），仓库没有离屏/无头先例；而 DLSSNRFilter 本身是独立的 NativeEffectBackend，直调只需 DeviceResources + NgxD3D12Core 两个外部服务。缩放模式机制仅保留接口位（config schema 与模式概念），未来增加 SR/FG 模式时再挂 NativeEffectBackend。
