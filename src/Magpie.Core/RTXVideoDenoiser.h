#pragma once
#include "NativeEffectBackend.h"
#include "ScalingOptions.h"

// Experimental baseline stub: reserved members are not yet read. Silence
// -Wunused-private-field (ClangCL only; MSVC has no such warning).
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif

namespace Magpie {

class DeviceResources;

// NVIDIA VideoSuperRes modes 8-11 perform same-resolution denoising. The
// native backend uses D3D11/CUDA interop, so no frame is copied through CPU.
class RTXVideoDenoiser final : public NativeEffectBackend {
public:
	RTXVideoDenoiser();
	RTXVideoDenoiser(const RTXVideoDenoiser&) = delete;
	RTXVideoDenoiser& operator=(const RTXVideoDenoiser&) = delete;
	~RTXVideoDenoiser() override;

	bool Initialize(
		DeviceResources& deviceResources,
		ID3D11Texture2D* input,
		ID3D11Texture2D* output,
		uint32_t qualityLevel
	) noexcept;

	bool Resize(
		DeviceResources& deviceResources,
		ID3D11Texture2D* input,
		ID3D11Texture2D* output
	) noexcept override;

	bool Draw(const NativeEffectDrawContext& context) noexcept override;
	ScalingError InitializationError() const noexcept { return _initializationError; }

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
	uint32_t _qualityLevel = 8;
	ScalingError _initializationError = ScalingError::NoError;
};

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

}
