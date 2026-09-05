#pragma once
#include <d3d12.h>

// Experimental baseline stub: consumer/parameter-block tracking is reserved
// for the upcoming integration and not yet read. Silence -Wunused-private-field
// (ClangCL only; MSVC has no such warning).
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif

struct NVSDK_NGX_Parameter;

namespace Magpie {

class DeviceResources;

// Renderer-session owner for the process-global NGX D3D12 Core state. Feature
// consumers may own independent queues, but share this device and final shutdown.
class NgxD3D12Core {
public:
	NgxD3D12Core() = default;
	NgxD3D12Core(const NgxD3D12Core&) = delete;
	NgxD3D12Core& operator=(const NgxD3D12Core&) = delete;
	~NgxD3D12Core();

	bool Acquire(DeviceResources& resources, std::string_view consumer) noexcept;
	void Release(std::string_view consumer) noexcept;
	ID3D12Device* Device() const noexcept { return _device.get(); }

	bool AllocateParameters(
		NVSDK_NGX_Parameter** parameters,
		std::string_view consumer
	) noexcept;
	bool GetCapabilityParameters(
		NVSDK_NGX_Parameter** parameters,
		std::string_view consumer
	) noexcept;
	bool DestroyParameters(
		NVSDK_NGX_Parameter* parameters,
		std::string_view consumer
	) noexcept;

private:
	void _Shutdown() noexcept;

	winrt::com_ptr<ID3D12Device> _device;
	uint32_t _activeConsumers = 0;
	uint32_t _activeParameterBlocks = 0;
	bool _initialized = false;
};

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

}
