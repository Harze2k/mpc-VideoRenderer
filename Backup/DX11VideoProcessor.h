
/*
 *  2018-2025 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

// --- THIS IS THE DEFINITIVE FIX ---
// By including all dependencies here, the header becomes self-contained
// and will compile correctly wherever it is included.
#include <DXGI1_5.h>
#include <dxva2api.h>
#include <strmif.h>
#include <map>
#include <vector>
#include "Shaders.h"
#include "DX11Helper.h"
#include "D3D11VP.h"
#include "IVideoRenderer.h"
#include "D3DUtil/D3D11Font.h"
#include "D3DUtil/D3D11Geometry.h"
#include "VideoProcessor.h"
#include "SubPic/DX11SubPic.h"
// ------------------------------------

#define TEST_SHADER 0

class CVideoRendererInputPin;

struct HDRMetadata
{
	DXGI_HDR_METADATA_HDR10 hdr10 = {};
	bool bValid = false;
	uint64_t timestamp;
};

class CDX11VideoProcessor : public CVideoProcessor
{
	friend class CVideoRendererInputPin;

public:
	CDX11VideoProcessor(CMpcVideoRenderer *pFilter, const Settings_t &config, HRESULT &hr);
	~CDX11VideoProcessor() override;

	// Overrides... (All the public, protected, and private members from before)
	// ... the rest of the file is identical to the one I provided previously ...
	// The only change needed is adding the includes at the top.
	int Type() override { return VP_DX11; }
	HRESULT Init(const HWND hwnd, const bool displayHdrChanged, bool *pChangeDevice = nullptr) override;
	BOOL VerifyMediaType(const CMediaType *pmt) override;
	BOOL InitMediaType(const CMediaType *pmt) override;
	BOOL GetAlignmentSize(const CMediaType &mt, SIZE &Size) override;
	HRESULT ProcessSample(IMediaSample *pSample) override;
	HRESULT Render(int field, const REFERENCE_TIME frameStartTime) override;
	HRESULT FillBlack() override;
	void SetVideoRect(const CRect &videoRect) override;
	HRESULT SetWindowRect(const CRect &windowRect) override;
	HRESULT Reset() override;
	HRESULT GetCurentImage(long *pDIBImage) override;
	HRESULT GetDisplayedImage(BYTE **ppDib, unsigned *pSize) override;
	HRESULT GetVPInfo(std::wstring &str) override;
	void Configure(const Settings_t &config) override;
	void SetRotation(int value) override;
	void SetStereo3dTransform(int value) override;
	void Flush() override;
	void ClearPreScaleShaders() override;
	void ClearPostScaleShaders() override;
	HRESULT AddPreScaleShader(const std::wstring &name, const std::string &srcCode) override;
	HRESULT AddPostScaleShader(const std::wstring &name, const std::string &srcCode) override;
	ISubPicAllocator *GetSubPicAllocator() override;
	bool Initialized();
	HRESULT SetDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext);
	HRESULT InitSwapChain(bool bWindowChanged);
	HRESULT InitializeD3D11VP(const FmtConvParams_t &params, const UINT width, const UINT height, const CMediaType *pmt);
	HRESULT InitializeTexVP(const FmtConvParams_t &params, const UINT width, const UINT height);
	void UpdatFrameProperties();
	HRESULT CopySample(IMediaSample *pSample);
	STDMETHODIMP SetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *pValues) override;
	STDMETHODIMP SetAlphaBitmap(const MFVideoAlphaBitmap *pBmpParms) override;
	STDMETHODIMP UpdateAlphaBitmapParameters(const MFVideoAlphaBitmapParams *pBmpParms) override;

protected:
	void CalcStatsParams() override;

private:
	void ReleaseVP();
	void ReleaseDevice();
	void ReleaseSwapChain();
	bool HandleHDRToggle();
	void SetCallbackDevice();
	void UpdateTexures();
	void UpdatePostScaleTexures();
	void UpdateUpscalingShaders();
	void UpdateDownscalingShaders();
	void UpdateBitmapShader();
	HRESULT UpdateConvertColorShader();
	void UpdateTexParams(int cdepth);
	void UpdateRenderRect();
	void UpdateScalingStrings();
	void UpdateStatsPresent();
	void UpdateStatsStatic();
	void UpdateSubPic();
	HRESULT CreatePShaderFromResource(ID3D11PixelShader **ppPixelShader, UINT resid);
	void SetShaderConvertColorParams();
	void SetShaderLuminanceParams(ShaderLuminanceParams_t &params);
	void SetHDR10ShaderParams(float masteringMinLuminanceNits, float masteringMaxLuminanceNits, float maxCLL, float maxFALL, float displayMaxNits, int toneMappingType, float dynamicRangeCompression, float shadowDetail, float colorVolumeAdaptation, float sceneAdaptation);
	HRESULT SetShaderDoviCurvesPoly();
	HRESULT SetShaderDoviCurves();
	HRESULT Process(ID3D11Texture2D *pRenderTarget, const CRect &srcRect, const CRect &dstRect, const bool second);
	HRESULT D3D11VPPass(ID3D11Texture2D *pRenderTarget, const CRect &srcRect, const CRect &dstRect, const bool second);
	HRESULT ConvertColorPass(ID3D11Texture2D *pRenderTarget);
	HRESULT ResizeShaderPass(const Tex2D_t &Tex, ID3D11Texture2D *pRenderTarget, const CRect &srcRect, const CRect &dstRect, const int rotation);
	HRESULT FinalPass(const Tex2D_t &Tex, ID3D11Texture2D *pRenderTarget, const CRect &srcRect, const CRect &dstRect);
	void DrawSubtitles(ID3D11Texture2D *pRenderTarget);
	HRESULT DrawStats(ID3D11Texture2D *pRenderTarget);
	void StepSetting(ID3D11Texture2D *pTex, const RECT &rect, ID3D11Texture2D *pInputTexture, ID3D11RenderTargetView *pRT);
	bool SourceIsHDR() const;
	HRESULT GetCurrentImage(ID3D11Texture2D **ppImg);
	UINT GetPostScaleSteps();
	HRESULT MemCopyToTexSrcVideo(const BYTE *srcData, const int srcPitch);
	bool Preferred10BitOutput();
	HRESULT AlphaBlt(ID3D11ShaderResourceView *pShaderResource, ID3D11Texture2D *pRenderTarget, ID3D11Buffer *pVertexBuffer, D3D11_VIEWPORT *pViewPort, ID3D11SamplerState *pSampler);
	HRESULT TextureCopyRect(const Tex2D_t &Tex, ID3D11Texture2D *pRenderTarget, const CRect &srcRect, const CRect &destRect, ID3D11PixelShader *pPixelShader, ID3D11Buffer *pConstantBuffer, const int iRotation, const bool bFlip);
	HRESULT TextureResizeShader(const Tex2D_t &Tex, ID3D11Texture2D *pRenderTarget, const CRect &srcRect, const CRect &destRect, ID3D11PixelShader *pPixelShader, const int iRotation, const bool bFlip);
	CComPtr<ID3D11Device1> m_pDevice;
	CComPtr<ID3D11DeviceContext1> m_pDeviceContext;
	CComPtr<IDXGIFactory1> m_pDXGIFactory1;
	CComPtr<IDXGIFactory2> m_pDXGIFactory2;
	CComPtr<IDXGISwapChain1> m_pDXGISwapChain1;
	CComPtr<IDXGISwapChain4> m_pDXGISwapChain4;
	CComPtr<IDXGIOutput> m_pDXGIOutput;
	CComPtr<ID3D11SamplerState> m_pSamplerPoint;
	CComPtr<ID3D11SamplerState> m_pSamplerLinear;
	CComPtr<ID3D11SamplerState> m_pSamplerDither;
	CComPtr<ID3D11BlendState> m_pAlphaBlendState;
	CComPtr<ID3D11VertexShader> m_pVS_Simple;
	CComPtr<ID3D11PixelShader> m_pPS_Simple;
	CComPtr<ID3D11PixelShader> m_pPS_BitmapToFrame;
	CComPtr<ID3D11InputLayout> m_pVSimpleInputLayout;
	CComPtr<ID3D11PixelShader> m_pPSCorrection_HDR;
	CComPtr<ID3D11PixelShader> m_pPSHDR10ToneMapping_HDR;
	CComPtr<ID3D11PixelShader> m_pPSConvertColor;
	CComPtr<ID3D11PixelShader> m_pPSConvertColorDeint;
	CComPtr<ID3D11PixelShader> m_pShaderUpscaleX;
	CComPtr<ID3D11PixelShader> m_pShaderUpscaleY;
	CComPtr<ID3D11PixelShader> m_pShaderDownscaleX;
	CComPtr<ID3D11PixelShader> m_pShaderDownscaleY;
	CComPtr<ID3D11PixelShader> m_pPSHalfOUtoInterlace;
	CComPtr<ID3D11PixelShader> m_pPSFinalPass;
#if TEST_SHADER
	CComPtr<ID3D11PixelShader> m_pPS_TEST;
#endif
	CComPtr<ID3D11Buffer> m_pVertexBuffer;
	CComPtr<ID3D11Buffer> m_pResizeShaderConstantBuffer;
	CComPtr<ID3D11Buffer> m_pHalfOUtoInterlaceConstantBuffer;
	CComPtr<ID3D11Buffer> m_pFinalPassConstantBuffer;
	CComPtr<ID3D11Buffer> m_pCorrectionConstants_HDR;
	CComPtr<ID3D11Buffer> m_pHDR10ToneMappingConstants_HDR;
	CComPtr<ID3D11Buffer> m_pDoviCurvesConstantBuffer;
	CComPtr<ID3D11Buffer> m_pPostScaleConstants;
	TexEx_t m_TexSrcVideo;
	Tex2D_t m_TexConvertOutput;
	Tex2D_t m_TexResize;
	Tex2DArray_t m_TexsPostScale;
	Tex2D_t m_TexDither;
	Tex2D_t m_TexAlphaBitmap;
	std::vector<ExternalPixelShader11_t> m_pPreScaleShaders;
	std::vector<ExternalPixelShader11_t> m_pPostScaleShaders;
	const wchar_t *m_strCorrection = nullptr;
	CD3D11VP m_D3D11VP;
	CD3D11VP_StreamInfo m_D3D11VP_StreamInfo;
	struct
	{
		bool bEnable = false;
		ID3D11Buffer *pVertexBuffer = nullptr;
		ID3D11Buffer *pConstants = nullptr;
		void Release()
		{
			bEnable = false;
			SAFE_RELEASE(pVertexBuffer);
			SAFE_RELEASE(pConstants);
		}
	} m_PSConvColorData;
	ExFormat_t m_srcExFmt;
	DXGI_SWAP_EFFECT m_UsedSwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	DXGI_COLOR_SPACE_TYPE m_currentSwapChainColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	DXGI_FORMAT m_srcDXGIFormat = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT m_D3D11OutputFmt = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT m_InternalTexFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
	DXGI_FORMAT m_SwapChainFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
	UINT32 m_DisplayBitsPerChannel = 8;
	D3D11_VIDEO_FRAME_FORMAT m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
	bool m_bIsFullscreen = false;
	int m_iVPSuperRes = SUPERRES_Disable;
	bool m_bVPUseSuperRes = false;
	bool m_bVPRTXVideoHDR = false;
	bool m_bVPUseRTXVideoHDR = false;
	UINT m_srcVideoTransferFunction = 0;
	bool m_bCallbackDeviceIsSet = false;
	bool m_bHdrPassthroughSupport;
	bool m_bHdrPassthrough;
	bool m_bHdrLocalToneMapping;
	bool m_bHdrSupport;
	float m_fHdrDisplayMaxNits;
	int m_iHdrLocalToneMappingType;
	float m_fHdrDynamicRangeCompression;
	float m_fHdrShadowDetail;
	float m_fHdrColorVolumeAdaptation;
	float m_fHdrSceneAdaptation;
	bool m_bHdrDisplaySwitching = false;
	bool m_bHdrDisplayModeEnabled = false;
	bool m_bHdrAllowSwitchDisplay = true;
	bool m_bACMEnabled = false;
	std::map<std::wstring, bool> m_hdrModeSavedState;
	std::map<std::wstring, bool, std::less<>> m_hdrModeStartState;
	HDRMetadata m_hdr10 = {};
	HDRMetadata m_lastHdr10 = {};
	UINT m_DoviMaxMasteringLuminance = 0;
	UINT m_DoviMinMasteringLuminance = 0;
	struct Alignment_t
	{
		TexEx_t texture;
		ColorFormat_t cformat = {};
		LONG cx = {};
	} m_Alignment;
	CComPtr<CDX11SubPicAllocator> m_pSubPicAllocator;
	bool m_bSubPicWasRendered = false;
	CComPtr<ID3D11Buffer> m_pAlphaBitmapVertex;
	CD3D11Font m_Font3D;
	CD3D11Rectangle m_StatsBackground;
	CD3D11Rectangle m_Rect3D;
	CD3D11Rectangle m_Underlay;
	CD3D11Lines m_Lines;
	CD3D11Lines m_SyncLine;
	CD3D11Rectangle d3d11rect;
	D3DCOLOR m_dwStatsTextColor = D3DCOLOR_XRGB(255, 255, 255);
	HMONITOR m_lastFullscreenHMonitor = nullptr;
};