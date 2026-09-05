#pragma once
#include "NativeEffectBackend.h"

// The DLSS-SR adapter below is an experimental baseline stub: several private
// members are reserved for the upcoming NGX integration and are not yet read.
// Silence -Wunused-private-field (ClangCL only; MSVC has no such warning) so the
// stub compiles cleanly until the integration lands.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif

namespace Magpie {

class DeviceResources;

struct DLSSSRSettings {
	bool enableJitter = false;
	bool useMotionVectors = true;
	bool useEstimatedDepth = false;
};

// Experimental DLSS-SR adapter for captured colour frames. The non-jitter
// path can consume Renderer-owned optical flow and estimated inverse depth.
class DLSSSRUpscaler final : public NativeEffectBackend {
public:
	DLSSSRUpscaler() = default;
	DLSSSRUpscaler(const DLSSSRUpscaler&) = delete;
	DLSSSRUpscaler& operator=(const DLSSSRUpscaler&) = delete;
	~DLSSSRUpscaler() override;

	bool Initialize(
		DeviceResources& deviceResources,
		ID3D11Texture2D* input,
		ID3D11Texture2D* output,
		const DLSSSRSettings& settings = {}
	) noexcept;

	FrameGuidanceRequirements GetFrameGuidanceRequirements() const noexcept override;

	bool Resize(
		DeviceResources& deviceResources,
		ID3D11Texture2D* input,
		ID3D11Texture2D* output
	) noexcept override;

	bool Draw(const NativeEffectDrawContext& context) noexcept override;

private:
	void _Reset() noexcept;

	ID3D11Device5* _device = nullptr;
	ID3D11DeviceContext4* _d3dDC = nullptr;
	winrt::com_ptr<ID3D11Texture2D> _zeroMotionVectors;
	winrt::com_ptr<ID3D11UnorderedAccessView> _zeroMotionVectorsUav;
	winrt::com_ptr<ID3D11Texture2D> _zeroDepth;
	winrt::com_ptr<ID3D11UnorderedAccessView> _zeroDepthUav;
	winrt::com_ptr<ID3D11Texture2D> _biasCurrentColorMask;
	winrt::com_ptr<ID3D11UnorderedAccessView> _biasCurrentColorMaskUav;
	void* _parameters = nullptr;
	void* _feature = nullptr;
	bool _ngxInitialized = false;
	bool _resetHistory = true;
	DLSSSRSettings _settings{};
	uint32_t _frameIndex = 0;
	uint8_t _lastGuidanceBinding = UINT8_MAX;
	FrameGuidanceFrameId _lastGuidanceResetFrameId =
		std::numeric_limits<FrameGuidanceFrameId>::max();
};

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

}
