﻿/*
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

// --- Core Includes from original .h ---
#include <DXGI1_5.h>
#include <dxva2api.h>
#include <strmif.h>
#include <map>
#include <vector>
// Assume these are required for shaders, helper functions, and DX11VP classes
#include "Shaders.h"
#include "DX11Helper.h"
#include "D3D11VP.h"
#include "IVideoRenderer.h"
#include "D3DUtil/D3D11Font.h"
#include "D3DUtil/D3D11Geometry.h"
#include "VideoProcessor.h"
#include "SubPic/DX11SubPic.h"
// --- End Core Includes ---

// --- Additional Includes identified from original .cpp ---
#include <uuids.h> // For GUIDs like IID_MediaSideDataHDR
#include <Mferror.h> // For MF error codes
#include <Mfidl.h>   // For MF interfaces like IMediaSample2, IMediaSideData
#include <dwmapi.h>  // For Dwm functions
#include <optional>  // For std::optional
#include "Helper.h"  // Project-specific helpers (e.g., GetDisplayConfig)
#include "Times.h"   // Project-specific time utilities
#include "resource.h"// For shader resource IDs (e.g., IDF_PS_11_SIMPLE)
#include "../Include/Version.h" // For VERSION_STR
#include "../Include/ID3DVideoMemoryConfiguration.h" // For related interfaces
// MinHook is used for function hooking
#include "../external/minhook/include/MinHook.h"
// --- End Additional Includes ---

#define TEST_SHADER 0

// Forward declaration for friend class
class CVideoRendererInputPin;

// Structs defined in header (or moved from .cpp)
struct HDRMetadata
{
	DXGI_HDR_METADATA_HDR10 hdr10 = {};
	bool bValid = false;
	uint64_t timestamp;
};

// Forward declarations for functions defined in the anonymous namespace below
namespace {
    bool ToggleHDR(const DisplayConfig_t &displayConfig, const bool bEnableAdvancedColor);
    static HRESULT CompileShader(const std::string &source, ID3DBlob **blob, const char *profile, ID3DBlob **errorBlob = nullptr); // Assuming this is in DX11Helper.h or similar, but making definition visible.
    static HRESULT GetDataFromResource(LPVOID &data, DWORD &size, UINT resid); // Assuming this is in DX11Helper.h
    static const ScalingShaderResId s_Upscaling11ResIDs[UPSCALE_COUNT];
    static const ScalingShaderResId s_Downscaling11ResIDs[DOWNSCALE_COUNT];
}

// Anonymous namespace for static/global helpers to avoid ODR violations
namespace {
    bool g_bPresent = false;
    bool g_bCreateSwapChain = false;

    typedef BOOL(WINAPI *pSetWindowPos)(
        _In_ HWND hWnd,
        _In_opt_ HWND hWndInsertAfter,
        _In_ int X,
        _In_ int Y,
        _In_ int cx,
        _In_ int cy,
        _In_ UINT uFlags);
    pSetWindowPos pOrigSetWindowPosDX11 = nullptr;
    static BOOL WINAPI pNewSetWindowPosDX11(
        _In_ HWND hWnd,
        _In_opt_ HWND hWndInsertAfter,
        _In_ int X,
        _In_ int Y,
        _In_ int cx,
        _In_ int cy,
        _In_ UINT uFlags)
    {
        if (g_bPresent)
        {
            DLog(L"call SetWindowPos() function during Present()");
            uFlags |= SWP_ASYNCWINDOWPOS;
        }
        return pOrigSetWindowPosDX11(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
    }
    typedef LONG(WINAPI *pSetWindowLongA)(
        _In_ HWND hWnd,
        _In_ int nIndex,
        _In_ LONG dwNewLong);
    pSetWindowLongA pOrigSetWindowLongADX11 = nullptr;
    static LONG WINAPI pNewSetWindowLongADX11(
        _In_ HWND hWnd,
        _In_ int nIndex,
        _In_ LONG dwNewLong)
    {
        if (g_bCreateSwapChain)
        {
            DLog(L"Blocking call SetWindowLongA() function during create fullscreen swap chain");
            return 0L;
        }
        return pOrigSetWindowLongADX11(hWnd, nIndex, dwNewLong);
    }
    template <typename T>
    inline bool HookFunc(T **ppSystemFunction, PVOID pHookFunction)
    {
        return MH_CreateHook(*ppSystemFunction, pHookFunction, reinterpret_cast<LPVOID *>(ppSystemFunction)) == MH_OK;
    }
    const UINT dither_size = 32;
    struct VERTEX
    {
        DirectX::XMFLOAT3 Pos;
        DirectX::XMFLOAT2 TexCoord;
    };
    struct PS_EXTSHADER_CONSTANTS
    {
        DirectX::XMFLOAT2 pxy;
        DirectX::XMFLOAT2 wh;
        uint32_t counter;
        float clock;
        float reserved1;
        float reserved2;
    };
    static_assert(sizeof(PS_EXTSHADER_CONSTANTS) % 16 == 0);

    // Helper functions from .cpp
    static void FillVertices(VERTEX (&Vertices)[4], const UINT srcW, const UINT srcH, const RECT &srcRect, const int iRotation, const bool bFlip)
    {
        const float src_dx = 1.0f / srcW;
        const float src_dy = 1.0f / srcH;
        float src_l = src_dx * srcRect.left;
        float src_r = src_dx * srcRect.right;
        const float src_t = src_dy * srcRect.top;
        const float src_b = src_dy * srcRect.bottom;
        POINT points[4];
        switch (iRotation)
        {
        case 90:
            points[0] = {-1, +1};
            points[1] = {+1, +1};
            points[2] = {-1, -1};
            points[3] = {+1, -1};
            break;
        case 180:
            points[0] = {+1, +1};
            points[1] = {+1, -1};
            points[2] = {-1, +1};
            points[3] = {-1, -1};
            break;
        case 270:
            points[0] = {+1, -1};
            points[1] = {-1, -1};
            points[2] = {+1, +1};
            points[3] = {-1, +1};
            break;
        default:
            points[0] = {-1, -1};
            points[1] = {-1, +1};
            points[2] = {+1, -1};
            points[3] = {+1, +1};
        }
        if (bFlip)
        {
            std::swap(src_l, src_r);
        }
        Vertices[0] = {{(float)points[0].x, (float)points[0].y, 0}, {src_l, src_b}};
        Vertices[1] = {{(float)points[1].x, (float)points[1].y, 0}, {src_l, src_t}};
        Vertices[2] = {{(float)points[2].x, (float)points[2].y, 0}, {src_r, src_b}};
        Vertices[3] = {{(float)points[3].x, (float)points[3].y, 0}, {src_r, src_t}};
    }
    static HRESULT CreateVertexBuffer(ID3D11Device *pDevice, ID3D11Buffer **ppVertexBuffer,
                                      const UINT srcW, const UINT srcH, const RECT &srcRect,
                                      const int iRotation, const bool bFlip)
    {
        ASSERT(ppVertexBuffer);
        ASSERT(*ppVertexBuffer == nullptr);
        VERTEX Vertices[4];
        FillVertices(Vertices, srcW, srcH, srcRect, iRotation, bFlip);
        D3D11_BUFFER_DESC BufferDesc = {sizeof(Vertices), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA InitData = {Vertices, 0, 0};
        HRESULT hr = pDevice->CreateBuffer(&BufferDesc, &InitData, ppVertexBuffer);
        DLogIf(FAILED(hr), L"CreateVertexBuffer() : CreateBuffer() failed with error {}", HR2Str(hr));
        return hr;
    }
    static HRESULT FillVertexBuffer(ID3D11DeviceContext *pDeviceContext, ID3D11Buffer *pVertexBuffer,
                                      const UINT srcW, const UINT srcH, const RECT &srcRect,
                                      const int iRotation, const bool bFlip)
    {
        ASSERT(pVertexBuffer);
        VERTEX Vertices[4];
        FillVertices(Vertices, srcW, srcH, srcRect, iRotation, bFlip);
        D3D11_MAPPED_SUBRESOURCE mr;
        HRESULT hr = pDeviceContext->Map(pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mr);
        if (FAILED(hr))
        {
            DLog(L"FillVertexBuffer() : Map() failed with error {}", HR2Str(hr));
            return hr;
        }
        memcpy(mr.pData, &Vertices, sizeof(Vertices));
        pDeviceContext->Unmap(pVertexBuffer, 0);
        return hr;
    }
    static void TextureBlt11(
        ID3D11DeviceContext *pDeviceContext,
        ID3D11RenderTargetView *pRenderTargetView, D3D11_VIEWPORT &viewport,
        ID3D11InputLayout *pInputLayout,
        ID3D11VertexShader *pVertexShader,
        ID3D11PixelShader *pPixelShader,
        ID3D11ShaderResourceView *pShaderResourceViews,
        ID3D11SamplerState *pSampler,
        ID3D11Buffer *pConstantBuffer,
        ID3D11Buffer *pVertexBuffer)
    {
        ASSERT(pDeviceContext);
        ASSERT(pRenderTargetView);
        const UINT Stride = sizeof(VERTEX);
        const UINT Offset = 0;
        pDeviceContext->IASetInputLayout(pInputLayout);
        pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
        pDeviceContext->RSSetViewports(1, &viewport);
        pDeviceContext->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
        pDeviceContext->VSSetShader(pVertexShader, nullptr, 0);
        pDeviceContext->PSSetShader(pPixelShader, nullptr, 0);
        pDeviceContext->PSSetShaderResources(0, 1, &pShaderResourceViews);
        pDeviceContext->PSSetSamplers(0, 1, &pSampler);
        pDeviceContext->PSSetConstantBuffers(0, 1, &pConstantBuffer);
        pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        pDeviceContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &Stride, &Offset);
        pDeviceContext->Draw(4, 0);
        ID3D11ShaderResourceView *views[1] = {};
        pDeviceContext->PSSetShaderResources(0, 1, views);
    }

    // Static shader ID arrays (copied from .cpp)
    static const ScalingShaderResId s_Upscaling11ResIDs[UPSCALE_COUNT] = {
        {0, 0, L"Nearest-neighbor"},
        {IDF_PS_11_INTERP_MITCHELL4_X, IDF_PS_11_INTERP_MITCHELL4_Y, L"Mitchell-Netravali"},
        {IDF_PS_11_INTERP_CATMULL4_X, IDF_PS_11_INTERP_CATMULL4_Y, L"Catmull-Rom"},
        {IDF_PS_11_INTERP_LANCZOS2_X, IDF_PS_11_INTERP_LANCZOS2_Y, L"Lanczos2"},
        {IDF_PS_11_INTERP_LANCZOS3_X, IDF_PS_11_INTERP_LANCZOS3_Y, L"Lanczos3"},
        {IDF_PS_11_INTERP_JINC2, IDF_PS_11_INTERP_JINC2, L"Jinc2m"},
    };
    static const ScalingShaderResId s_Downscaling11ResIDs[DOWNSCALE_COUNT] = {
        {IDF_PS_11_CONVOL_BOX_X, IDF_PS_11_CONVOL_BOX_Y, L"Box"},
        {IDF_PS_11_CONVOL_BILINEAR_X, IDF_PS_11_CONVOL_BILINEAR_Y, L"Bilinear"},
        {IDF_PS_11_CONVOL_HAMMING_X, IDF_PS_11_CONVOL_HAMMING_Y, L"Hamming"},
        {IDF_PS_11_CONVOL_BICUBIC05_X, IDF_PS_11_CONVOL_BICUBIC05_Y, L"Bicubic"},
        {IDF_PS_11_CONVOL_BICUBIC15_X, IDF_PS_11_CONVOL_BICUBIC15_Y, L"Bicubic sharp"},
        {IDF_PS_11_CONVOL_LANCZOS_X, IDF_PS_11_CONVOL_LANCZOS_Y, L"Lanczos"}};
} // namespace

// Helper function to toggle HDR (defined in .cpp, needed here)
static bool ToggleHDR(const DisplayConfig_t &displayConfig, const bool bEnableAdvancedColor)
{
	auto GetCurrentDisplayMode = [](LPCWSTR lpszDeviceName) -> std::optional<DEVMODEW>
	{
		DEVMODEW devmode = {};
		devmode.dmSize = sizeof(DEVMODEW);
		auto ret = EnumDisplaySettingsW(lpszDeviceName, ENUM_CURRENT_SETTINGS, &devmode);
		if (ret)
		{
			return devmode;
		}
		return {};
	};
	auto beforeModeOpt = GetCurrentDisplayMode(displayConfig.displayName);
	LONG ret = 1;
	if (IsWindows11_24H2OrGreater())
	{
		DISPLAYCONFIG_SET_HDR_STATE setHdrState = {};
		setHdrState.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(DISPLAYCONFIG_DEVICE_INFO_SET_HDR_STATE);
		setHdrState.header.size = sizeof(setHdrState);
		setHdrState.header.adapterId = displayConfig.modeTarget.adapterId;
		setHdrState.header.id = displayConfig.modeTarget.id;
		setHdrState.enableHdr = bEnableAdvancedColor ? 1 : 0;
		ret = DisplayConfigSetDeviceInfo(&setHdrState.header);
		DLogIf(ERROR_SUCCESS != ret, L"ToggleHDR() : DisplayConfigSetDeviceInfo(DISPLAYCONFIG_SET_HDR_STATE) with '{}' failed with error {}", bEnableAdvancedColor, HR2Str(HRESULT_FROM_WIN32(ret)));
	}
	else
	{
		DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setColorState = {};
		setColorState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
		setColorState.header.size = sizeof(setColorState);
		setColorState.header.adapterId = displayConfig.modeTarget.adapterId;
		setColorState.header.id = displayConfig.modeTarget.id;
		setColorState.enableAdvancedColor = bEnableAdvancedColor ? 1 : 0;
		ret = DisplayConfigSetDeviceInfo(&setColorState.header);
		DLogIf(ERROR_SUCCESS != ret, L"ToggleHDR() : DisplayConfigSetDeviceInfo(DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE) with '{}' failed with error {}", bEnableAdvancedColor, HR2Str(HRESULT_FROM_WIN32(ret)));
	}
	if (ret == ERROR_SUCCESS && beforeModeOpt.has_value())
	{
		auto afterModeOpt = GetCurrentDisplayMode(displayConfig.displayName);
		if (afterModeOpt.has_value())
		{
			auto &beforeMode = *beforeModeOpt;
			auto &afterMode = *afterModeOpt;
			if (beforeMode.dmPelsWidth != afterMode.dmPelsWidth || beforeMode.dmPelsHeight != afterMode.dmPelsHeight || beforeMode.dmBitsPerPel != afterMode.dmBitsPerPel || beforeMode.dmDisplayFrequency != afterMode.dmDisplayFrequency)
			{
				DLog(L"ToggleHDR() : Display mode changed from {}x{}@{} to {}x{}@{}, restoring", beforeMode.dmPelsWidth, beforeMode.dmPelsHeight, beforeMode.dmDisplayFrequency, afterMode.dmPelsWidth, afterMode.dmPelsHeight, afterMode.dmDisplayFrequency);
				auto ret_restore = ChangeDisplaySettingsExW(displayConfig.displayName, &beforeMode, nullptr, CDS_FULLSCREEN, nullptr);
				DLogIf(DISP_CHANGE_SUCCESSFUL != ret_restore, L"ToggleHDR() : ChangeDisplaySettingsExW() failed with error {}", HR2Str(HRESULT_FROM_WIN32(ret_restore)));
			}
		}
	}
	return ret == ERROR_SUCCESS;
}

// Placeholder/Assumption for CompileShader, usually from DX11Helper.h
// In a real scenario, you'd ensure these are correctly declared/defined.
// For this example, assuming it's available.
// static HRESULT CompileShader(const std::string& source, ID3DBlob** blob, const char* profile, ID3DBlob** errorBlob = nullptr) { return E_NOTIMPL; }

// Placeholder/Assumption for GetDataFromResource, usually from DX11Helper.h
// static HRESULT GetDataFromResource(LPVOID& data, DWORD& size, UINT resid) { return E_NOTIMPL; }


// --- Class Declaration ---
class CDX11VideoProcessor : public CVideoProcessor
{
	friend class CVideoRendererInputPin;

public:
	CDX11VideoProcessor(CMpcVideoRenderer *pFilter, const Settings_t &config, HRESULT &hr);
	~CDX11VideoProcessor() override;

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
	HRESULT ConvertColorPass(ID3D11Texture2D *pRenderTargetTex);
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

// --- Function Definitions from original .cpp ---
// Note: These must be defined *outside* the class body but within the header.

// Helper for shader compilation (assuming it's defined in DX11Helper.h or needs to be here)
// If CompileShader is truly external and not included via DX11Helper, add its definition.
// For now, assuming it's available via DX11Helper.h.
// If not, uncomment and define a placeholder/actual implementation here.
/*
static HRESULT CompileShader(const std::string& source, ID3DBlob** blob, const char* profile, ID3DBlob** errorBlob = nullptr) {
    // Implementation would go here, likely involving D3DCompile
    return E_NOTIMPL; // Placeholder
}
*/

// Helper to get data from resource (assuming it's defined in DX11Helper.h or needs to be here)
// If GetDataFromResource is truly external and not included via DX11Helper, add its definition.
// For now, assuming it's available via DX11Helper.h.
// If not, uncomment and define a placeholder/actual implementation here.
/*
static HRESULT GetDataFromResource(LPVOID& data, DWORD& size, UINT resid) {
    return E_NOTIMPL; // Placeholder
}
*/

HRESULT CDX11VideoProcessor::TextureCopyRect(
    const Tex2D_t &Tex, ID3D11Texture2D *pRenderTarget,
    const CRect &srcRect, const CRect &destRect,
    ID3D11PixelShader *pPixelShader, ID3D11Buffer *pConstantBuffer,
    const int iRotation, const bool bFlip)
{
    CComPtr<ID3D11RenderTargetView> pRenderTargetView;
    HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
    if (FAILED(hr))
    {
        DLog(L"TextureCopyRect() : CreateRenderTargetView() failed: {}", HR2Str(hr));
        return hr;
    }
    hr = FillVertexBuffer(m_pDeviceContext, m_pVertexBuffer, Tex.desc.Width, Tex.desc.Height, srcRect, iRotation, bFlip);
    if (FAILED(hr))
        return hr;
    D3D11_VIEWPORT VP;
    VP.TopLeftX = (FLOAT)destRect.left;
    VP.TopLeftY = (FLOAT)destRect.top;
    VP.Width = (FLOAT)destRect.Width();
    VP.Height = (FLOAT)destRect.Height();
    VP.MinDepth = 0.0f;
    VP.MaxDepth = 1.0f;
    TextureBlt11(m_pDeviceContext, pRenderTargetView, VP, m_pVSimpleInputLayout, m_pVS_Simple, pPixelShader, Tex.pShaderResource, m_pSamplerPoint, pConstantBuffer, m_pVertexBuffer);
    return hr;
}
HRESULT CDX11VideoProcessor::TextureResizeShader(
    const Tex2D_t &Tex, ID3D11Texture2D *pRenderTarget,
    const CRect &srcRect, const CRect &destRect,
    ID3D11PixelShader *pPixelShader,
    const int iRotation, const bool bFlip)
{
    CComPtr<ID3D11RenderTargetView> pRenderTargetView;
    HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
    if (FAILED(hr))
    {
        DLog(L"CDX11VideoProcessor::TextureResizeShader() : CreateRenderTargetView() failed with error {}", HR2Str(hr));
        return hr;
    }
    hr = FillVertexBuffer(m_pDeviceContext, m_pVertexBuffer, Tex.desc.Width, Tex.desc.Height, srcRect, iRotation, bFlip);
    if (FAILED(hr))
    {
        return hr;
    }
    const FLOAT constants[][4] = {
        {(float)Tex.desc.Width, (float)Tex.desc.Height, 1.0f / Tex.desc.Width, 1.0f / Tex.desc.Height},
        {(float)srcRect.Width() / destRect.Width(), (float)srcRect.Height() / destRect.Height(), 0, 0}};
    D3D11_MAPPED_SUBRESOURCE mr;
    hr = m_pDeviceContext->Map(m_pResizeShaderConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mr);
    if (FAILED(hr))
    {
        DLog(L"CDX11VideoProcessor::TextureResizeShader() : Map() failed with error {}", HR2Str(hr));
        return hr;
    }
    memcpy(mr.pData, &constants, sizeof(constants));
    m_pDeviceContext->Unmap(m_pResizeShaderConstantBuffer, 0);
    D3D11_VIEWPORT VP;
    VP.TopLeftX = (FLOAT)destRect.left;
    VP.TopLeftY = (FLOAT)destRect.top;
    VP.Width = (FLOAT)destRect.Width();
    VP.Height = (FLOAT)destRect.Height();
    VP.MinDepth = 0.0f;
    VP.MaxDepth = 1.0f;
    TextureBlt11(m_pDeviceContext, pRenderTargetView, VP, m_pVSimpleInputLayout, m_pVS_Simple, pPixelShader, Tex.pShaderResource, m_pSamplerPoint, m_pResizeShaderConstantBuffer, m_pVertexBuffer);
    return hr;
}
void CDX11VideoProcessor::SetCallbackDevice()
{
	if (!m_bCallbackDeviceIsSet && m_pDevice && m_pFilter->m_pSub11CallBack)
	{
		m_bCallbackDeviceIsSet = SUCCEEDED(m_pFilter->m_pSub11CallBack->SetDevice11(m_pDevice));
	}
}
CDX11VideoProcessor::CDX11VideoProcessor(CMpcVideoRenderer *pFilter, const Settings_t &config, HRESULT &hr)
	: CVideoProcessor(pFilter)
{
	m_srcExFmt = {}; // Initialize struct
	m_bShowStats = config.bShowStats;
	m_iResizeStats = config.iResizeStats;
	m_iTexFormat = config.iTexFormat;
	m_VPFormats = config.VPFmts;
	m_bDeintDouble = config.bDeintDouble;
	m_bVPScaling = config.bVPScaling;
	m_iChromaScaling = config.iChromaScaling;
	m_iUpscaling = config.iUpscaling;
	m_iDownscaling = config.iDownscaling;
	m_bInterpolateAt50pct = config.bInterpolateAt50pct;
	m_bUseDither = config.bUseDither;
	m_bDeintBlend = config.bDeintBlend;
	m_iSwapEffect = config.iSwapEffect;
	m_bVBlankBeforePresent = config.bVBlankBeforePresent;
	m_bAdjustPresentTime = config.bAdjustPresentTime;
	m_bHdrPreferDoVi = config.bHdrPreferDoVi;
	m_bHdrPassthrough = config.bHdrPassthrough;
	m_bHdrLocalToneMapping = config.bHdrLocalToneMapping;
	m_iHdrLocalToneMappingType = config.iHdrLocalToneMappingType;
	m_fHdrDisplayMaxNits = config.fHdrDisplayMaxNits;
	m_fHdrDynamicRangeCompression = config.fHdrDynamicRangeCompression;
	m_fHdrShadowDetail = config.fHdrShadowDetail;
	m_fHdrColorVolumeAdaptation = config.fHdrColorVolumeAdaptation;
	m_fHdrSceneAdaptation = config.fHdrSceneAdaptation;
	m_iHdrToggleDisplay = config.iHdrToggleDisplay;
	m_iHdrOsdBrightness = config.iHdrOsdBrightness;
	m_bConvertToSdr = config.bConvertToSdr;
	m_iSDRDisplayNits = config.iSDRDisplayNits;
	m_bVPRTXVideoHDR = config.bVPRTXVideoHDR;
	m_iVPSuperRes = config.iVPSuperRes;
	m_nCurrentAdapter = -1;
	hr = CreateDXGIFactory1(IID_IDXGIFactory1, (void **)&m_pDXGIFactory1);
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::CDX11VideoProcessor() : CreateDXGIFactory1() failed with error {}", HR2Str(hr));
		return;
	}
	SetDefaultDXVA2ProcAmpRanges(m_DXVA2ProcAmpRanges);
	SetDefaultDXVA2ProcAmpValues(m_DXVA2ProcAmpValues);
	pOrigSetWindowPosDX11 = SetWindowPos;
	auto ret = HookFunc(&pOrigSetWindowPosDX11, pNewSetWindowPosDX11);
	DLogIf(!ret, L"CDX11VideoProcessor::CDX11VideoProcessor() : hook for SetWindowPos() fail");
	pOrigSetWindowLongADX11 = SetWindowLongA;
	ret = HookFunc(&pOrigSetWindowLongADX11, pNewSetWindowLongADX11);
	DLogIf(!ret, L"CDX11VideoProcessor::CDX11VideoProcessor() : hook for SetWindowLongA() fail");
	MH_EnableHook(MH_ALL_HOOKS);
	CComPtr<IDXGIAdapter> pDXGIAdapter;
	for (UINT adapter = 0; m_pDXGIFactory1->EnumAdapters(adapter, &pDXGIAdapter) != DXGI_ERROR_NOT_FOUND; ++adapter)
	{
		CComPtr<IDXGIOutput> pDXGIOutput;
		for (UINT output = 0; pDXGIAdapter->EnumOutputs(output, &pDXGIOutput) != DXGI_ERROR_NOT_FOUND; ++output)
		{
			DXGI_OUTPUT_DESC desc{};
			if (SUCCEEDED(pDXGIOutput->GetDesc(&desc)))
			{
				DisplayConfig_t displayConfig = {};
				if (GetDisplayConfig(desc.DeviceName, displayConfig))
				{
					m_hdrModeStartState[desc.DeviceName] = displayConfig.HDREnabled();
				}
			}
			pDXGIOutput.Release();
		}
		pDXGIAdapter.Release();
	}
}
void CDX11VideoProcessor::SetHDR10ShaderParams(float masteringMinLuminanceNits, float masteringMaxLuminanceNits, float maxCLL, float maxFALL, float displayMaxNits, int toneMappingType, float dynamicRangeCompression, float shadowDetail, float colorVolumeAdaptation, float sceneAdaptation)
{
	if (masteringMinLuminanceNits <= 0)
		masteringMinLuminanceNits = 0;
	if (masteringMaxLuminanceNits <= 0)
		masteringMaxLuminanceNits = 1000.0f;
	if (maxCLL <= 0)
		maxCLL = masteringMaxLuminanceNits;
	if (maxFALL <= 0)
		maxFALL = maxCLL;
	if (displayMaxNits <= 0 || displayMaxNits > 10000.0)
		displayMaxNits = 600.0f;
	if (toneMappingType < 1 || toneMappingType > 4)
		toneMappingType = 1;
	dynamicRangeCompression = std::clamp(dynamicRangeCompression, 0.0f, 1.0f);
	shadowDetail = std::clamp(shadowDetail, 0.1f, 2.0f);
	colorVolumeAdaptation = std::clamp(colorVolumeAdaptation, 0.0f, 1.0f);
	sceneAdaptation = std::clamp(sceneAdaptation, 0.0f, 1.0f);
	float shadowGamma = 1.0f / shadowDetail;
	const FLOAT cbuffer[12] = {
		masteringMinLuminanceNits, masteringMaxLuminanceNits, maxCLL, maxFALL,
		displayMaxNits, (float)toneMappingType, dynamicRangeCompression, shadowGamma,
		colorVolumeAdaptation, sceneAdaptation, 0.0f, 0.0f};
	if (m_pHDR10ToneMappingConstants_HDR)
	{
		m_pDeviceContext->UpdateSubresource(m_pHDR10ToneMappingConstants_HDR, 0, nullptr, &cbuffer, 0, 0);
	}
	else
	{
		D3D11_BUFFER_DESC BufferDesc = {
			.ByteWidth = sizeof(cbuffer),
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
		};
		D3D11_SUBRESOURCE_DATA InitData = {&cbuffer, 0, 0};
		HRESULT result = m_pDevice->CreateBuffer(&BufferDesc, &InitData, &m_pHDR10ToneMappingConstants_HDR);
		if (FAILED(result))
		{
			DLog(L"SetHDR10ShaderParams() failed to create m_pHDR10ToneMappingConstants_HDR. Error: {}", HR2Str(result));
		}
	}
}
void CDX11VideoProcessor::SetShaderLuminanceParams(ShaderLuminanceParams_t &params)
{
	FLOAT cbuffer[4] = {10000.0f / m_iSDRDisplayNits, 0, 0, 0};
	if (m_pCorrectionConstants_HDR)
	{
		m_pDeviceContext->UpdateSubresource(m_pCorrectionConstants_HDR, 0, nullptr, &cbuffer, 0, 0);
	}
	else
	{
		D3D11_BUFFER_DESC BufferDesc = {
			.ByteWidth = sizeof(cbuffer),
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_CONSTANT_BUFFER};
		D3D11_SUBRESOURCE_DATA InitData = {&cbuffer, 0, 0};
		EXECUTE_ASSERT(S_OK == m_pDevice->CreateBuffer(&BufferDesc, &InitData, &m_pCorrectionConstants_HDR));
	}
}
HRESULT CDX11VideoProcessor::SetShaderDoviCurvesPoly()
{
	ASSERT(m_Dovi.bValid);
	PS_DOVI_POLY_CURVE polyCurves[3] = {};
	const float scale = 1.0f / ((1 << m_Dovi.msd.Header.bl_bit_depth) - 1);
	const float scale_coef = 1.0f / (1u << m_Dovi.msd.Header.coef_log2_denom);
	for (int c = 0; c < 3; c++)
	{
		const auto &curve = m_Dovi.msd.Mapping.curves[c];
		auto &out = polyCurves[c];
		const int num_coef = curve.num_pivots - 1;
		bool has_poly = false;
		bool has_mmr = false;
		for (int i = 0; i < num_coef; i++)
		{
			switch (curve.mapping_idc[i])
			{
			case 0: // polynomial
				has_poly = true;
				out.coeffs_data[i].x = scale_coef * curve.poly_coef[i][0];
				out.coeffs_data[i].y = (curve.poly_order[i] >= 1) ? scale_coef * curve.poly_coef[i][1] : 0.0f;
				out.coeffs_data[i].z = (curve.poly_order[i] >= 2) ? scale_coef * curve.poly_coef[i][2] : 0.0f;
				out.coeffs_data[i].w = 0.0f; // order=0 signals polynomial
				break;
			case 1: // mmr
				has_mmr = true;
				out.coeffs_data[i].x = 0.0f;
				out.coeffs_data[i].y = 1.0f;
				out.coeffs_data[i].z = 0.0f;
				out.coeffs_data[i].w = 0.0f;
				break;
			}
		}
		const int n = curve.num_pivots - 2;
		for (int i = 0; i < n; i++)
		{
			out.pivots_data[i].x = scale * curve.pivots[i + 1];
		}
		for (int i = n; i < 7; i++)
		{
			out.pivots_data[i].x = 1e9f;
		}
	}
	HRESULT hr;
	if (m_pDoviCurvesConstantBuffer)
	{
		D3D11_MAPPED_SUBRESOURCE mr;
		hr = m_pDeviceContext->Map(m_pDoviCurvesConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mr);
		if (SUCCEEDED(hr))
		{
			memcpy(mr.pData, &polyCurves, sizeof(polyCurves));
			m_pDeviceContext->Unmap(m_pDoviCurvesConstantBuffer, 0);
		}
	}
	else
	{
		D3D11_BUFFER_DESC BufferDesc = {sizeof(polyCurves), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
		D3D11_SUBRESOURCE_DATA InitData = {&polyCurves, 0, 0};
		hr = m_pDevice->CreateBuffer(&BufferDesc, &InitData, &m_pDoviCurvesConstantBuffer);
	}
	return hr;
}
HRESULT CDX11VideoProcessor::SetShaderDoviCurves()
{
	ASSERT(m_Dovi.bValid);
	PS_DOVI_CURVE cbuffer[3] = {};
	for (int c = 0; c < 3; c++)
	{
		const auto &curve = m_Dovi.msd.Mapping.curves[c];
		auto &out = cbuffer[c];
		bool has_poly = false, has_mmr = false, mmr_single = true;
		uint32_t mmr_idx = 0, min_order = 3, max_order = 1;
		const float scale_coef = 1.0f / (1 << m_Dovi.msd.Header.coef_log2_denom);
		const int num_coef = curve.num_pivots - 1;
		for (int i = 0; i < num_coef; i++)
		{
			switch (curve.mapping_idc[i])
			{
			case 0: // polynomial
				has_poly = true;
				out.coeffs_data[i].x = scale_coef * curve.poly_coef[i][0];
				out.coeffs_data[i].y = (curve.poly_order[i] >= 1) ? scale_coef * curve.poly_coef[i][1] : 0.0f;
				out.coeffs_data[i].z = (curve.poly_order[i] >= 2) ? scale_coef * curve.poly_coef[i][2] : 0.0f;
				out.coeffs_data[i].w = 0.0f; // order=0 signals polynomial
				break;
			case 1: // mmr
				min_order = std::min<int>(min_order, curve.mmr_order[i]);
				max_order = std::max<int>(max_order, curve.mmr_order[i]);
				mmr_single = !has_mmr;
				has_mmr = true;
				out.coeffs_data[i].x = scale_coef * curve.mmr_constant[i];
				out.coeffs_data[i].y = static_cast<float>(mmr_idx);
				out.coeffs_data[i].w = static_cast<float>(curve.mmr_order[i]);
				for (int j = 0; j < curve.mmr_order[i]; j++)
				{
					out.mmr_data[mmr_idx].x = scale_coef * curve.mmr_coef[i][j][0];
					out.mmr_data[mmr_idx].y = scale_coef * curve.mmr_coef[i][j][1];
					out.mmr_data[mmr_idx].z = scale_coef * curve.mmr_coef[i][j][2];
					out.mmr_data[mmr_idx].w = 0.0f; // unused
					mmr_idx++;
					out.mmr_data[mmr_idx].x = scale_coef * curve.mmr_coef[i][j][3];
					out.mmr_data[mmr_idx].y = scale_coef * curve.mmr_coef[i][j][4];
					out.mmr_data[mmr_idx].z = scale_coef * curve.mmr_coef[i][j][5];
					out.mmr_data[mmr_idx].w = scale_coef * curve.mmr_coef[i][j][6];
					mmr_idx++;
				}
				break;
			}
		}
		const float scale = 1.0f / ((1 << m_Dovi.msd.Header.bl_bit_depth) - 1);
		const int n = curve.num_pivots - 2;
		for (int i = 0; i < n; i++)
		{
			out.pivots_data[i].x = scale * curve.pivots[i + 1];
		}
		for (int i = n; i < 7; i++)
		{
			out.pivots_data[i].x = 1e9f;
		}
		if (has_poly)
		{
			out.params.methods = PS_RESHAPE_POLY;
		}
		if (has_mmr)
		{
			out.params.methods |= PS_RESHAPE_MMR;
			out.params.mmr_single = mmr_single;
			out.params.min_order = min_order;
			out.params.max_order = max_order;
		}
	}
	HRESULT hr;
	if (m_pDoviCurvesConstantBuffer)
	{
		D3D11_MAPPED_SUBRESOURCE mr;
		hr = m_pDeviceContext->Map(m_pDoviCurvesConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mr);
		if (SUCCEEDED(hr))
		{
			memcpy(mr.pData, &cbuffer, sizeof(cbuffer));
			m_pDeviceContext->Unmap(m_pDoviCurvesConstantBuffer, 0);
		}
	}
	else
	{
		D3D11_BUFFER_DESC BufferDesc = {sizeof(cbuffer), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
		D3D11_SUBRESOURCE_DATA InitData = {&cbuffer, 0, 0};
		hr = m_pDevice->CreateBuffer(&BufferDesc, &InitData, &m_pDoviCurvesConstantBuffer);
	}
	return hr;
}
void CDX11VideoProcessor::UpdateTexParams(int cdepth)
{
	switch (m_iTexFormat)
	{
	case TEXFMT_AUTOINT:
		m_InternalTexFmt = (cdepth > 8 || m_bVPUseRTXVideoHDR) ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
		break;
	case TEXFMT_8INT:
		m_InternalTexFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
		break;
	case TEXFMT_10INT:
		m_InternalTexFmt = DXGI_FORMAT_R10G10B10A2_UNORM;
		break;
	case TEXFMT_16FLOAT:
		m_InternalTexFmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
		break;
	default:
		ASSERT(FALSE);
	}
}
void CDX11VideoProcessor::UpdateRenderRect()
{
	m_renderRect.IntersectRect(m_videoRect, m_windowRect);
	UpdateScalingStrings();
}
void CDX11VideoProcessor::UpdateScalingStrings()
{
	const int w2 = m_videoRect.Width();
	const int h2 = m_videoRect.Height();
	const int k = m_bInterpolateAt50pct ? 2 : 1;
	int w1, h1;
	if (m_iRotation == 90 || m_iRotation == 270)
	{
		w1 = m_srcRectHeight;
		h1 = m_srcRectWidth;
	}
	else
	{
		w1 = m_srcRectWidth;
		h1 = m_srcRectHeight;
	}
	m_strShaderX = (w1 == w2) ? nullptr
				   : (w1 > k * w2)
					   ? s_Downscaling11ResIDs[m_iDownscaling].description
					   : s_Upscaling11ResIDs[m_iUpscaling].description;
	m_strShaderY = (h1 == h2) ? nullptr
				   : (h1 > k * h2)
					   ? s_Downscaling11ResIDs[m_iDownscaling].description
					   : s_Upscaling11ResIDs[m_iUpscaling].description;
}
void CDX11VideoProcessor::CalcStatsParams()
{
	if (m_pDeviceContext && !m_windowRect.IsRectEmpty())
	{
		SIZE rtSize = m_windowRect.Size();
		if (S_OK == m_Font3D.CreateFontBitmap(L"Consolas", m_StatsFontH, 0))
		{
			SIZE charSize = m_Font3D.GetMaxCharMetric();
			m_StatsRect.right = m_StatsRect.left + 61 * charSize.cx + 5 + 3;
			m_StatsRect.bottom = m_StatsRect.top + 18 * charSize.cy + 5 + 3;
		}
		m_StatsBackground.Set(m_StatsRect, rtSize, D3DCOLOR_ARGB(80, 0, 0, 0));
		CalcGraphParams();
		m_Underlay.Set(m_GraphRect, rtSize, D3DCOLOR_ARGB(80, 0, 0, 0));
		m_Lines.ClearPoints(rtSize);
		POINT points[2];
		const int linestep = 20 * m_Yscale;
		for (int y = m_GraphRect.top + (m_Yaxis - m_GraphRect.top) % (linestep); y < m_GraphRect.bottom; y += linestep)
		{
			points[0] = {m_GraphRect.left, y};
			points[1] = {m_GraphRect.right, y};
			m_Lines.AddPoints(points, std::size(points), (y == m_Yaxis) ? D3DCOLOR_XRGB(150, 150, 255) : D3DCOLOR_XRGB(100, 100, 255));
		}
		m_Lines.UpdateVertexBuffer();
	}
}
HRESULT CDX11VideoProcessor::MemCopyToTexSrcVideo(const BYTE *srcData, const int srcPitch)
{
	HRESULT hr = S_FALSE;
	D3D11_MAPPED_SUBRESOURCE mappedResource = {};

	if (m_TexSrcVideo.pTexture)
	{
		hr = m_pDeviceContext->Map(m_TexSrcVideo.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (SUCCEEDED(hr))
		{
			const BYTE *src = (srcPitch < 0) ? srcData + srcPitch * (1 - (int)m_srcLines) : srcData;
			m_pCopyPlaneFn(m_srcLines, (BYTE *)mappedResource.pData, mappedResource.RowPitch, src, srcPitch);
			m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture, 0);
		}
	}

	if (m_TexSrcVideo.pTexture2)
	{
		hr = m_pDeviceContext->Map(m_TexSrcVideo.pTexture2, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (SUCCEEDED(hr))
		{
			const UINT cromaH = m_srcHeight / m_srcParams.pDX11Planes->div_chroma_h;
			const int cromaPitch = (m_TexSrcVideo.pTexture3) ? srcPitch / m_srcParams.pDX11Planes->div_chroma_w : srcPitch;
			srcData += abs(srcPitch) * m_srcHeight;
			m_pCopyPlaneFn(cromaH, (BYTE *)mappedResource.pData, mappedResource.RowPitch, srcData, cromaPitch);
			m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture2, 0);

			if (m_TexSrcVideo.pTexture3)
			{
				hr = m_pDeviceContext->Map(m_TexSrcVideo.pTexture3, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
				if (SUCCEEDED(hr))
				{
					srcData += cromaPitch * cromaH;
					m_pCopyPlaneFn(cromaH, (BYTE *)mappedResource.pData, mappedResource.RowPitch, srcData, cromaPitch);
					m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture3, 0);
				}
			}
		}
	}

	return hr;
}
void CDX11VideoProcessor::ReleaseVP()
{
	DLog(L"CDX11VideoProcessor::ReleaseVP()");
	m_pFilter->ResetStreamingTimes2();
	m_RenderStats.Reset();
	if (m_pDeviceContext)
	{
		m_pDeviceContext->ClearState();
	}
	m_TexSrcVideo.Release();
	m_TexConvertOutput.Release();
	m_TexResize.Release();
	m_TexsPostScale.Release();
	m_PSConvColorData.Release();
	m_pDoviCurvesConstantBuffer.Release();
	m_D3D11VP.ReleaseVideoProcessor();
	m_strCorrection = nullptr;
	m_srcParams = {};
	m_srcDXGIFormat = DXGI_FORMAT_UNKNOWN;
	m_pCopyPlaneFn = CopyPlaneAsIs;
	m_srcWidth = 0;
	m_srcHeight = 0;
}
void CDX11VideoProcessor::ReleaseDevice()
{
	DLog(L"CDX11VideoProcessor::ReleaseDevice()");
	ReleaseVP();
	m_D3D11VP.ReleaseVideoDevice();
	m_StatsBackground.InvalidateDeviceObjects();
	m_Font3D.InvalidateDeviceObjects();
	m_Rect3D.InvalidateDeviceObjects();
	m_Underlay.InvalidateDeviceObjects();
	m_Lines.InvalidateDeviceObjects();
	m_SyncLine.InvalidateDeviceObjects();
	m_TexDither.Release();
	m_bAlphaBitmapEnable = false;
	m_pAlphaBitmapVertex.Release();
	m_TexAlphaBitmap.Release();
	ClearPreScaleShaders();
	ClearPostScaleShaders();
	m_pPSCorrection_HDR.Release();
	m_pPSHDR10ToneMapping_HDR.Release();
	m_pPSConvertColor.Release();
	m_pPSConvertColorDeint.Release();
	m_pShaderUpscaleX.Release();
	m_pShaderUpscaleY.Release();
	m_pShaderDownscaleX.Release();
	m_pShaderDownscaleY.Release();
	m_strShaderX = nullptr;
	m_strShaderY = nullptr;
	m_pPSFinalPass.Release();
	m_pCorrectionConstants_HDR.Release();
	m_pHDR10ToneMappingConstants_HDR.Release();
	m_pPostScaleConstants.Release();
#if TEST_SHADER
	m_pPS_TEST.Release();
#endif
	m_pVSimpleInputLayout.Release();
	m_pVS_Simple.Release();
	m_pPS_Simple.Release();
	m_pPS_BitmapToFrame.Release();
	m_pSamplerPoint.Release();
	m_pSamplerLinear.Release();
	m_pSamplerDither.Release();
	m_pAlphaBlendState.Release();
	m_Alignment.texture.Release();
	m_Alignment.cformat = {};
	m_Alignment.cx = {};
	if (m_pDeviceContext)
	{
		m_pDeviceContext->Flush();
	}
	m_pDeviceContext.Release();
	m_bCallbackDeviceIsSet = false;
#if (1 && _DEBUG)
	if (m_pDevice)
	{
		ID3D11Debug *pDebugDevice = nullptr;
		HRESULT hr2 = m_pDevice->QueryInterface(IID_PPV_ARGS(&pDebugDevice));
		if (S_OK == hr2)
		{
			hr2 = pDebugDevice->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
			ASSERT(S_OK == hr2);
		}
		SAFE_RELEASE(pDebugDevice);
	}
#endif
	m_pVertexBuffer.Release();
	m_pResizeShaderConstantBuffer.Release();
	m_pHalfOUtoInterlaceConstantBuffer.Release();
	m_pFinalPassConstantBuffer.Release();
	m_pDevice.Release();
}
void CDX11VideoProcessor::ReleaseSwapChain()
{
	if (m_pDXGISwapChain1)
	{
		m_pDXGISwapChain1->SetFullscreenState(FALSE, nullptr);
	}
	m_pDXGIOutput.Release();
	m_pDXGISwapChain4.Release();
	m_pDXGISwapChain1.Release();
}
void CDX11VideoProcessor::UpdateSubPic()
{
	ASSERT(m_pDevice);
	if (m_pFilter->m_pSubPicProvider)
	{
		if (m_pSubPicAllocator)
		{
			m_pSubPicAllocator->ChangeDevice(m_pDevice);
		}
		if (m_pFilter->m_pSubPicQueue)
		{
			m_pFilter->m_pSubPicQueue->Invalidate();
			m_pFilter->m_pSubPicQueue->SetSubPicProvider(m_pFilter->m_pSubPicProvider);
		}
	}
}
HRESULT CDX11VideoProcessor::CreatePShaderFromResource(ID3D11PixelShader **ppPixelShader, UINT resid)
{
	if (!m_pDevice || !ppPixelShader)
	{
		return E_POINTER;
	}
	LPVOID data;
	DWORD size;
	HRESULT hr = GetDataFromResource(data, size, resid);
	if (FAILED(hr))
	{
		return hr;
	}
	return m_pDevice->CreatePixelShader(data, size, nullptr, ppPixelShader);
}
HRESULT CDX11VideoProcessor::SetDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext)
{
	DLog(L"CDX11VideoProcessor::SetDevice()");
	ReleaseSwapChain();
	m_pDXGIFactory2.Release();
	ReleaseDevice();
	CheckPointer(pDevice, E_POINTER);
	HRESULT hr = pDevice->QueryInterface(IID_PPV_ARGS(&m_pDevice));
	if (FAILED(hr))
	{
		return hr;
	}
	if (pContext)
	{
		hr = pContext->QueryInterface(IID_PPV_ARGS(&m_pDeviceContext));
		if (FAILED(hr))
		{
			return hr;
		}
	}
	else
	{
		m_pDevice->GetImmediateContext1(&m_pDeviceContext);
	}
	CComQIPtr<ID3D10Multithread> pMultithread(m_pDeviceContext);
	pMultithread->SetMultithreadProtected(TRUE);
	CComPtr<IDXGIDevice> pDXGIDevice;
	hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice));
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::SetDevice() : QueryInterface(IDXGIDevice) failed with error {}", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}
	CComPtr<IDXGIAdapter> pDXGIAdapter;
	hr = pDXGIDevice->GetAdapter(&pDXGIAdapter);
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::SetDevice() : GetAdapter(IDXGIAdapter) failed with error {}", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}
	DXGI_ADAPTER_DESC dxgiAdapterDesc = {};
	hr = pDXGIAdapter->GetDesc(&dxgiAdapterDesc);
	if (SUCCEEDED(hr))
	{
		m_VendorId = dxgiAdapterDesc.VendorId;
		m_strAdapterDescription = std::format(L"{} ({:04X}:{:04X})", dxgiAdapterDesc.Description, dxgiAdapterDesc.VendorId, dxgiAdapterDesc.DeviceId);
		DLog(L"Graphics DXGI adapter: {}", m_strAdapterDescription);
	}
	HRESULT hr2 = m_D3D11VP.InitVideoDevice(m_pDevice, m_pDeviceContext, m_VendorId);
	DLogIf(FAILED(hr2), L"CDX11VideoProcessor::SetDevice() : InitVideoDevice failed with error {}", HR2Str(hr2));
	D3D11_SAMPLER_DESC SampDesc = {};
	SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SampDesc.MinLOD = 0;
	SampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateSamplerState(&SampDesc, &m_pSamplerPoint));
	SampDesc.Filter = D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT; // linear interpolation for magnification
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateSamplerState(&SampDesc, &m_pSamplerLinear));
	SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateSamplerState(&SampDesc, &m_pSamplerDither));
	D3D11_BLEND_DESC bdesc = {};
	bdesc.RenderTarget[0].BlendEnable = TRUE;
	bdesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	bdesc.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_ALPHA;
	bdesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	bdesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	bdesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	bdesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	bdesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateBlendState(&bdesc, &m_pAlphaBlendState));
	LPVOID data;
	DWORD size;
	EXECUTE_ASSERT(S_OK == GetDataFromResource(data, size, IDF_VS_11_SIMPLE));
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateVertexShader(data, size, nullptr, &m_pVS_Simple));
	D3D11_INPUT_ELEMENT_DESC Layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateInputLayout(Layout, std::size(Layout), data, size, &m_pVSimpleInputLayout));
	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPS_Simple, IDF_PS_11_SIMPLE));
	D3D11_BUFFER_DESC BufferDesc = {sizeof(VERTEX) * 4, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateBuffer(&BufferDesc, nullptr, &m_pVertexBuffer));
	BufferDesc = {sizeof(FLOAT) * 4 * 2, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateBuffer(&BufferDesc, nullptr, &m_pResizeShaderConstantBuffer));
	BufferDesc = {sizeof(FLOAT) * 4, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateBuffer(&BufferDesc, nullptr, &m_pHalfOUtoInterlaceConstantBuffer));
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateBuffer(&BufferDesc, nullptr, &m_pFinalPassConstantBuffer));
	BufferDesc = {sizeof(PS_EXTSHADER_CONSTANTS), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateBuffer(&BufferDesc, nullptr, &m_pPostScaleConstants));
	CComPtr<IDXGIFactory1> pDXGIFactory1;
	hr = pDXGIAdapter->GetParent(IID_PPV_ARGS(&pDXGIFactory1));
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::SetDevice() : GetParent(IDXGIFactory1) failed with error {}", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}
	hr = pDXGIFactory1->QueryInterface(IID_PPV_ARGS(&m_pDXGIFactory2));
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::SetDevice() : QueryInterface(IDXGIFactory2) failed with error {}", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}
	HRESULT hr3 = m_Font3D.InitDeviceObjects(m_pDevice, m_pDeviceContext);
	DLogIf(FAILED(hr3), L"m_Font3D.InitDeviceObjects() failed with error {}", HR2Str(hr3));
	if (SUCCEEDED(hr3))
	{
		hr3 = m_StatsBackground.InitDeviceObjects(m_pDevice, m_pDeviceContext);
		hr3 = m_Rect3D.InitDeviceObjects(m_pDevice, m_pDeviceContext);
		hr3 = m_Underlay.InitDeviceObjects(m_pDevice, m_pDeviceContext);
		hr3 = m_Lines.InitDeviceObjects(m_pDevice, m_pDeviceContext);
		hr3 = m_SyncLine.InitDeviceObjects(m_pDevice, m_pDeviceContext);
		DLogIf(FAILED(hr3), L"Geometric primitives InitDeviceObjects() failed with error {}", HR2Str(hr3));
	}
	ASSERT(S_OK == hr3);
	if (m_pFilter->m_inputMT.IsValid())
	{
		if (!InitMediaType(&m_pFilter->m_inputMT))
		{
			ReleaseDevice();
			return E_FAIL;
		}
	}
	if (m_hWnd)
	{
		hr = InitSwapChain(false);
		if (FAILED(hr))
		{
			ReleaseDevice();
			return hr;
		}
	}
	SetCallbackDevice();
	UpdateSubPic();
	SetStereo3dTransform(m_iStereo3dTransform);
	HRESULT hr4 = m_TexDither.Create(m_pDevice, DXGI_FORMAT_R16G16B16A16_FLOAT, dither_size, dither_size, Tex2D_DynamicShaderWrite);
	if (S_OK == hr4)
	{
		hr4 = GetDataFromResource(data, size, IDF_DITHER_32X32_FLOAT16);
		if (S_OK == hr4)
		{
			D3D11_MAPPED_SUBRESOURCE mappedResource;
			hr4 = m_pDeviceContext->Map(m_TexDither.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
			if (S_OK == hr4)
			{
				uint16_t *src = (uint16_t *)data;
				BYTE *dst = (BYTE *)mappedResource.pData;
				for (UINT y = 0; y < dither_size; y++)
				{
					uint16_t *pUInt16 = reinterpret_cast<uint16_t *>(dst);
					for (UINT x = 0; x < dither_size; x++)
					{
						*pUInt16++ = src[x];
						*pUInt16++ = src[x];
						*pUInt16++ = src[x];
						*pUInt16++ = src[x];
					}
					src += dither_size;
					dst += mappedResource.RowPitch;
				}
				m_pDeviceContext->Unmap(m_TexDither.pTexture, 0);
			}
		}
		if (FAILED(hr4))
		{
			m_TexDither.Release();
		}
	}
	m_pFilter->OnDisplayModeChange();
	UpdateStatsStatic();
	UpdateStatsByWindow();
	UpdateStatsByDisplay();
	return hr;
}
HRESULT CDX11VideoProcessor::InitSwapChain(bool bWindowChanged)
{
	DLog(L"CDX11VideoProcessor::InitSwapChain() - {}", m_pFilter->m_bIsFullscreen ? L"fullscreen" : L"window");
	CheckPointer(m_pDXGIFactory2, E_FAIL);
	ReleaseSwapChain();
	auto bFullscreenChange = m_bIsFullscreen != m_pFilter->m_bIsFullscreen;
	m_bIsFullscreen = m_pFilter->m_bIsFullscreen;
	if (bFullscreenChange || bWindowChanged)
	{
		HandleHDRToggle();
		UpdateBitmapShader();
		if ((m_bHdrPassthrough || m_bHdrLocalToneMapping) && SourceIsPQorHLG(m_srcExFmt.HDRParams))
		{
			m_bHdrAllowSwitchDisplay = false;
			InitMediaType(&m_pFilter->m_inputMT);
			m_bHdrAllowSwitchDisplay = true;
			if (m_pDXGISwapChain1)
			{
				DLog(L"CDX11VideoProcessor::InitSwapChain() - SwapChain was created during the call to InitMediaType(), exit");
				return S_OK;
			}
		}
	}
	const auto bHdrOutput = m_bHdrPassthroughSupport && m_bHdrPassthrough && (SourceIsHDR() || m_bVPUseRTXVideoHDR);
	const auto b10BitOutput = bHdrOutput || Preferred10BitOutput();
	m_SwapChainFmt = b10BitOutput ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
	HRESULT hr = S_OK;
	DXGI_SWAP_CHAIN_DESC1 desc1 = {};
	if (m_bIsFullscreen)
	{
		MONITORINFOEXW mi = {sizeof(mi)};
		GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi);
		const CRect rc(mi.rcMonitor);
		desc1.Width = rc.Width();
		desc1.Height = rc.Height();
		desc1.Format = m_SwapChainFmt;
		desc1.SampleDesc.Count = 1;
		desc1.SampleDesc.Quality = 0;
		desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		if ((m_iSwapEffect == SWAPEFFECT_Flip && IsWindows8OrGreater()) || bHdrOutput)
		{
			desc1.BufferCount = bHdrOutput ? 6 : 2;
			desc1.Scaling = DXGI_SCALING_NONE;
			desc1.SwapEffect = IsWindows10OrGreater() ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		}
		else
		{ // SWAPEFFECT_Discard or Windows 7
			desc1.BufferCount = 1;
			desc1.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		}
		desc1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc = {};
		fullscreenDesc.RefreshRate.Numerator = 0;
		fullscreenDesc.RefreshRate.Denominator = 1;
		fullscreenDesc.Windowed = FALSE;
		g_bCreateSwapChain = true;
		hr = m_pDXGIFactory2->CreateSwapChainForHwnd(m_pDevice, m_hWnd, &desc1, &fullscreenDesc, nullptr, &m_pDXGISwapChain1);
		g_bCreateSwapChain = false;
		DLogIf(FAILED(hr), L"CDX11VideoProcessor::InitSwapChain() : CreateSwapChainForHwnd(fullscreen) failed with error {}", HR2Str(hr));
		m_lastFullscreenHMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY);
	}
	else
	{
		desc1.Width = std::max(8, m_windowRect.Width());
		desc1.Height = std::max(8, m_windowRect.Height());
		desc1.Format = m_SwapChainFmt;
		desc1.SampleDesc.Count = 1;
		desc1.SampleDesc.Quality = 0;
		desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		if ((m_iSwapEffect == SWAPEFFECT_Flip && IsWindows8OrGreater()) || bHdrOutput)
		{
			desc1.BufferCount = bHdrOutput ? 6 : 2;
			desc1.Scaling = DXGI_SCALING_NONE;
			desc1.SwapEffect = IsWindows10OrGreater() ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		}
		else
		{ // SWAPEFFECT_Discard or Windows 7
			desc1.BufferCount = 1;
			desc1.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		}
		desc1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		DLogIf(m_windowRect.Width() < 8 || m_windowRect.Height() < 8, L"CDX11VideoProcessor::InitSwapChain() : Invalid window size {}x{}, use {}x{}", m_windowRect.Width(), m_windowRect.Height(), desc1.Width, desc1.Height);
		hr = m_pDXGIFactory2->CreateSwapChainForHwnd(m_pDevice, m_hWnd, &desc1, nullptr, nullptr, &m_pDXGISwapChain1);
		DLogIf(FAILED(hr), L"CDX11VideoProcessor::InitSwapChain() : CreateSwapChainForHwnd() failed with error {}", HR2Str(hr));
		m_lastFullscreenHMonitor = nullptr;
	}
	if (m_pDXGISwapChain1)
	{
		m_UsedSwapEffect = desc1.SwapEffect;
		HRESULT hr2 = m_pDXGISwapChain1->GetContainingOutput(&m_pDXGIOutput);
		m_currentSwapChainColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
		if (bHdrOutput)
		{
			hr2 = m_pDXGISwapChain1->QueryInterface(IID_PPV_ARGS(&m_pDXGISwapChain4));
		}
	}
	return hr;
}

// Note: The ToggleHDR function is defined above in the anonymous namespace, as it's a helper used within this combined file.

BOOL CDX11VideoProcessor::VerifyMediaType(const CMediaType *pmt)
{
	const auto &FmtParams = GetFmtConvParams(pmt);
	if (FmtParams.VP11Format == DXGI_FORMAT_UNKNOWN && FmtParams.DX11Format == DXGI_FORMAT_UNKNOWN)
	{
		return FALSE;
	}
	const BITMAPINFOHEADER *pBIH = GetBIHfromVIHs(pmt);
	if (!pBIH)
	{
		return FALSE;
	}
	if (pBIH->biWidth <= 0 || !pBIH->biHeight)
	{
		return FALSE;
	}
	return TRUE;
}
void CDX11VideoProcessor::SetShaderConvertColorParams()
{
	mp_cmat cmatrix;
	if (m_Dovi.bValid)
	{
		const float brightness = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Brightness) / 255;
		const float contrast = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Contrast);
		for (int i = 0; i < 3; i++)
		{
			cmatrix.m[i][0] = (float)m_Dovi.msd.ColorMetadata.ycc_to_rgb_matrix[i * 3 + 0] * contrast;
			cmatrix.m[i][1] = (float)m_Dovi.msd.ColorMetadata.ycc_to_rgb_matrix[i * 3 + 1] * contrast;
			cmatrix.m[i][2] = (float)m_Dovi.msd.ColorMetadata.ycc_to_rgb_matrix[i * 3 + 2] * contrast;
		}
		for (int i = 0; i < 3; i++)
		{
			cmatrix.c[i] = brightness;
			for (int j = 0; j < 3; j++)
			{
				cmatrix.c[i] -= cmatrix.m[i][j] * m_Dovi.msd.ColorMetadata.ycc_to_rgb_offset[j];
			}
		}
		m_PSConvColorData.bEnable = true;
	}
	else
	{
		mp_csp_params csp_params;
		set_colorspace(m_srcExFmt, csp_params.color);
		csp_params.brightness = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Brightness) / 255;
		csp_params.contrast = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Contrast);
		csp_params.hue = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Hue) / 180 * acos(-1);
		csp_params.saturation = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Saturation);
		csp_params.gray = m_srcParams.CSType == CS_GRAY;
		csp_params.input_bits = csp_params.texture_bits = m_srcParams.CDepth;
		mp_get_csp_matrix(&csp_params, &cmatrix);
		m_PSConvColorData.bEnable =
			m_srcParams.CSType == CS_YUV ||
			m_srcParams.cformat == CF_GBRP8 || m_srcParams.cformat == CF_GBRP10 || m_srcParams.cformat == CF_GBRP16 ||
			csp_params.gray ||
			fabs(csp_params.brightness) > 1e-4f || fabs(csp_params.contrast - 1.0f) > 1e-4f;
	}
	PS_COLOR_TRANSFORM cbuffer = {
		{cmatrix.m[0][0], cmatrix.m[0][1], cmatrix.m[0][2], 0},
		{cmatrix.m[1][0], cmatrix.m[1][1], cmatrix.m[1][2], 0},
		{cmatrix.m[2][0], cmatrix.m[2][1], cmatrix.m[2][2], 0},
		{cmatrix.c[0], cmatrix.c[1], cmatrix.c[2], 0},
	};
	if (m_srcParams.cformat == CF_GBRP8 || m_srcParams.cformat == CF_GBRP10 || m_srcParams.cformat == CF_GBRP16)
	{
		std::swap(cbuffer.cm_r.x, cbuffer.cm_r.y);
		std::swap(cbuffer.cm_r.y, cbuffer.cm_r.z);
		std::swap(cbuffer.cm_g.x, cbuffer.cm_g.y);
		std::swap(cbuffer.cm_g.y, cbuffer.cm_g.z);
		std::swap(cbuffer.cm_b.x, cbuffer.cm_b.y);
		std::swap(cbuffer.cm_b.y, cbuffer.cm_b.z);
	}
	else if (m_srcParams.CSType == CS_GRAY)
	{
		cbuffer.cm_g.x = cbuffer.cm_g.y;
		cbuffer.cm_g.y = 0;
		cbuffer.cm_b.x = cbuffer.cm_b.z;
		cbuffer.cm_b.z = 0;
	}
	if (m_PSConvColorData.pConstants)
	{
		m_pDeviceContext->UpdateSubresource(m_PSConvColorData.pConstants, 0, nullptr, &cbuffer, 0, 0);
	}
	else
	{
		D3D11_BUFFER_DESC BufferDesc = {
			.ByteWidth = sizeof(cbuffer),
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_CONSTANT_BUFFER};
		D3D11_SUBRESOURCE_DATA InitData = {&cbuffer, 0, 0};
		EXECUTE_ASSERT(S_OK == m_pDevice->CreateBuffer(&BufferDesc, &InitData, &m_PSConvColorData.pConstants));
	}
	if (m_PSConvColorData.bEnable && !m_PSConvColorData.pVertexBuffer)
	{
		EXECUTE_ASSERT(S_OK == CreateVertexBuffer(m_pDevice, &m_PSConvColorData.pVertexBuffer, m_srcWidth, m_srcHeight, m_srcRect, 0, false));
	}
	else if (!m_PSConvColorData.bEnable && m_PSConvColorData.pVertexBuffer)
	{
		SAFE_RELEASE(m_PSConvColorData.pVertexBuffer);
	}
}
bool CDX11VideoProcessor::HandleHDRToggle()
{
	m_bHdrDisplaySwitching = true;
	bool bRet = false;
	if ((m_bHdrPassthrough || m_bHdrLocalToneMapping) && SourceIsHDR())
	{
		MONITORINFOEXW mi = {sizeof(mi)};
		GetMonitorInfoW(m_lastFullscreenHMonitor ? m_lastFullscreenHMonitor : MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO *)&mi);
		DisplayConfig_t displayConfig = {};
		if (GetDisplayConfig(mi.szDevice, displayConfig))
		{
			if (displayConfig.HDRSupported() && m_iHdrToggleDisplay)
			{
				bool bHDREnabled = false;
				const auto it = m_hdrModeStartState.find(mi.szDevice);
				if (it != m_hdrModeStartState.cend())
				{
					bHDREnabled = it->second;
				}
				const bool bNeedToggleOn = !displayConfig.HDREnabled() && (m_iHdrToggleDisplay == HDRTD_On || m_iHdrToggleDisplay == HDRTD_OnOff || m_bIsFullscreen && (m_iHdrToggleDisplay == HDRTD_On_Fullscreen || m_iHdrToggleDisplay == HDRTD_OnOff_Fullscreen));
				const bool bNeedToggleOff = displayConfig.HDREnabled() &&
											!bHDREnabled && !m_bIsFullscreen && m_iHdrToggleDisplay == HDRTD_OnOff_Fullscreen;
				DLog(L"HandleHDRToggle() : {}, {}", bNeedToggleOn, bNeedToggleOff);
				if (bNeedToggleOn)
				{
					bRet = ToggleHDR(displayConfig, true);
					DLogIf(!bRet, L"CDX11VideoProcessor::HandleHDRToggle() : Toggle HDR ON failed");
					if (bRet)
					{
						std::wstring deviceName(mi.szDevice);
						const auto &it = m_hdrModeSavedState.find(deviceName);
						if (it == m_hdrModeSavedState.cend())
						{
							m_hdrModeSavedState[std::move(deviceName)] = false;
						}
					}
				}
				else if (bNeedToggleOff)
				{
					bRet = ToggleHDR(displayConfig, false);
					DLogIf(!bRet, L"CDX11VideoProcessor::HandleHDRToggle() : Toggle HDR OFF failed");
					if (bRet)
					{
						std::wstring deviceName(mi.szDevice);
						const auto &it = m_hdrModeSavedState.find(deviceName);
						if (it == m_hdrModeSavedState.cend())
						{
							m_hdrModeSavedState[std::move(deviceName)] = true;
						}
					}
				}
			}
		}
	}
	else if (m_iHdrToggleDisplay)
	{
		MONITORINFOEXW mi = {sizeof(mi)};
		GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO *)&mi);
		DisplayConfig_t displayConfig = {};
		if (GetDisplayConfig(mi.szDevice, displayConfig))
		{
			BOOL bWindowsHDREnabled = FALSE;
			const auto &it = m_hdrModeStartState.find(mi.szDevice);
			if (it != m_hdrModeStartState.cend())
			{
				bWindowsHDREnabled = it->second;
			}
			if (displayConfig.HDRSupported() && displayConfig.HDREnabled() &&
				(!bWindowsHDREnabled || (m_iHdrToggleDisplay == HDRTD_OnOff || m_iHdrToggleDisplay == HDRTD_OnOff_Fullscreen && m_bIsFullscreen)))
			{
				bRet = ToggleHDR(displayConfig, false);
				DLogIf(!bRet, L"CDX11VideoProcessor::HandleHDRToggle() : Toggle HDR OFF failed");
				if (bRet)
				{
					std::wstring deviceName(mi.szDevice);
					const auto &it = m_hdrModeSavedState.find(deviceName);
					if (it == m_hdrModeSavedState.cend())
					{
						m_hdrModeSavedState[std::move(deviceName)] = true;
					}
				}
			}
		}
	}
	m_bHdrDisplaySwitching = false;
	if (bRet)
	{
		MONITORINFOEXW mi = {sizeof(mi)};
		GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO *)&mi);
		DisplayConfig_t displayConfig = {};
		if (GetDisplayConfig(mi.szDevice, displayConfig))
		{
			m_bHdrDisplayModeEnabled = displayConfig.HDREnabled();
			m_bHdrSupport = displayConfig.HDRSupported() && m_bHdrDisplayModeEnabled;
			m_DisplayBitsPerChannel = displayConfig.bitsPerChannel;
			m_bACMEnabled = !m_bHdrDisplayModeEnabled && displayConfig.ACMEnabled();
		}
	}
	return bRet;
}
HRESULT CDX11VideoProcessor::Init(const HWND hwnd, const bool displayHdrChanged, bool *pChangeDevice /* = nullptr*/)
{
	DLog(L"CDX11VideoProcessor::Init()");
	const bool bWindowChanged = displayHdrChanged || (m_hWnd != hwnd);
	m_hWnd = hwnd;
	m_bHdrPassthroughSupport = false;
	m_bHdrDisplayModeEnabled = false;
	m_DisplayBitsPerChannel = 8;
	m_bACMEnabled = false;
	MONITORINFOEXW mi = {sizeof(mi)};
	GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO *)&mi);
	DisplayConfig_t displayConfig = {};
	if (GetDisplayConfig(mi.szDevice, displayConfig))
	{
		m_bHdrDisplayModeEnabled = displayConfig.HDREnabled();
		m_bHdrPassthroughSupport = displayConfig.HDRSupported() && m_bHdrDisplayModeEnabled;
		m_DisplayBitsPerChannel = displayConfig.bitsPerChannel;
		m_bACMEnabled = !m_bHdrDisplayModeEnabled && displayConfig.ACMEnabled();
	}
	if (m_bIsFullscreen != m_pFilter->m_bIsFullscreen)
	{
		m_srcVideoTransferFunction = 0;
	}
	IDXGIAdapter *pDXGIAdapter = nullptr;
	const UINT currentAdapter = GetAdapter(hwnd, m_pDXGIFactory1, &pDXGIAdapter);
	CheckPointer(pDXGIAdapter, E_FAIL);
	if (m_nCurrentAdapter == currentAdapter)
	{
		SAFE_RELEASE(pDXGIAdapter);
		SetCallbackDevice();
		if (!m_pDXGISwapChain1 || m_bIsFullscreen != m_pFilter->m_bIsFullscreen || bWindowChanged)
		{
			InitSwapChain(bWindowChanged);
			UpdateStatsStatic();
			m_pFilter->OnDisplayModeChange();
		}
		return S_OK;
	}
	m_nCurrentAdapter = currentAdapter;
	ReleaseSwapChain();
	m_pDXGIFactory2.Release();
	ReleaseDevice();
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};
	D3D_FEATURE_LEVEL featurelevel;
	ID3D11Device *pDevice = nullptr;
	HRESULT hr = D3D11CreateDevice(
		pDXGIAdapter,
		D3D_DRIVER_TYPE_UNKNOWN,
		nullptr,
#ifdef _DEBUG
		D3D11_CREATE_DEVICE_DEBUG,
#else
		0,
#endif
		featureLevels,
		std::size(featureLevels),
		D3D11_SDK_VERSION,
		&pDevice,
		&featurelevel,
		nullptr);
#ifdef _DEBUG
	if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING || (hr == E_FAIL && !IsWindows8OrGreater()))
	{
		DLog(L"WARNING: D3D11 debugging messages will not be displayed");
		hr = D3D11CreateDevice(
			pDXGIAdapter,
			D3D_DRIVER_TYPE_UNKNOWN,
			nullptr,
			0,
			featureLevels,
			std::size(featureLevels),
			D3D11_SDK_VERSION,
			&pDevice,
			&featurelevel,
			nullptr);
	}
#endif
	SAFE_RELEASE(pDXGIAdapter);
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::Init() : D3D11CreateDevice() failed with error {}", HR2Str(hr));
		return hr;
	}
	DLog(L"CDX11VideoProcessor::Init() : D3D11CreateDevice() successfully with feature level {}.{}", (featurelevel >> 12), (featurelevel >> 8) & 0xF);
	hr = SetDevice(pDevice, nullptr);
	pDevice->Release();
	if (S_OK == hr)
	{
		if (pChangeDevice)
		{
			*pChangeDevice = true;
		}
	}
	if (m_VendorId == PCIV_INTEL && CPUInfo::HaveSSE41())
	{
		m_pCopyGpuFn = CopyGpuFrame_SSE41;
	}
	else
	{
		m_pCopyGpuFn = CopyPlaneAsIs;
	}
	return hr;
}
BOOL CDX11VideoProcessor::InitMediaType(const CMediaType *pmt)
{
	DLog(L"CDX11VideoProcessor::InitMediaType()");
	if (!VerifyMediaType(pmt))
	{
		return FALSE;
	}
	ReleaseVP();
	auto FmtParams = GetFmtConvParams(pmt);
	const BITMAPINFOHEADER *pBIH = nullptr;
	m_decExFmt.value = 0;
	if (pmt->formattype == FORMAT_VideoInfo2)
	{
		const VIDEOINFOHEADER2 *vih2 = (VIDEOINFOHEADER2 *)pmt->pbFormat;
		pBIH = &vih2->bmiHeader;
		m_srcRect = vih2->rcSource;
		m_srcAspectRatioX = vih2->dwPictAspectRatioX;
		m_srcAspectRatioY = vih2->dwPictAspectRatioY;
		if (FmtParams.CSType == CS_YUV && (vih2->dwControlFlags & (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT)))
		{
			m_decExFmt.value = vih2->dwControlFlags;
			m_decExFmt.SampleFormat = AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT; // ignore other flags
		}
		m_bInterlaced = (vih2->dwInterlaceFlags & AMINTERLACE_IsInterlaced);
		m_rtAvgTimePerFrame = vih2->AvgTimePerFrame;
	}
	else if (pmt->formattype == FORMAT_VideoInfo)
	{
		const VIDEOINFOHEADER *vih = (VIDEOINFOHEADER *)pmt->pbFormat;
		pBIH = &vih->bmiHeader;
		m_srcRect = vih->rcSource;
		m_srcAspectRatioX = 0;
		m_srcAspectRatioY = 0;
		m_bInterlaced = 0;
		m_rtAvgTimePerFrame = vih->AvgTimePerFrame;
	}
	else
	{
		return FALSE;
	}
	m_pFilter->m_FrameStats.SetStartFrameDuration(m_rtAvgTimePerFrame);
	m_pFilter->m_bValidBuffer = false;
	UINT biWidth = pBIH->biWidth;
	UINT biHeight = labs(pBIH->biHeight);
	m_srcLines = biHeight * FmtParams.PitchCoeff / 2;
	m_srcPitch = biWidth * FmtParams.Packsize;
	switch (FmtParams.cformat)
	{
	case CF_Y8:
	case CF_NV12:
	case CF_RGB24:
	case CF_BGR48:
		m_srcPitch = ALIGN(m_srcPitch, 4);
		break;
	case CF_V210:
		m_srcPitch = ALIGN((biWidth + 5) / 6 * 16, 128);
	}
	if (pBIH->biCompression == BI_RGB && pBIH->biHeight > 0)
	{
		m_srcPitch = -m_srcPitch;
	}
	UINT origW = biWidth;
	UINT origH = biHeight;
	if (pmt->FormatLength() == 112 + sizeof(VR_Extradata))
	{
		const VR_Extradata *vrextra = reinterpret_cast<VR_Extradata *>(pmt->pbFormat + 112);
		if (vrextra->QueryWidth == pBIH->biWidth && vrextra->QueryHeight == pBIH->biHeight && vrextra->Compression == pBIH->biCompression)
		{
			origW = vrextra->FrameWidth;
			origH = abs(vrextra->FrameHeight);
		}
	}
	if (m_srcRect.IsRectNull())
	{
		m_srcRect.SetRect(0, 0, origW, origH);
	}
	m_srcRectWidth = m_srcRect.Width();
	m_srcRectHeight = m_srcRect.Height();
	m_srcExFmt = SpecifyExtendedFormat(m_decExFmt, FmtParams, m_srcRectWidth, m_srcRectHeight);
	bool disableD3D11VP = false;
	switch (FmtParams.cformat)
	{
	case CF_NV12:
		disableD3D11VP = !m_VPFormats.bNV12;
		break;
	case CF_P010:
	case CF_P016:
		disableD3D11VP = !m_VPFormats.bP01x;
		break;
	case CF_YUY2:
		disableD3D11VP = !m_VPFormats.bYUY2;
		break;
	default:
		disableD3D11VP = !m_VPFormats.bOther;
		break;
	}
	if (m_srcExFmt.VideoTransferMatrix == VIDEOTRANSFERMATRIX_YCgCo || m_Dovi.bValid)
	{
		disableD3D11VP = true;
	}
	if (FmtParams.CSType == CS_RGB && m_VendorId == PCIV_NVIDIA)
	{
		disableD3D11VP = true;
	}
	if (disableD3D11VP)
	{
		FmtParams.VP11Format = DXGI_FORMAT_UNKNOWN;
	}
	const auto frm_gcd = std::gcd(m_srcRectWidth, m_srcRectHeight);
	const auto srcFrameARX = m_srcRectWidth / frm_gcd;
	const auto srcFrameARY = m_srcRectHeight / frm_gcd;
	if (!m_srcAspectRatioX || !m_srcAspectRatioY)
	{
		m_srcAspectRatioX = srcFrameARX;
		m_srcAspectRatioY = srcFrameARY;
		m_srcAnamorphic = false;
	}
	else
	{
		const auto ar_gcd = std::gcd(m_srcAspectRatioX, m_srcAspectRatioY);
		m_srcAspectRatioX /= ar_gcd;
		m_srcAspectRatioY /= ar_gcd;
		m_srcAnamorphic = (srcFrameARX != m_srcAspectRatioX || srcFrameARY != m_srcAspectRatioY);
	}
	UpdateUpscalingShaders();
	UpdateDownscalingShaders();
	m_pPSCorrection_HDR.Release();
	m_pPSConvertColor.Release();
	m_pPSConvertColorDeint.Release();
	m_PSConvColorData.bEnable = false;
	m_pPSHDR10ToneMapping_HDR.Release();
	m_pHDR10ToneMappingConstants_HDR.Release();
	UpdateTexParams(FmtParams.CDepth);
	if (m_bHdrAllowSwitchDisplay && m_srcVideoTransferFunction != m_srcExFmt.HDRParams.VideoTransferFunction)
	{
		auto ret = HandleHDRToggle();
		if (!ret && ((m_bHdrPassthrough || m_bHdrLocalToneMapping) && m_bHdrSupport && SourceIsPQorHLG(m_srcExFmt.HDRParams) && !m_pDXGISwapChain4))
		{
			ret = true;
		}
		if (ret)
		{
			ReleaseSwapChain();
			Init(m_hWnd, false); // Calls Init, which calls InitMediaType again.
		}
	}
	if (Preferred10BitOutput() && m_SwapChainFmt == DXGI_FORMAT_B8G8R8A8_UNORM)
	{
		ReleaseSwapChain();
		Init(m_hWnd, false); // Calls Init, which calls InitMediaType again.
	}
	m_srcVideoTransferFunction = m_srcExFmt.HDRParams.VideoTransferFunction;
	HRESULT hr = E_NOT_VALID_STATE;
	if (FmtParams.VP11Format != DXGI_FORMAT_UNKNOWN)
	{
		hr = InitializeD3D11VP(FmtParams, origW, origH, pmt);
		if (SUCCEEDED(hr))
		{
			UINT resId = 0;
			m_pCorrectionConstants_HDR.Release();
			bool bTransFunc22 = m_srcExFmt.HDRParams.VideoTransferFunction == DXVA2_VideoTransFunc_22 || m_srcExFmt.HDRParams.VideoTransferFunction == DXVA2_VideoTransFunc_709 || m_srcExFmt.HDRParams.VideoTransferFunction == DXVA2_VideoTransFunc_240M;
			if (m_srcExFmt.HDRParams.VideoTransferFunction == MFVideoTransFunc_2084 && !(m_bHdrPassthroughSupport && (m_bHdrPassthrough || m_bHdrLocalToneMapping)) && m_bConvertToSdr)
			{
				resId = m_D3D11VP.IsPqSupported() ? IDF_PS_11_CONVERT_PQ_TO_SDR : IDF_PS_11_FIXCONVERT_PQ_TO_SDR;
				m_strCorrection = L"PQ to SDR";
			}
			else if (m_srcExFmt.HDRParams.VideoTransferFunction == MFVideoTransFunc_HLG)
			{
				if (m_bHdrPassthroughSupport && (m_bHdrPassthrough || m_bHdrLocalToneMapping))
				{
					resId = IDF_PS_11_CONVERT_HLG_TO_PQ;
					m_strCorrection = L"HLG to PQ";
				}
				else if (m_bConvertToSdr)
				{
					resId = IDF_PS_11_FIXCONVERT_HLG_TO_SDR;
					m_strCorrection = L"HLG to SDR";
				}
				else if (m_srcExFmt.HDRParams.VideoPrimaries == MFVideoPrimaries_BT2020)
				{
					resId = IDF_PS_11_FIX_BT2020;
					m_strCorrection = L"Fix BT.2020";
				}
			}
			else if (bTransFunc22 && m_srcExFmt.HDRParams.VideoPrimaries == MFVideoPrimaries_BT2020)
			{
				resId = IDF_PS_11_FIX_BT2020;
				m_strCorrection = L"Fix BT.2020";
			}
			if (resId)
			{
				S_OK == CreatePShaderFromResource(&m_pPSCorrection_HDR, resId);
				EXECUTE_ASSERT(S_OK);
				DLogIf(m_pPSCorrection_HDR, L"CDX11VideoProcessor::InitMediaType() m_pPSCorrection_HDR('{}') created", m_strCorrection);
				ShaderLuminanceParams_t lumParams = {};
				SetShaderLuminanceParams(lumParams);
			}
			if (m_bHdrSupport && m_bHdrLocalToneMapping)
			{
				EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSHDR10ToneMapping_HDR, IDF_PS_11_FIX_HDR10));
				DLogIf(m_pPSHDR10ToneMapping_HDR, L"CDX11VideoProcessor::InitMediaType() m_pPSHDR10ToneMapping_HDR(type: '{}') created", m_iHdrLocalToneMappingType);
				SetHDR10ShaderParams(
					m_lastHdr10.hdr10.MinMasteringLuminance / 10000.0f,
					m_lastHdr10.hdr10.MaxMasteringLuminance,
					m_lastHdr10.hdr10.MaxContentLightLevel,
					m_lastHdr10.hdr10.MaxFrameAverageLightLevel,
					m_fHdrDisplayMaxNits,
					m_iHdrLocalToneMappingType,
					m_fHdrDynamicRangeCompression,
					m_fHdrShadowDetail,
					m_fHdrColorVolumeAdaptation,
					m_fHdrSceneAdaptation);
			}
		}
		else
		{
			ReleaseVP();
		}
	}
	if (FAILED(hr) && FmtParams.DX11Format != DXGI_FORMAT_UNKNOWN)
	{
		m_bVPUseRTXVideoHDR = false;
		hr = InitializeTexVP(FmtParams, origW, origH);
		if (SUCCEEDED(hr))
		{
			SetShaderConvertColorParams();
			ShaderLuminanceParams_t lumParams = {};
			SetShaderLuminanceParams(lumParams);
		}
	}
	if (SUCCEEDED(hr))
	{
		UpdateBitmapShader();
		UpdateTexures();
		UpdatePostScaleTexures();
		UpdateStatsStatic();
		m_pFilter->m_inputMT = *pmt;
		return TRUE;
	}
	return FALSE;
}
HRESULT CDX11VideoProcessor::InitializeD3D11VP(const FmtConvParams_t &params, const UINT width, const UINT height, const CMediaType *pmt)
{
	if (!m_D3D11VP.IsVideoDeviceOk())
	{
		return E_ABORT;
	}
	const auto &dxgiFormat = params.VP11Format;
	DLog(L"CDX11VideoProcessor::InitializeD3D11VP() started with input surface: {}, {} x {}", DXGIFormatToString(dxgiFormat), width, height);
	m_TexSrcVideo.Release();
	const bool bHdrPassthrough = m_bHdrDisplayModeEnabled && (SourceIsPQorHLG(m_srcExFmt.HDRParams) || (m_bVPUseRTXVideoHDR && params.CDepth == 8));
	m_D3D11OutputFmt = m_InternalTexFmt;
	HRESULT hr = m_D3D11VP.InitVideoProcessor(dxgiFormat, width, height, m_srcExFmt, m_bInterlaced, bHdrPassthrough, m_D3D11OutputFmt, &m_D3D11VP_StreamInfo);
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : InitVideoProcessor() failed with error {}", HR2Str(hr));
		return hr;
	}
	hr = m_D3D11VP_StreamInfo.InitInputTextures(m_pDevice);
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : InitInputTextures() failed with error {}", HR2Str(hr));
		return hr;
	}
	auto superRes = (m_bVPScaling && (params.CDepth == 8 || !m_bACMEnabled)) ? m_iVPSuperRes : SUPERRES_Disable;
	m_bVPUseSuperRes = (m_D3D11VP_StreamInfo.SetSuperRes(superRes) == S_OK);
	auto rtxHDR = m_bVPRTXVideoHDR && m_bHdrPassthroughSupport && m_bHdrPassthrough && m_iTexFormat != TEXFMT_8INT && !SourceIsHDR();
	m_bVPUseRTXVideoHDR = (m_D3D11VP_StreamInfo.SetRTXVideoHDR(rtxHDR) == S_OK);

	bool bSwapChainNeedsReinit = false;
	if ((m_bVPUseRTXVideoHDR && !m_pDXGISwapChain4) || (!m_bVPUseRTXVideoHDR && m_pDXGISwapChain4 && !SourceIsHDR()))
	{
		bSwapChainNeedsReinit = true;
	}
	if (bSwapChainNeedsReinit)
	{
		InitSwapChain(false);
		InitMediaType(pmt);
		return S_OK;
	}
	hr = m_TexSrcVideo.Create(m_pDevice, dxgiFormat, width, height, Tex2D_DynamicShaderWriteNoSRV);
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : m_TexSrcVideo.Create() failed with error {}", HR2Str(hr));
		return hr;
	}
	m_srcWidth = width;
	m_srcHeight = height;
	m_srcParams = params;
	m_srcDXGIFormat = dxgiFormat;
	m_pCopyPlaneFn = GetCopyPlaneFunction(params, VP_D3D11);
	DLog(L"CDX11VideoProcessor::InitializeD3D11VP() completed successfully");
	return S_OK;
}
HRESULT CDX11VideoProcessor::InitializeTexVP(const FmtConvParams_t &params, const UINT width, const UINT height)
{
	DLog(L"CDX11VideoProcessor::InitializeTexVP() started with input surface: {}, {} x {}", DXGIFormatToString(params.DX11Format), width, height);
	HRESULT hr = m_TexSrcVideo.CreateEx(m_pDevice, params.DX11Format, params.pDX11Planes, width, height, Tex2D_DynamicShaderWrite);
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::InitializeTexVP() : m_TexSrcVideo.CreateEx() failed with error {}", HR2Str(hr));
		return hr;
	}
	m_srcWidth = width;
	m_srcHeight = height;
	m_srcParams = params;
	m_srcDXGIFormat = params.DX11Format;
	m_pCopyPlaneFn = GetCopyPlaneFunction(params, VP_D3D11_SHADER);
	SetDefaultDXVA2ProcAmpRanges(m_DXVA2ProcAmpRanges);
	DLog(L"CDX11VideoProcessor::InitializeTexVP() completed successfully");
	return S_OK;
}
void CDX11VideoProcessor::UpdatFrameProperties()
{
	m_srcPitch = m_srcWidth * m_srcParams.Packsize;
	m_srcLines = m_srcHeight * m_srcParams.PitchCoeff / 2;
}
BOOL CDX11VideoProcessor::GetAlignmentSize(const CMediaType &mt, SIZE &Size)
{
	if (VerifyMediaType(&mt))
	{
		const auto &FmtParams = GetFmtConvParams(&mt);
		if (FmtParams.cformat == CF_RGB24)
		{
			Size.cx = ALIGN(Size.cx, 4);
		}
		else if (FmtParams.cformat == CF_RGB48 || FmtParams.cformat == CF_BGR48)
		{
			Size.cx = ALIGN(Size.cx, 2);
		}
		else
		{
			auto pBIH = GetBIHfromVIHs(&mt);
			if (!pBIH)
			{
				return FALSE;
			}
			auto biWidth = pBIH->biWidth;
			auto biHeight = labs(pBIH->biHeight);
			if (!m_Alignment.cx || m_Alignment.cformat != FmtParams.cformat || m_Alignment.texture.desc.Width != biWidth || m_Alignment.texture.desc.Height != biHeight)
			{
				m_Alignment.texture.Release();
				m_Alignment.cformat = {};
				m_Alignment.cx = {};
			}
			if (!m_Alignment.texture.pTexture)
			{
				auto VP11Format = FmtParams.VP11Format;
				if (VP11Format != DXGI_FORMAT_UNKNOWN)
				{
					bool disableD3D11VP = false;
					switch (FmtParams.cformat)
					{
					case CF_NV12:
						disableD3D11VP = !m_VPFormats.bNV12;
						break;
					case CF_P010:
					case CF_P016:
						disableD3D11VP = !m_VPFormats.bP01x;
						break;
					case CF_YUY2:
						disableD3D11VP = !m_VPFormats.bYUY2;
						break;
					default:
						disableD3D11VP = !m_VPFormats.bOther;
						break;
					}
					if (disableD3D11VP)
					{
						VP11Format = DXGI_FORMAT_UNKNOWN;
					}
				}
				HRESULT hr = E_FAIL;
				if (VP11Format != DXGI_FORMAT_UNKNOWN)
				{
					hr = m_Alignment.texture.Create(m_pDevice, VP11Format, biWidth, biHeight, Tex2D_DynamicShaderWriteNoSRV);
				}
				if (FAILED(hr) && FmtParams.DX11Format != DXGI_FORMAT_UNKNOWN)
				{
					hr = m_Alignment.texture.CreateEx(m_pDevice, FmtParams.DX11Format, FmtParams.pDX11Planes, biWidth, biHeight, Tex2D_DynamicShaderWrite);
				}
				if (FAILED(hr))
				{
					return FALSE;
				}
				UINT RowPitch = 0;
				D3D11_MAPPED_SUBRESOURCE mappedResource = {};
				if (SUCCEEDED(m_pDeviceContext->Map(m_Alignment.texture.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource)))
				{
					RowPitch = mappedResource.RowPitch;
					m_pDeviceContext->Unmap(m_Alignment.texture.pTexture, 0);
				}
				if (!RowPitch)
				{
					return FALSE;
				}
				m_Alignment.cformat = FmtParams.cformat;
				m_Alignment.cx = RowPitch / FmtParams.Packsize;
			}
			Size.cx = m_Alignment.cx;
		}
		if (FmtParams.cformat == CF_RGB24 || FmtParams.cformat == CF_XRGB32 || FmtParams.cformat == CF_ARGB32)
		{
			Size.cy = -abs(Size.cy); // only for biCompression == BI_RGB
		}
		else
		{
			Size.cy = abs(Size.cy);
		}
		return TRUE;
	}
	return FALSE;
}
HRESULT CDX11VideoProcessor::ProcessSample(IMediaSample *pSample)
{
	REFERENCE_TIME rtStart, rtEnd;
	if (FAILED(pSample->GetTime(&rtStart, &rtEnd)))
	{
		rtStart = m_pFilter->m_FrameStats.GeTimestamp();
	}
	const REFERENCE_TIME rtFrameDur = m_pFilter->m_FrameStats.GetAverageFrameDuration();
	rtEnd = rtStart + rtFrameDur;
	m_rtStart = rtStart;
	CRefTime rtClock(rtStart);
	HRESULT hr = CopySample(pSample);
	if (FAILED(hr))
	{
		m_RenderStats.failed++;
		return hr;
	}
	hr = Render(1, rtStart);
	m_pFilter->m_DrawStats.Add(GetPreciseTick());
	if (m_pFilter->m_filterState == State_Running)
	{
		m_pFilter->StreamTime(rtClock);
	}
	m_RenderStats.syncoffset = rtClock - rtStart;
	int so = (int)std::clamp(m_RenderStats.syncoffset, -UNITS, UNITS);
	m_Syncs.Add(so);
	if (m_bDoubleFrames)
	{
		if (rtEnd < rtClock)
		{
			m_RenderStats.dropped2++;
			return S_FALSE; // skip frame
		}
		rtStart += rtFrameDur / 2;
		hr = Render(2, rtStart);
		m_pFilter->m_DrawStats.Add(GetPreciseTick());
		if (m_pFilter->m_filterState == State_Running)
		{
			m_pFilter->StreamTime(rtClock);
		}
		m_RenderStats.syncoffset = rtClock - rtStart;
		so = (int)std::clamp(m_RenderStats.syncoffset, -UNITS, UNITS);
		m_Syncs.Add(so);
	}
	return hr;
}
HRESULT CDX11VideoProcessor::CopySample(IMediaSample *pSample)
{
	CheckPointer(m_pDXGISwapChain1, E_FAIL);
	uint64_t tick = GetPreciseTick();
	m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE; // Progressive
	m_bDoubleFrames = false;
	if (m_bInterlaced)
	{
		if (CComQIPtr<IMediaSample2> pMS2 = pSample)
		{
			AM_SAMPLE2_PROPERTIES props;
			if (SUCCEEDED(pMS2->GetProperties(sizeof(props), (BYTE *)&props)))
			{
				if ((props.dwTypeSpecificFlags & AM_VIDEO_FLAG_WEAVE) == 0)
				{
					if (props.dwTypeSpecificFlags & AM_VIDEO_FLAG_FIELD1FIRST)
					{
						m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST; // Top-field first
					}
					else
					{
						m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST; // Bottom-field first
					}
					m_bDoubleFrames = m_bDeintDouble && m_D3D11VP_StreamInfo.IsReady();
				}
			}
		}
	}
	HRESULT hr = S_OK;
	m_FieldDrawn = 0;
	bool updateStats = false;
	m_hdr10 = {};
	if (CComQIPtr<IMediaSideData> pMediaSideData = pSample)
	{
		if (SourceIsPQorHLG(m_srcExFmt.HDRParams) && (m_bHdrPassthrough || m_bHdrLocalToneMapping))
		{
			MediaSideDataHDR *hdr = nullptr;
			size_t size = 0;
			hr = pMediaSideData->GetSideData(IID_MediaSideDataHDR, (const BYTE **)&hdr, &size);
			if (SUCCEEDED(hr) && size == sizeof(MediaSideDataHDR))
			{
				m_hdr10.timestamp = tick;
				updateStats = true;
				const auto &primaries_x = hdr->display_primaries_x;
				const auto &primaries_y = hdr->display_primaries_y;
				if (primaries_x[0] > 0. && primaries_x[1] > 0. && primaries_x[2] > 0. && primaries_y[0] > 0. && primaries_y[1] > 0. && primaries_y[2] > 0. && hdr->white_point_x > 0. && hdr->white_point_y > 0. && hdr->max_display_mastering_luminance > 0. && hdr->min_display_mastering_luminance >= 0.)
				{
					m_hdr10.bValid = true;
					m_hdr10.hdr10.RedPrimary[0] = static_cast<UINT16>(std::lround(primaries_x[2] * 50000.0));
					m_hdr10.hdr10.RedPrimary[1] = static_cast<UINT16>(std::lround(primaries_y[2] * 50000.0));
					m_hdr10.hdr10.GreenPrimary[0] = static_cast<UINT16>(std::lround(primaries_x[0] * 50000.0));
					m_hdr10.hdr10.GreenPrimary[1] = static_cast<UINT16>(std::lround(primaries_y[0] * 50000.0));
					m_hdr10.hdr10.BluePrimary[0] = static_cast<UINT16>(std::lround(primaries_x[1] * 50000.0));
					m_hdr10.hdr10.BluePrimary[1] = static_cast<UINT16>(std::lround(primaries_y[1] * 50000.0));
					m_hdr10.hdr10.WhitePoint[0] = static_cast<UINT16>(std::lround(hdr->white_point_x * 50000.0));
					m_hdr10.hdr10.WhitePoint[1] = static_cast<UINT16>(std::lround(hdr->white_point_y * 50000.0));
					m_hdr10.hdr10.MaxMasteringLuminance = static_cast<UINT>(std::lround(hdr->max_display_mastering_luminance));
					m_hdr10.hdr10.MinMasteringLuminance = static_cast<UINT>(std::lround(hdr->min_display_mastering_luminance * 10000.0));
				}
			}
			MediaSideDataHDRContentLightLevel *hdrCLL = nullptr;
			size = 0;
			hr = pMediaSideData->GetSideData(IID_MediaSideDataHDRContentLightLevel, (const BYTE **)&hdrCLL, &size);
			if (SUCCEEDED(hr) && size == sizeof(MediaSideDataHDRContentLightLevel))
			{
				m_hdr10.hdr10.MaxContentLightLevel = hdrCLL->MaxCLL;
				m_hdr10.hdr10.MaxFrameAverageLightLevel = hdrCLL->MaxFALL;
			}
		}
		size_t size = 0;
		MediaSideData3DOffset *offset = nullptr;
		hr = pMediaSideData->GetSideData(IID_MediaSideData3DOffset, (const BYTE **)&offset, &size);
		if (SUCCEEDED(hr) && size == sizeof(MediaSideData3DOffset) && offset->offset_count > 0 && offset->offset[0])
		{
			m_nStereoSubtitlesOffsetInPixels = offset->offset[0];
		}
		if (m_srcParams.CSType == CS_YUV && (m_bHdrPreferDoVi || !SourceIsPQorHLG(m_srcExFmt.HDRParams)))
		{
			MediaSideDataDOVIMetadata *pDOVIMetadata = nullptr;
			hr = pMediaSideData->GetSideData(IID_MediaSideDataDOVIMetadata, (const BYTE **)&pDOVIMetadata, &size);
			if (SUCCEEDED(hr) && size == sizeof(MediaSideDataDOVIMetadata) && CheckDoviMetadata(pDOVIMetadata, 1))
			{
				const bool bYCCtoRGBChanged = !m_PSConvColorData.bEnable || (memcmp(&m_Dovi.msd.ColorMetadata.ycc_to_rgb_matrix, &pDOVIMetadata->ColorMetadata.ycc_to_rgb_matrix, sizeof(MediaSideDataDOVIMetadata::ColorMetadata.ycc_to_rgb_matrix) + sizeof(MediaSideDataDOVIMetadata::ColorMetadata.ycc_to_rgb_offset)) != 0);
				const bool bRGBtoLMSChanged = (memcmp(&m_Dovi.msd.ColorMetadata.rgb_to_lms_matrix, &pDOVIMetadata->ColorMetadata.rgb_to_lms_matrix, sizeof(MediaSideDataDOVIMetadata::ColorMetadata.rgb_to_lms_matrix)) != 0);
				const bool bMappingCurvesChanged = !m_pDoviCurvesConstantBuffer || (memcmp(&m_Dovi.msd.Mapping.curves, &pDOVIMetadata->Mapping.curves, sizeof(MediaSideDataDOVIMetadata::Mapping.curves)) != 0);
				const bool bMasteringLuminanceChanged = m_Dovi.msd.ColorMetadata.source_max_pq != pDOVIMetadata->ColorMetadata.source_max_pq || m_Dovi.msd.ColorMetadata.source_min_pq != pDOVIMetadata->ColorMetadata.source_min_pq;
				bool bMMRChanged = false;
				if (bMappingCurvesChanged)
				{
					bool has_mmr = false;
					for (const auto &curve : pDOVIMetadata->Mapping.curves)
					{
						for (uint8_t i = 0; i < (curve.num_pivots - 1); i++)
						{
							if (curve.mapping_idc[i] == 1)
							{
								has_mmr = true;
								break;
							}
						}
					}
					if (m_Dovi.bHasMMR != has_mmr)
					{
						m_Dovi.bHasMMR = has_mmr;
						m_pDoviCurvesConstantBuffer.Release();
						bMMRChanged = true;
					}
				}
				memcpy(&m_Dovi.msd, pDOVIMetadata, sizeof(MediaSideDataDOVIMetadata));
				const bool doviStateChanged = !m_Dovi.bValid;
				m_Dovi.bValid = true;
				if (bMasteringLuminanceChanged)
				{
					constexpr float
						PQ_M1 = 2610.f / (4096.f * 4.f),
						PQ_M2 = 2523.f / 4096.f * 128.f,
						PQ_C1 = 3424.f / 4096.f,
						PQ_C2 = 2413.f / 4096.f * 32.f,
						PQ_C3 = 2392.f / 4096.f * 32.f;
					auto pl_hdr_rescale = [](float x)
					{
						x = powf(x, 1.0f / PQ_M2);
						x = fmaxf(x - PQ_C1, 0.0f) / (PQ_C2 - PQ_C3 * x);
						x = powf(x, 1.0f / PQ_M1);
						x *= 10000.0f;
						return x;
					};
					m_DoviMaxMasteringLuminance = static_cast<UINT>(pl_hdr_rescale(m_Dovi.msd.ColorMetadata.source_max_pq / 4095.f));
					m_DoviMinMasteringLuminance = static_cast<UINT>(pl_hdr_rescale(m_Dovi.msd.ColorMetadata.source_min_pq / 4095.f) * 10000.0);
				}
				if (m_D3D11VP_StreamInfo.IsReady())
				{
					InitMediaType(&m_pFilter->m_inputMT);
				}
				else if (doviStateChanged)
				{
					UpdateStatsStatic();
				}
				if (bYCCtoRGBChanged)
				{
					DLog(L"CDX11VideoProcessor::CopySample() : DoVi ycc_to_rgb_matrix is changed");
					SetShaderConvertColorParams();
				}
				if (bRGBtoLMSChanged || bMMRChanged)
				{
					DLogIf(bRGBtoLMSChanged, L"CDX11VideoProcessor::CopySample() : DoVi rgb_to_lms_matrix is changed");
					DLogIf(bMMRChanged, L"CDX11VideoProcessor::CopySample() : DoVi has_mmr is changed");
					UpdateConvertColorShader();
				}
				if (bMappingCurvesChanged)
				{
					if (m_Dovi.bHasMMR)
					{
						hr = SetShaderDoviCurves();
					}
					else
					{
						hr = SetShaderDoviCurvesPoly();
					}
				}
				if (doviStateChanged && !SourceIsPQorHLG(m_srcExFmt.HDRParams))
				{
					ReleaseSwapChain();
					Init(m_hWnd, false);
				}
			}
		}
	}
	if (CComQIPtr<IMediaSampleD3D11> pMSD3D11 = pSample)
	{
		if (m_iSrcFromGPU != 11)
		{
			m_iSrcFromGPU = 11;
			updateStats = true;
		}
		CComQIPtr<ID3D11Texture2D> pD3D11Texture2D;
		UINT ArraySlice = 0;
		hr = pMSD3D11->GetD3D11Texture(0, &pD3D11Texture2D, &ArraySlice);
		if (FAILED(hr))
		{
			DLog(L"CDX11VideoProcessor::CopySample() : GetD3D11Texture() failed with error {}", HR2Str(hr));
			return hr;
		}
		D3D11_TEXTURE2D_DESC desc = {};
		pD3D11Texture2D->GetDesc(&desc);
		if (desc.Format != m_srcDXGIFormat)
		{
			return E_UNEXPECTED;
		}
		D3D11_BOX srcBox = {0, 0, 0, m_srcWidth, m_srcHeight, 1};
		if (m_D3D11VP_StreamInfo.IsReady())
		{
			m_pDeviceContext->CopySubresourceRegion(m_D3D11VP_StreamInfo.GetNextInputTexture(m_SampleFormat), 0, 0, 0, 0, pD3D11Texture2D, ArraySlice, &srcBox);
		}
		else
		{
			m_pDeviceContext->CopySubresourceRegion(m_TexSrcVideo.pTexture, 0, 0, 0, 0, pD3D11Texture2D, ArraySlice, &srcBox);
		}
	}
	else
	{
		if (m_iSrcFromGPU != 0)
		{
			m_iSrcFromGPU = 0;
			updateStats = true;
		}
		BYTE *data = nullptr;
		const int size = pSample->GetActualDataLength();
		if (size >= abs(m_srcPitch) * (int)m_srcLines && S_OK == pSample->GetPointer(&data))
		{
			hr = MemCopyToTexSrcVideo(data, m_srcPitch);
			if (m_D3D11VP_StreamInfo.IsReady())
			{
				m_pDeviceContext->CopyResource(m_D3D11VP_StreamInfo.GetNextInputTexture(m_SampleFormat), m_TexSrcVideo.pTexture);
			}
		}
	}
	if (updateStats)
	{
		UpdateStatsStatic();
	}
	m_RenderStats.copyticks = GetPreciseTick() - tick;
	return hr;
}
HRESULT CDX11VideoProcessor::AlphaBlt(
	ID3D11ShaderResourceView *pShaderResource,
	ID3D11Texture2D *pRenderTargetTex,
	ID3D11Buffer *pVertexBuffer,
	D3D11_VIEWPORT *pViewPort,
	ID3D11SamplerState *pSampler)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTargetTex, nullptr, &pRenderTargetView);
	if (FAILED(hr))
	{
		DLog(L"AlphaBlt() : CreateRenderTargetView() failed: {}", HR2Str(hr));
		return hr;
	}

	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;
	// Set resources
	m_pDeviceContext->IASetInputLayout(m_pVSimpleInputLayout);
	m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView.p, nullptr);
	m_pDeviceContext->RSSetViewports(1, pViewPort);
	m_pDeviceContext->OMSetBlendState(m_pAlphaBlendState, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
	m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
	m_pDeviceContext->PSSetShader(m_pPS_BitmapToFrame, nullptr, 0);
	m_pDeviceContext->PSSetShaderResources(0, 1, &pShaderResource);
	m_pDeviceContext->PSSetSamplers(0, 1, &pSampler);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &Stride, &Offset);
	// Draw textured quad onto render target
	m_pDeviceContext->Draw(4, 0);

	return hr;
}
HRESULT CDX11VideoProcessor::Render(int field, const REFERENCE_TIME frameStartTime)
{
	CheckPointer(m_TexSrcVideo.pTexture, E_FAIL);
	CheckPointer(m_pDXGISwapChain1, E_FAIL);
	if (field)
	{
		m_FieldDrawn = field;
	}
	CComPtr<ID3D11Texture2D> pBackBuffer;
	HRESULT hr = m_pDXGISwapChain1->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::Render() : GetBuffer() failed with error {}", HR2Str(hr));
		return hr;
	}
	uint64_t tick1 = GetPreciseTick();
	if (!m_windowRect.IsRectEmpty())
	{
		CComPtr<ID3D11RenderTargetView> pRenderTargetView;
		if (S_OK == m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView))
		{
			const FLOAT ClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
			m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ClearColor);
		}
	}
	if (!m_renderRect.IsRectEmpty())
	{
		hr = Process(pBackBuffer, m_srcRect, m_videoRect, m_FieldDrawn == 2);
	}
	DrawSubtitles(pBackBuffer);

	if (m_bShowStats)
	{
		hr = DrawStats(pBackBuffer);
	}
	if (m_bAlphaBitmapEnable)
	{
		D3D11_TEXTURE2D_DESC desc;
		pBackBuffer->GetDesc(&desc);
		D3D11_VIEWPORT VP = {
			m_AlphaBitmapNRectDest.left * desc.Width,
			m_AlphaBitmapNRectDest.top * desc.Height,
			(m_AlphaBitmapNRectDest.right - m_AlphaBitmapNRectDest.left) * desc.Width,
			(m_AlphaBitmapNRectDest.bottom - m_AlphaBitmapNRectDest.top) * desc.Height,
			0.0f,
			1.0f};
		hr = AlphaBlt(m_TexAlphaBitmap.pShaderResource, pBackBuffer, m_pAlphaBitmapVertex, &VP, m_pSamplerLinear);
	}
	static int nTearingPos = 0;
	if (m_bTearingTest)
	{
		CComPtr<ID3D11RenderTargetView> pRenderTargetView;
		if (S_OK == m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView))
		{
			HRESULT hr2 = d3d11rect.InitDeviceObjects(m_pDevice, m_pDeviceContext);
			const SIZE szWindow = m_windowRect.Size();
			RECT rcTearing;
			rcTearing.left = nTearingPos;
			rcTearing.top = 0;
			rcTearing.right = rcTearing.left + 4;
			rcTearing.bottom = szWindow.cy;
			hr2 = d3d11rect.Set(rcTearing, szWindow, D3DCOLOR_XRGB(255, 0, 0));
			hr2 = d3d11rect.Draw(pRenderTargetView, szWindow);
			rcTearing.left = (rcTearing.right + 15) % szWindow.cx;
			rcTearing.right = rcTearing.left + 4;
			hr2 = d3d11rect.Set(rcTearing, szWindow, D3DCOLOR_XRGB(255, 0, 0));
			hr2 = d3d11rect.Draw(pRenderTargetView, szWindow);
			d3d11rect.InvalidateDeviceObjects();
			nTearingPos = (nTearingPos + 7) % szWindow.cx;
		}
	}
	uint64_t tick3 = GetPreciseTick();
	m_RenderStats.paintticks = tick3 - tick1;
	const DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
	if (m_pDXGISwapChain4)
	{
		if (m_currentSwapChainColorSpace != colorSpace)
		{
			m_pDXGISwapChain4->SetColorSpace1(colorSpace);
			m_currentSwapChainColorSpace = colorSpace;
		}

		if (m_hdr10.bValid)
		{
			if (m_bHdrPassthrough)
			{
				hr = m_pDXGISwapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(DXGI_HDR_METADATA_HDR10), &m_hdr10.hdr10);
				DLogIf(FAILED(hr), L"CDX11VideoProcessor::Render() : SetHDRMetaData(hdr) failed with error {}", HR2Str(hr));
			}
			else if (m_bHdrLocalToneMapping)
			{
				SetHDR10ShaderParams(
					m_hdr10.hdr10.MinMasteringLuminance / 10000.0f, m_hdr10.hdr10.MaxMasteringLuminance,
					m_hdr10.hdr10.MaxContentLightLevel, m_hdr10.hdr10.MaxFrameAverageLightLevel,
					m_fHdrDisplayMaxNits, m_iHdrLocalToneMappingType, m_fHdrDynamicRangeCompression, m_fHdrShadowDetail, m_fHdrColorVolumeAdaptation, m_fHdrSceneAdaptation);
			}
			m_lastHdr10 = m_hdr10;
			UpdateStatsStatic();
		}
		else if (m_lastHdr10.bValid)
		{
			if (m_bHdrPassthrough)
			{
				hr = m_pDXGISwapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(DXGI_HDR_METADATA_HDR10), &m_lastHdr10.hdr10);
				DLogIf(FAILED(hr), L"CDX11VideoProcessor::Render() : SetHDRMetaData(lastHdr) failed with error {}", HR2Str(hr));
			}
			else if (m_bHdrLocalToneMapping)
			{
				SetHDR10ShaderParams(
					m_lastHdr10.hdr10.MinMasteringLuminance / 10000.0f, m_lastHdr10.hdr10.MaxMasteringLuminance,
					m_lastHdr10.hdr10.MaxContentLightLevel, m_lastHdr10.hdr10.MaxFrameAverageLightLevel,
					m_fHdrDisplayMaxNits, m_iHdrLocalToneMappingType, m_fHdrDynamicRangeCompression, m_fHdrShadowDetail, m_fHdrColorVolumeAdaptation, m_fHdrSceneAdaptation);
			}
		}
		else if (memcmp(&m_hdr10.hdr10, &m_lastHdr10.hdr10, sizeof(m_hdr10.hdr10)) != 0)
		{
			if (m_bHdrPassthrough)
			{
				hr = m_pDXGISwapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(DXGI_HDR_METADATA_HDR10), &m_hdr10.hdr10);
				DLogIf(FAILED(hr), L"CDX11VideoProcessor::Render() : SetHDRMetaData(hdr) failed with error {}", HR2Str(hr));
			}
			else if (m_bHdrLocalToneMapping)
			{
				SetHDR10ShaderParams(
					m_hdr10.hdr10.MinMasteringLuminance / 10000.0f, m_hdr10.hdr10.MaxMasteringLuminance,
					m_hdr10.hdr10.MaxContentLightLevel, m_hdr10.hdr10.MaxFrameAverageLightLevel,
					m_fHdrDisplayMaxNits, m_iHdrLocalToneMappingType, m_fHdrDynamicRangeCompression, m_fHdrShadowDetail, m_fHdrColorVolumeAdaptation, m_fHdrSceneAdaptation);
			}
			m_lastHdr10 = m_hdr10;
			UpdateStatsStatic();
		}
	}
	if (m_bVBlankBeforePresent && m_pDXGIOutput)
	{
		hr = m_pDXGIOutput->WaitForVBlank();
		DLogIf(FAILED(hr), L"WaitForVBlank failed with error {}", HR2Str(hr));
	}
	if (m_bAdjustPresentTime)
	{
		SyncFrameToStreamTime(frameStartTime);
	}
	g_bPresent = true;
	hr = m_pDXGISwapChain1->Present(1, 0);
	g_bPresent = false;
	DLogIf(FAILED(hr), L"CDX11VideoProcessor::Render() : Present() failed with error {}", HR2Str(hr));
	m_RenderStats.presentticks = GetPreciseTick() - tick3;
	if (hr == DXGI_ERROR_INVALID_CALL && m_pFilter->m_bIsD3DFullscreen)
	{
		InitSwapChain(false);
	}
	return hr;
}
HRESULT CDX11VideoProcessor::FillBlack()
{
	CheckPointer(m_pDXGISwapChain1, E_ABORT);
	CComPtr<ID3D11Texture2D> pBackBuffer;
	HRESULT hr = m_pDXGISwapChain1->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::FillBlack() : GetBuffer() failed with error {}", HR2Str(hr));
		return hr;
	}
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	hr = m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView);
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::FillBlack() : CreateRenderTargetView() failed with error {}", HR2Str(hr));
		return hr;
	}
	const FLOAT ClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ClearColor);

	if (m_bShowStats)
	{
		hr = DrawStats(pBackBuffer);
	}
	if (m_bAlphaBitmapEnable)
	{
		D3D11_TEXTURE2D_DESC desc;
		pBackBuffer->GetDesc(&desc);
		D3D11_VIEWPORT VP = {
			m_AlphaBitmapNRectDest.left * desc.Width,
			m_AlphaBitmapNRectDest.top * desc.Height,
			(m_AlphaBitmapNRectDest.right - m_AlphaBitmapNRectDest.left) * desc.Width,
			(m_AlphaBitmapNRectDest.bottom - m_AlphaBitmapNRectDest.top) * desc.Height,
			0.0f,
			1.0f};
		hr = AlphaBlt(m_TexAlphaBitmap.pShaderResource, pBackBuffer, m_pAlphaBitmapVertex, &VP, m_pSamplerLinear);
	}
	g_bPresent = true;
	hr = m_pDXGISwapChain1->Present(1, 0);
	g_bPresent = false;
	DLogIf(FAILED(hr), L"CDX11VideoProcessor::FillBlack() : Present() failed with error {}", HR2Str(hr));
	if (hr == DXGI_ERROR_INVALID_CALL && m_pFilter->m_bIsD3DFullscreen)
	{
		InitSwapChain(false);
	}
	return hr;
}
void CDX11VideoProcessor::UpdateTexures()
{
	if (!m_srcWidth || !m_srcHeight)
	{
		return;
	}
	HRESULT hr = S_OK;
	if (m_D3D11VP_StreamInfo.IsReady())
	{
		if (m_bVPScaling)
		{
			CSize texsize = m_videoRect.Size();
			hr = m_TexConvertOutput.CheckCreate(m_pDevice, m_D3D11OutputFmt, texsize.cx, texsize.cy, Tex2D_DefaultShaderRTarget);
			if (FAILED(hr))
			{
				hr = m_TexConvertOutput.CheckCreate(m_pDevice, m_D3D11OutputFmt, m_srcRectWidth, m_srcRectHeight, Tex2D_DefaultShaderRTarget);
			}
		}
		else
		{
			hr = m_TexConvertOutput.CheckCreate(m_pDevice, m_D3D11OutputFmt, m_srcRectWidth, m_srcRectHeight, Tex2D_DefaultShaderRTarget);
		}
	}
	else
	{
		hr = m_TexConvertOutput.CheckCreate(m_pDevice, m_InternalTexFmt, m_srcRectWidth, m_srcRectHeight, Tex2D_DefaultShaderRTarget);
	}
}
UINT CDX11VideoProcessor::GetPostScaleSteps()
{
	UINT nSteps = (UINT)m_pPostScaleShaders.size();
	if (m_pPSCorrection_HDR)
		nSteps++;
	if (m_pPSHDR10ToneMapping_HDR)
		nSteps++;
	if (m_pPSHalfOUtoInterlace)
		nSteps++;
	if (m_bFinalPass)
		nSteps++;
	return nSteps;
}
void CDX11VideoProcessor::UpdatePostScaleTexures()
{
	const bool needDither =
		(m_SwapChainFmt == DXGI_FORMAT_B8G8R8A8_UNORM && m_InternalTexFmt != DXGI_FORMAT_B8G8R8A8_UNORM) ||
		(m_SwapChainFmt == DXGI_FORMAT_R10G10B10A2_UNORM && m_InternalTexFmt == DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_bFinalPass = (m_bUseDither && needDither && m_TexDither.pTexture);
	if (m_bFinalPass)
	{
		m_pPSFinalPass.Release();
		m_bFinalPass = SUCCEEDED(CreatePShaderFromResource(
			&m_pPSFinalPass,
			(m_SwapChainFmt == DXGI_FORMAT_R10G10B10A2_UNORM) ? IDF_PS_11_FINAL_PASS_10 : IDF_PS_11_FINAL_PASS));
	}
	const UINT numPostScaleSteps = GetPostScaleSteps();
	HRESULT hr = m_TexsPostScale.CheckCreate(m_pDevice, m_InternalTexFmt, m_windowRect.Width(), m_windowRect.Height(), numPostScaleSteps);
}
void CDX11VideoProcessor::UpdateUpscalingShaders()
{
	m_pShaderUpscaleX.Release();
	m_pShaderUpscaleY.Release();
	if (m_iUpscaling != UPSCALE_Nearest)
	{
		EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderUpscaleX, s_Upscaling11ResIDs[m_iUpscaling].shaderX));
		if (m_iUpscaling == UPSCALE_Jinc2)
		{
			m_pShaderUpscaleY = m_pShaderUpscaleX;
		}
		else
		{
			EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderUpscaleY, s_Upscaling11ResIDs[m_iUpscaling].shaderY));
		}
	}
	UpdateScalingStrings();
}
void CDX11VideoProcessor::UpdateDownscalingShaders()
{
	m_pShaderDownscaleX.Release();
	m_pShaderDownscaleY.Release();
	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderDownscaleX, s_Downscaling11ResIDs[m_iDownscaling].shaderX));
	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderDownscaleY, s_Downscaling11ResIDs[m_iDownscaling].shaderY));
	UpdateScalingStrings();
}
void CDX11VideoProcessor::UpdateBitmapShader()
{
	if (m_bHdrDisplayModeEnabled && (SourceIsHDR() || m_bVPUseRTXVideoHDR))
	{
		UINT resid;
		float SDR_peak_lum;
		switch (m_iHdrOsdBrightness)
		{
		default:
			resid = IDF_PS_11_CONVERT_BITMAP_TO_PQ;
			SDR_peak_lum = 100;
			break;
		case 1:
			resid = IDF_PS_11_CONVERT_BITMAP_TO_PQ1;
			SDR_peak_lum = 50;
			break;
		case 2:
			resid = IDF_PS_11_CONVERT_BITMAP_TO_PQ2;
			SDR_peak_lum = 30;
			break;
		}
		m_pPS_BitmapToFrame.Release();
		EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPS_BitmapToFrame, resid));
		m_dwStatsTextColor = TransferPQ(D3DCOLOR_XRGB(255, 255, 255), SDR_peak_lum);
	}
	else
	{
		m_pPS_BitmapToFrame = m_pPS_Simple;
		m_dwStatsTextColor = D3DCOLOR_XRGB(255, 255, 255);
	}
}
HRESULT CDX11VideoProcessor::D3D11VPPass(ID3D11Texture2D *pRenderTarget, const CRect &srcRect, const CRect &dstRect, const bool second)
{
	HRESULT hr = m_D3D11VP_StreamInfo.SetRectangles(srcRect, dstRect);
	hr = m_D3D11VP_StreamInfo.Process(pRenderTarget, m_SampleFormat, second);
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::D3D11VPPass() : m_D3D11VP_StreamInfo.Process() failed with error {}", HR2Str(hr));
	}
	return hr;
}
HRESULT CDX11VideoProcessor::ConvertColorPass(ID3D11Texture2D *pRenderTargetTex)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTargetTex, nullptr, &pRenderTargetView);
	if (FAILED(hr))
	{
		DLog(L"ConvertColorPass() : CreateRenderTargetView() failed with error {}", HR2Str(hr));
		return hr;
	}
	D3D11_VIEWPORT VP;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	VP.Width = (FLOAT)m_TexConvertOutput.desc.Width;
	VP.Height = (FLOAT)m_TexConvertOutput.desc.Height;
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	const UINT Stride = sizeof(VERTEX);
	const UINT Offset = 0;
	m_pDeviceContext->IASetInputLayout(m_pVSimpleInputLayout);
	m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView.p, nullptr);
	m_pDeviceContext->RSSetViewports(1, &VP);
	m_pDeviceContext->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
	m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
	if (m_bDeintBlend && m_SampleFormat != D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE && m_pPSConvertColorDeint)
	{
		m_pDeviceContext->PSSetShader(m_pPSConvertColorDeint, nullptr, 0);
	}
	else
	{
		m_pDeviceContext->PSSetShader(m_pPSConvertColor, nullptr, 0);
	}
	m_pDeviceContext->PSSetShaderResources(0, 1, &m_TexSrcVideo.pShaderResource.p);
	m_pDeviceContext->PSSetShaderResources(1, 1, &m_TexSrcVideo.pShaderResource2.p);
	m_pDeviceContext->PSSetShaderResources(2, 1, &m_TexSrcVideo.pShaderResource3.p);
	m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerPoint.p);
	m_pDeviceContext->PSSetSamplers(1, 1, &m_pSamplerLinear.p);
	m_pDeviceContext->PSSetConstantBuffers(0, 1, &m_PSConvColorData.pConstants);
	m_pDeviceContext->PSSetConstantBuffers(1, 1, &m_pCorrectionConstants_HDR.p);
	m_pDeviceContext->PSSetConstantBuffers(2, 1, &m_pDoviCurvesConstantBuffer.p);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_PSConvColorData.pVertexBuffer, &Stride, &Offset);
	m_pDeviceContext->Draw(4, 0);
	ID3D11ShaderResourceView *views[3] = {};
	m_pDeviceContext->PSSetShaderResources(0, 3, views);
	return hr;
}
HRESULT CDX11VideoProcessor::ResizeShaderPass(const Tex2D_t &Tex, ID3D11Texture2D *pRenderTarget, const CRect &srcRect, const CRect &dstRect, const int rotation)
{
	HRESULT hr = S_OK;
	const int w2 = dstRect.Width();
	const int h2 = dstRect.Height();
	const int k = m_bInterpolateAt50pct ? 2 : 1;
	int w1, h1;
	ID3D11PixelShader *resizerX;
	ID3D11PixelShader *resizerY;
	if (rotation == 90 || rotation == 270)
	{
		w1 = srcRect.Height();
		h1 = srcRect.Width();
		resizerX = (w1 == w2) ? nullptr : (w1 > k * w2) ? m_pShaderDownscaleY.p
														: m_pShaderUpscaleY.p;
		if (resizerX)
		{
			resizerY = (h1 == h2) ? nullptr : (h1 > k * h2) ? m_pShaderDownscaleY.p
															: m_pShaderUpscaleY.p;
		}
		else
		{
			resizerY = (h1 == h2) ? nullptr : (h1 > k * h2) ? m_pShaderDownscaleX.p
															: m_pShaderUpscaleX.p;
		}
	}
	else
	{
		w1 = srcRect.Width();
		h1 = srcRect.Height();
		resizerX = (w1 == w2) ? nullptr : (w1 > k * w2) ? m_pShaderDownscaleX.p
														: m_pShaderUpscaleX.p;
		resizerY = (h1 == h2) ? nullptr : (h1 > k * h2) ? m_pShaderDownscaleY.p
														: m_pShaderUpscaleY.p;
	}
	if (resizerX && resizerY)
	{
		D3D11_TEXTURE2D_DESC desc;
		pRenderTarget->GetDesc(&desc);
		if (resizerX == resizerY)
		{
			hr = TextureResizeShader(Tex, pRenderTarget, srcRect, dstRect, resizerX, rotation, m_bFlip);
			DLogIf(FAILED(hr), L"CDX11VideoProcessor::ResizeShaderPass() : failed with error {}", HR2Str(hr));
			return hr;
		}
		const UINT texWidth = desc.Width;
		const UINT texHeight = h1;
		if (m_TexResize.pTexture)
		{
			if (texWidth != m_TexResize.desc.Width || texHeight != m_TexResize.desc.Height)
			{
				m_TexResize.Release(); // need new texture
			}
		}
		if (!m_TexResize.pTexture)
		{
			hr = m_TexResize.Create(m_pDevice, DXGI_FORMAT_R16G16B16A16_FLOAT, texWidth, texHeight, Tex2D_DefaultShaderRTarget);
			if (FAILED(hr))
			{
				DLog(L"CDX11VideoProcessor::ResizeShaderPass() : m_TexResize.Create() failed with error {}", HR2Str(hr));
				return hr;
			}
		}
		CRect resizeRect(dstRect.left, 0, dstRect.right, texHeight);
		hr = TextureResizeShader(Tex, m_TexResize.pTexture, srcRect, resizeRect, resizerX, rotation, m_bFlip);
		hr = TextureResizeShader(m_TexResize, pRenderTarget, resizeRect, dstRect, resizerY, 0, false);
	}
	else
	{
		if (resizerX)
		{
			hr = TextureResizeShader(Tex, pRenderTarget, srcRect, dstRect, resizerX, rotation, m_bFlip);
		}
		else if (resizerY)
		{
			hr = TextureResizeShader(Tex, pRenderTarget, srcRect, dstRect, resizerY, rotation, m_bFlip);
		}
		else
		{
			hr = TextureCopyRect(Tex, pRenderTarget, srcRect, dstRect, m_pPS_Simple, nullptr, rotation, m_bFlip);
		}
	}
	DLogIf(FAILED(hr), L"CDX11VideoProcessor::ResizeShaderPass() : failed with error {}", HR2Str(hr));
	return hr;
}
HRESULT CDX11VideoProcessor::FinalPass(const Tex2D_t &Tex, ID3D11Texture2D *pRenderTarget, const CRect &srcRect, const CRect &dstRect)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::FinalPass() : CreateRenderTargetView() failed with error {}", HR2Str(hr));
		return hr;
	}
	hr = FillVertexBuffer(m_pDeviceContext, m_pVertexBuffer, Tex.desc.Width, Tex.desc.Height, srcRect, 0, false);
	if (FAILED(hr))
		return hr;
	const FLOAT constants[4] = {(float)Tex.desc.Width / dither_size, (float)Tex.desc.Height / dither_size, 0, 0};
	D3D11_MAPPED_SUBRESOURCE mr;
	hr = m_pDeviceContext->Map(m_pFinalPassConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mr);
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::FinalPass() : Map() failed with error {}", HR2Str(hr));
		return hr;
	}
	memcpy(mr.pData, &constants, sizeof(constants));
	m_pDeviceContext->Unmap(m_pFinalPassConstantBuffer, 0);
	D3D11_VIEWPORT VP;
	VP.TopLeftX = (FLOAT)dstRect.left;
	VP.TopLeftY = (FLOAT)dstRect.top;
	VP.Width = (FLOAT)dstRect.Width();
	VP.Height = (FLOAT)dstRect.Height();
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	m_pDeviceContext->PSSetShaderResources(1, 1, &m_TexDither.pShaderResource.p);
	m_pDeviceContext->PSSetSamplers(1, 1, &m_pSamplerDither.p);
	TextureBlt11(m_pDeviceContext, pRenderTargetView, VP, m_pVSimpleInputLayout, m_pVS_Simple, m_pPSFinalPass, Tex.pShaderResource, m_pSamplerPoint, m_pFinalPassConstantBuffer, m_pVertexBuffer);
	ID3D11ShaderResourceView *views[1] = {};
	m_pDeviceContext->PSSetShaderResources(1, 1, views);
	return hr;
}
void CDX11VideoProcessor::DrawSubtitles(ID3D11Texture2D *pRenderTargetTex)
{
	HRESULT hr = S_OK;
	CComPtr<ISubPic> pSubPic = m_pFilter->GetSubPic(m_rtStart);
	if (pSubPic)
	{
		RECT rcSource, rcDest;
		hr = pSubPic->GetSourceAndDest(m_windowRect, m_videoRect, &rcSource, &rcDest, FALSE, {}, 0, FALSE);
		if (SUCCEEDED(hr))
		{
			CComPtr<ID3D11RenderTargetView> pRenderTargetView;
			hr = m_pDevice->CreateRenderTargetView(pRenderTargetTex, nullptr, &pRenderTargetView);
			if (SUCCEEDED(hr))
			{
				m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView.p, nullptr);
				m_pDeviceContext->IASetInputLayout(m_pVSimpleInputLayout);
				m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
				m_pDeviceContext->PSSetShader(m_pPS_BitmapToFrame, nullptr, 0);
				hr = pSubPic->AlphaBlt(&rcSource, &rcDest, nullptr);
			}
		}
		return;
	}
	if (m_pFilter->m_pSub11CallBack)
	{
		CComPtr<ID3D11RenderTargetView> pRenderTargetView;
		hr = m_pDevice->CreateRenderTargetView(pRenderTargetTex, nullptr, &pRenderTargetView);
		if (SUCCEEDED(hr))
		{
			const CRect rSrcPri(POINT(0, 0), m_windowRect.Size());
			const CRect rDstVid(m_videoRect);
			const auto rtStart = m_pFilter->m_rtStartTime + m_rtStart;
			m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView.p, nullptr);
			m_pDeviceContext->IASetInputLayout(m_pVSimpleInputLayout);
			m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
			m_pDeviceContext->PSSetShader(m_pPS_BitmapToFrame, nullptr, 0);
			hr = m_pFilter->m_pSub11CallBack->Render11(rtStart, 0, m_rtAvgTimePerFrame, rDstVid, rDstVid, rSrcPri, 1., m_iStereo3dTransform == 1 ? m_nStereoSubtitlesOffsetInPixels : 0);
		}
	}
}
HRESULT CDX11VideoProcessor::Process(ID3D11Texture2D *pRenderTarget, const CRect &srcRect, const CRect &dstRect, const bool second)
{
	HRESULT hr = S_OK;
	m_bDitherUsed = false;
	int rotation = m_iRotation;
	CRect rSrc = srcRect;
	Tex2D_t *pInputTexture = nullptr;
	const UINT numSteps = GetPostScaleSteps();
	if (m_D3D11VP_StreamInfo.IsReady())
	{
		if (!(m_iSwapEffect == SWAPEFFECT_Discard && (m_VendorId == PCIV_AMDATI || m_VendorId == PCIV_INTEL)))
		{
			const bool bNeedShaderTransform =
				(m_TexConvertOutput.desc.Width != dstRect.Width() || m_TexConvertOutput.desc.Height != dstRect.Height() || m_bFlip || dstRect.right > m_windowRect.right || dstRect.bottom > m_windowRect.bottom) || (m_bHdrSupport && (m_bHdrPassthrough || m_bHdrLocalToneMapping));
			if (!bNeedShaderTransform && !numSteps)
			{
				m_bVPScalingUseShaders = false;
				hr = D3D11VPPass(pRenderTarget, rSrc, dstRect, second);
				return hr;
			}
		}
		CRect rect(0, 0, m_TexConvertOutput.desc.Width, m_TexConvertOutput.desc.Height);
		hr = D3D11VPPass(m_TexConvertOutput.pTexture, rSrc, rect, second);
		pInputTexture = &m_TexConvertOutput;
		rSrc = rect;
		rotation = 0;
	}
	else if (m_PSConvColorData.bEnable)
	{
		ConvertColorPass(m_TexConvertOutput.pTexture);
		pInputTexture = &m_TexConvertOutput;
		rSrc.SetRect(0, 0, m_TexConvertOutput.desc.Width, m_TexConvertOutput.desc.Height);
	}
	else
	{
		pInputTexture = (Tex2D_t *)&m_TexSrcVideo;
	}

	if (numSteps)
	{
		UINT step = 0;
		Tex2D_t *pTex = m_TexsPostScale.GetFirstTex();
		ID3D11Texture2D *pRT = pTex->pTexture;

		auto StepSetting = [&]()
		{
			if (step > 0)
				pInputTexture = pTex;
			step++;
			if (step < numSteps)
			{
				pTex = m_TexsPostScale.GetNextTex();
				pRT = pTex->pTexture;
			}
			else
			{
				pRT = pRenderTarget;
			}
		};

		CRect currentDestRect = dstRect;
		if (rSrc != dstRect || rotation != 0 || m_bFlip)
		{
			m_bVPScalingUseShaders = true;
			if (numSteps > 0)
			{
				StepSetting();
				hr = ResizeShaderPass(*pInputTexture, pRT, rSrc, dstRect, rotation);
				currentDestRect = CRect(0, 0, dstRect.Width(), dstRect.Height());
			}
			else
			{
				hr = ResizeShaderPass(*pInputTexture, pRenderTarget, rSrc, dstRect, rotation);
			}
		}
		else
		{
			m_bVPScalingUseShaders = false;
		}

		if (m_pPSCorrection_HDR)
		{
			StepSetting();
			hr = TextureCopyRect(*pInputTexture, pRT, currentDestRect, currentDestRect, m_pPSCorrection_HDR, m_pCorrectionConstants_HDR, 0, false);
		}
		if (m_pPSHDR10ToneMapping_HDR)
		{
			StepSetting();
			SetHDR10ShaderParams(
				m_lastHdr10.hdr10.MinMasteringLuminance / 10000.0f,
				m_lastHdr10.hdr10.MaxMasteringLuminance,
				m_lastHdr10.hdr10.MaxContentLightLevel,
				m_lastHdr10.hdr10.MaxFrameAverageLightLevel,
				m_fHdrDisplayMaxNits,
				m_iHdrLocalToneMappingType,
				m_fHdrDynamicRangeCompression,
				m_fHdrShadowDetail,
				m_fHdrColorVolumeAdaptation,
				m_fHdrSceneAdaptation);
			hr = TextureCopyRect(*pInputTexture, pRT, currentDestRect, currentDestRect, m_pPSHDR10ToneMapping_HDR, m_pHDR10ToneMappingConstants_HDR, 0, false);
		}
		if (m_pPostScaleShaders.size())
		{
			static uint32_t counter = 0;
			static long start = GetTickCount();
			long stop = GetTickCount();
			long diff = stop - start;
			if (diff >= 10 * 60 * 1000)
			{
				start = stop;
			}
			PS_EXTSHADER_CONSTANTS ConstData = {
				{1.0f / pTex->desc.Width, 1.0f / pTex->desc.Height},
				{(float)pTex->desc.Width, (float)pTex->desc.Height},
				counter++,
				(float)diff / 1000,
				0,
				0};
			m_pDeviceContext->UpdateSubresource(m_pPostScaleConstants, 0, nullptr, &ConstData, 0, 0);
			for (UINT idx = 0; idx < m_pPostScaleShaders.size(); idx++)
			{
				StepSetting();
				hr = TextureCopyRect(*pInputTexture, pRT, currentDestRect, currentDestRect, m_pPostScaleShaders[idx].shader, m_pPostScaleConstants, 0, false);
			}
		}
		if (m_pPSHalfOUtoInterlace)
		{
			DrawSubtitles(pInputTexture->pTexture);
			StepSetting();
			FLOAT ConstData[] = {
				(float)pTex->desc.Height,
				0,
				(float)dstRect.top / pTex->desc.Height,
				(float)dstRect.bottom / pTex->desc.Height,
			};
			D3D11_MAPPED_SUBRESOURCE mr;
			hr = m_pDeviceContext->Map(m_pHalfOUtoInterlaceConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mr);
			if (SUCCEEDED(hr))
			{
				memcpy(mr.pData, &ConstData, sizeof(ConstData));
				m_pDeviceContext->Unmap(m_pHalfOUtoInterlaceConstantBuffer, 0);
			}
			hr = TextureCopyRect(*pInputTexture, pRT, currentDestRect, currentDestRect, m_pPSHalfOUtoInterlace, m_pHalfOUtoInterlaceConstantBuffer, 0, false);
		}
		if (m_bFinalPass)
		{
			StepSetting();
			hr = FinalPass(*pInputTexture, pRT, currentDestRect, currentDestRect);
			m_bDitherUsed = true;
		}
	}
	else
	{
		hr = ResizeShaderPass(*pInputTexture, pRenderTarget, rSrc, dstRect, rotation);
	}
	DLogIf(FAILED(hr), L"CDX11VideoProcessor::Process() : failed with error {}", HR2Str(hr));
	return hr;
}
void CDX11VideoProcessor::SetVideoRect(const CRect &videoRect)
{
	m_videoRect = videoRect;
	UpdateRenderRect();
	UpdateTexures();
}
HRESULT CDX11VideoProcessor::SetWindowRect(const CRect &windowRect)
{
	m_windowRect = windowRect;
	UpdateRenderRect();
	HRESULT hr = S_OK;
	const UINT w = m_windowRect.Width();
	const UINT h = m_windowRect.Height();
	if (m_pDXGISwapChain1 && !m_bIsFullscreen)
	{
		hr = m_pDXGISwapChain1->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
	}
	UpdateStatsByWindow();
	UpdatePostScaleTexures();
	return hr;
}
HRESULT CDX11VideoProcessor::Reset()
{
	DLog(L"CDX11VideoProcessor::Reset()");
	if ((m_bHdrPassthrough || m_bHdrLocalToneMapping) && SourceIsPQorHLG(m_srcExFmt.HDRParams))
	{
		MONITORINFOEXW mi = {sizeof(mi)};
		GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO *)&mi);
		DisplayConfig_t displayConfig = {};
		if (GetDisplayConfig(mi.szDevice, displayConfig))
		{
			const auto bHdrPassthroughSupport = displayConfig.HDRSupported() && displayConfig.HDREnabled();
			if ((bHdrPassthroughSupport && !m_bHdrSupport) || (!displayConfig.HDREnabled() && m_bHdrSupport))
			{
				m_hdrModeSavedState.erase(mi.szDevice);
				if (m_pFilter->m_inputMT.IsValid())
				{
					CAutoLock cRendererLock(&m_pFilter->m_RendererLock);
					ReleaseSwapChain();
					if (m_iSwapEffect == SWAPEFFECT_Discard && !displayConfig.HDREnabled())
					{
						m_pFilter->Init(true);
					}
					else
					{
						Init(m_hWnd, true);
					}
				}
			}
		}
	}
	return S_OK;
}
HRESULT CDX11VideoProcessor::GetCurrentImage(ID3D11Texture2D **ppImg)
{
	CComPtr<ID3D11Texture2D> pRGB32Texture2D;
	D3D11_TEXTURE2D_DESC desc = CreateTex2DDesc(DXGI_FORMAT_B8G8R8A8_UNORM, m_srcWidth, m_srcHeight, Tex2D_DefaultShaderRTarget);
	HRESULT hr = m_pDevice->CreateTexture2D(&desc, nullptr, &pRGB32Texture2D);
	if (FAILED(hr))
	{
		DLog(L"GetCurrentImage() : CreateTexture2D() failed: {}", HR2Str(hr));
		return hr;
	}

	RECT imageRect = {0, 0, m_srcWidth, m_srcHeight};

	const auto backupHdrPassthrough = m_bHdrPassthrough;
	const auto backupHdrLocalToneMapping = m_bHdrLocalToneMapping;
	const bool bisHDROutput = (m_bHdrSupport && (backupHdrPassthrough || backupHdrLocalToneMapping) && SourceIsHDR());

	CComPtr<ID3D11PixelShader> pOrigPSCorrection = m_pPSCorrection_HDR;
	CComPtr<ID3D11Buffer> pOrigCorrectionConstants = m_pCorrectionConstants_HDR;
	CComPtr<ID3D11PixelShader> pOrigPSHDR10ToneMapping = m_pPSHDR10ToneMapping_HDR;
	CComPtr<ID3D11Buffer> pOrigHDR10ToneMappingConstants = m_pHDR10ToneMappingConstants_HDR;

	if (bisHDROutput)
	{
		if (m_D3D11VP_StreamInfo.IsReady())
		{
			m_pPSCorrection_HDR.Release();
			m_pCorrectionConstants_HDR.Release();
			auto resId = m_srcExFmt.HDRParams.VideoTransferFunction == MFVideoTransFunc_2084 ? IDF_PS_11_CONVERT_PQ_TO_SDR : IDF_PS_11_FIXCONVERT_HLG_TO_SDR;
			HRESULT hrCreate = CreatePShaderFromResource(&m_pPSCorrection_HDR, resId);
			DLogIf(FAILED(hrCreate), L"GetCurrentImage() : CreatePShaderFromResource for temporary SDR correction failed with error {}", HR2Str(hrCreate));
			ShaderLuminanceParams_t lumParams = {};
			SetShaderLuminanceParams(lumParams);

			m_pPSHDR10ToneMapping_HDR.Release();
			m_pHDR10ToneMappingConstants_HDR.Release();
			if (backupHdrLocalToneMapping)
			{
				hrCreate = CreatePShaderFromResource(&m_pPSHDR10ToneMapping_HDR, IDF_PS_11_FIX_HDR10);
				DLogIf(FAILED(hrCreate), L"GetCurrentImage() : CreatePShaderFromResource for temporary HDR10 tone mapping failed with error {}", HR2Str(hrCreate));
				SetHDR10ShaderParams(
					m_lastHdr10.hdr10.MinMasteringLuminance / 10000.0f,
					m_lastHdr10.hdr10.MaxMasteringLuminance,
					m_lastHdr10.hdr10.MaxContentLightLevel,
					m_lastHdr10.hdr10.MaxFrameAverageLightLevel,
					(float)m_iSDRDisplayNits, // Force output to SDR peak nits
					m_iHdrLocalToneMappingType,
					m_fHdrDynamicRangeCompression,
					m_fHdrShadowDetail,
					m_fHdrColorVolumeAdaptation,
					m_fHdrSceneAdaptation);
			}
		}
		else
		{
			m_bHdrPassthrough = false;
			m_bHdrLocalToneMapping = false;
			UpdateConvertColorShader(); // Update shader for SDR output
		}
	}

	const auto backupVidRect = m_videoRect;
	const auto backupWndRect = m_windowRect;
	CopyRect(&m_videoRect, &imageRect);
	CopyRect(&m_windowRect, &imageRect);
	UpdateTexures();
	UpdatePostScaleTexures();

	auto pSub11CallBack = m_pFilter->m_pSub11CallBack;
	m_pFilter->m_pSub11CallBack = nullptr;
	hr = Process(pRGB32Texture2D, m_srcRect, imageRect, false);
	m_pFilter->m_pSub11CallBack = pSub11CallBack;

	m_videoRect = backupVidRect;
	m_windowRect = backupWndRect;
	UpdateTexures();
	UpdatePostScaleTexures();

	if (bisHDROutput)
	{
		if (m_D3D11VP_StreamInfo.IsReady())
		{
			m_pPSCorrection_HDR = pOrigPSCorrection;
			m_pCorrectionConstants_HDR = pOrigCorrectionConstants;
			m_pPSHDR10ToneMapping_HDR = pOrigPSHDR10ToneMapping;
			m_pHDR10ToneMappingConstants_HDR = pOrigHDR10ToneMappingConstants;

			if (backupHdrLocalToneMapping)
			{
				SetHDR10ShaderParams(
					m_lastHdr10.hdr10.MinMasteringLuminance / 10000.0f, m_lastHdr10.hdr10.MaxMasteringLuminance,
					m_lastHdr10.hdr10.MaxContentLightLevel, m_lastHdr10.hdr10.MaxFrameAverageLightLevel,
					m_fHdrDisplayMaxNits, // Restore original display nits
					m_iHdrLocalToneMappingType, m_fHdrDynamicRangeCompression, m_fHdrShadowDetail, m_fHdrColorVolumeAdaptation, m_fHdrSceneAdaptation);
			}
		}
		else
		{
			m_bHdrPassthrough = backupHdrPassthrough;
			m_bHdrLocalToneMapping = backupHdrLocalToneMapping;
			UpdateConvertColorShader(); // Restore original shader
		}
	}

	if (SUCCEEDED(hr))
	{
		*ppImg = pRGB32Texture2D.Detach();
	}

	return hr;
}
HRESULT CDX11VideoProcessor::GetDisplayedImage(BYTE **ppDib, unsigned *pSize)
{
	if (!m_pDXGISwapChain1 || !m_pDevice || !m_pDeviceContext)
	{
		return E_ABORT;
	}
	CComPtr<ID3D11Texture2D> pBackBuffer;
	HRESULT hr = m_pDXGISwapChain1->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::GetDisplayedImage() failed with error {}", HR2Str(hr));
		return hr;
	}
	D3D11_TEXTURE2D_DESC desc;
	pBackBuffer->GetDesc(&desc);
	if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format != DXGI_FORMAT_R10G10B10A2_UNORM)
	{
		DLog(L"CDX11VideoProcessor::GetDisplayedImage() backbuffer format not supported");
		return E_FAIL;
	}
	D3D11_TEXTURE2D_DESC desc2 = CreateTex2DDesc(desc.Format, desc.Width, desc.Height, Tex2D_StagingRead);
	CComPtr<ID3D11Texture2D> pTexture2DShared;
	hr = m_pDevice->CreateTexture2D(&desc2, nullptr, &pTexture2DShared);
	if (FAILED(hr))
	{
		DLog(L"CDX11VideoProcessor::GetDisplayedImage() failed with error {}", HR2Str(hr));
		return hr;
	}
	m_pDeviceContext->CopyResource(pTexture2DShared, pBackBuffer);
	UINT dib_bitdepth;
	CopyFrameDataFn pConvertToDibFunc;
	if (desc2.Format == DXGI_FORMAT_R10G10B10A2_UNORM)
	{
		if (m_bAllowDeepColorBitmaps)
		{
			dib_bitdepth = 48;
			pConvertToDibFunc = ConvertR10G10B10A2toBGR48;
		}
		else
		{
			dib_bitdepth = 32;
			pConvertToDibFunc = ConvertR10G10B10A2toBGR32;
		}
	}
	else
	{
		dib_bitdepth = 32;
		pConvertToDibFunc = CopyPlaneAsIs;
	}
	const UINT dib_pitch = CalcDibRowPitch(desc.Width, dib_bitdepth);
	const UINT dib_size = dib_pitch * desc.Height;
	*pSize = sizeof(BITMAPINFOHEADER) + dib_size;
	BYTE *p = (BYTE *)LocalAlloc(LMEM_FIXED, *pSize);
	if (!p)
	{
		return E_OUTOFMEMORY;
	}
	BITMAPINFOHEADER *pBIH = (BITMAPINFOHEADER *)p;
	ZeroMemory(pBIH, sizeof(BITMAPINFOHEADER));
	pBIH->biSize = sizeof(BITMAPINFOHEADER);
	pBIH->biWidth = desc.Width;
	pBIH->biHeight = -(LONG)desc.Height;
	pBIH->biBitCount = dib_bitdepth;
	pBIH->biPlanes = 1;
	pBIH->biSizeImage = dib_size;
	D3D11_MAPPED_SUBRESOURCE mappedResource = {};
	hr = m_pDeviceContext->Map(pTexture2DShared, 0, D3D11_MAP_READ, 0, &mappedResource);
	if (SUCCEEDED(hr))
	{
		pConvertToDibFunc(desc.Height, (BYTE *)(pBIH + 1), dib_pitch, (BYTE *)mappedResource.pData, mappedResource.RowPitch);
		m_pDeviceContext->Unmap(pTexture2DShared, 0);
		*ppDib = p;
	}
	else
	{
		LocalFree(p);
	}
	return hr;
}
HRESULT CDX11VideoProcessor::GetVPInfo(std::wstring &str)
{
	str = L"DirectX 11";
	str += std::format(L"\nGraphics adapter: {}", m_strAdapterDescription);
	str.append(L"\nVideoProcessor  : ");
	if (m_D3D11VP_StreamInfo.IsReady())
	{
		D3D11_VIDEO_PROCESSOR_CAPS caps;
		UINT rateConvIndex;
		D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS rateConvCaps;
		m_D3D11VP_StreamInfo.GetVPParams(caps, rateConvIndex, rateConvCaps);
		str += std::format(L"D3D11, RateConversion_{}", rateConvIndex);
		str.append(L"\nDeinterlaceTech.:");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BLEND)
			str.append(L" Blend,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BOB)
			str.append(L" Bob,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_ADAPTIVE)
			str.append(L" Adaptive,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_MOTION_COMPENSATION)
			str.append(L" Motion Compensation,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_INVERSE_TELECINE)
			str.append(L" Inverse Telecine,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_FRAME_RATE_CONVERSION)
			str.append(L" Frame Rate Conversion");
		str_trim_end(str, ',');
		str += std::format(L"\nReference Frames: Past {}, Future {}", rateConvCaps.PastFrames, rateConvCaps.FutureFrames);
	}
	else
	{
		str.append(L"Shaders");
	}
	str.append(m_strStatsDispInfo);
	if (m_pPostScaleShaders.size())
	{
		str.append(L"\n\nPost scale pixel shaders:");
		for (const auto &pshader : m_pPostScaleShaders)
		{
			str += std::format(L"\n  {}", pshader.name);
		}
	}
#ifdef _DEBUG
	str.append(L"\n\nDEBUG info:");
	str += std::format(L"\nSource tex size: {}x{}", m_srcWidth, m_srcHeight);
	str += std::format(L"\nSource rect    : {},{},{},{} - {}x{}", m_srcRect.left, m_srcRect.top, m_srcRect.right, m_srcRect.bottom, m_srcRect.Width(), m_srcRect.Height());
	str += std::format(L"\nVideo rect     : {},{},{},{} - {}x{}", m_videoRect.left, m_videoRect.top, m_videoRect.right, m_videoRect.bottom, m_videoRect.Width(), m_videoRect.Height());
	str += std::format(L"\nWindow rect    : {},{},{},{} - {}x{}", m_windowRect.left, m_windowRect.top, m_windowRect.right, m_windowRect.bottom, m_windowRect.Width(), m_windowRect.Height());
	if (m_pDevice)
	{
		std::vector<std::pair<const DXGI_FORMAT, UINT>> formatsYUV = {
			{DXGI_FORMAT_NV12, 0},
			{DXGI_FORMAT_P010, 0},
			{DXGI_FORMAT_P016, 0},
			{DXGI_FORMAT_YUY2, 0},
			{DXGI_FORMAT_Y210, 0},
			{DXGI_FORMAT_Y216, 0},
			{DXGI_FORMAT_AYUV, 0},
			{DXGI_FORMAT_Y410, 0},
			{DXGI_FORMAT_Y416, 0},
		};
		std::vector<std::pair<const DXGI_FORMAT, UINT>> formatsRGB = {
			{DXGI_FORMAT_B8G8R8X8_UNORM, 0},
			{DXGI_FORMAT_B8G8R8A8_UNORM, 0},
			{DXGI_FORMAT_R10G10B10A2_UNORM, 0},
			{DXGI_FORMAT_R16G16B16A16_UNORM, 0},
		};
		for (auto &[format, formatSupport] : formatsYUV)
		{
			m_pDevice->CheckFormatSupport(format, &formatSupport);
		}
		for (auto &[format, formatSupport] : formatsRGB)
		{
			m_pDevice->CheckFormatSupport(format, &formatSupport);
		}
		int count = 0;
		str += L"\nD3D11 VP input formats  :";
		for (const auto &[format, formatSupport] : formatsYUV)
		{
			if (formatSupport & D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT)
			{
				str.append(L" ");
				str.append(DXGIFormatToString(format));
				count++;
			}
		}
		if (count)
		{
			str += L"\n ";
		}
		for (const auto &[format, formatSupport] : formatsRGB)
		{
			if (formatSupport & D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT)
			{
				str.append(L" ");
				str.append(DXGIFormatToString(format));
			}
		}
		count = 0;
		str += L"\nShader Texture2D formats:";
		for (const auto &[format, formatSupport] : formatsYUV)
		{
			if (formatSupport & (D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
			{
				str.append(L" ");
				str.append(DXGIFormatToString(format));
				count++;
			}
		}
		if (count)
		{
			str += L"\n ";
		}
		for (const auto &[format, formatSupport] : formatsRGB)
		{
			if (formatSupport & (D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
			{
				str.append(L" ");
				str.append(DXGIFormatToString(format));
			}
		}
	}
#endif
	return S_OK;
}
void CDX11VideoProcessor::Configure(const Settings_t &config)
{
	bool changeWindow = false;
	bool changeDevice = false;
	bool changeVP = false;
	bool changeHDR = false;
	bool changeTextures = false;
	bool changeConvertShader = false;
	bool changeBitmapShader = false;
	bool changeUpscalingShader = false;
	bool changeDowndcalingShader = false;
	bool changeNumTextures = false;
	bool changeResizeStats = false;
	bool changeSuperRes = false;
	bool changeRTXVideoHDR = false;
	bool changeLuminanceParams = false;
	m_bShowStats = config.bShowStats;
	m_bDeintDouble = config.bDeintDouble;
	m_bInterpolateAt50pct = config.bInterpolateAt50pct;
	m_bVBlankBeforePresent = config.bVBlankBeforePresent;
	m_bAdjustPresentTime = config.bAdjustPresentTime;
	m_bDeintBlend = config.bDeintBlend;
	if (config.iResizeStats != m_iResizeStats)
	{
		m_iResizeStats = config.iResizeStats;
		changeResizeStats = true;
	}
	if (config.iTexFormat != m_iTexFormat)
	{
		m_iTexFormat = config.iTexFormat;
		changeTextures = true;
	}
	if (m_srcParams.cformat == CF_NV12)
	{
		changeVP = config.VPFmts.bNV12 != m_VPFormats.bNV12;
	}
	else if (m_srcParams.cformat == CF_P010 || m_srcParams.cformat == CF_P016)
	{
		changeVP = config.VPFmts.bP01x != m_VPFormats.bP01x;
	}
	else if (m_srcParams.cformat == CF_YUY2)
	{
		changeVP = config.VPFmts.bYUY2 != m_VPFormats.bYUY2;
	}
	else
	{
		changeVP = config.VPFmts.bOther != m_VPFormats.bOther;
	}
	m_VPFormats = config.VPFmts;
	if (config.bVPScaling != m_bVPScaling)
	{
		m_bVPScaling = config.bVPScaling;
		changeTextures = true;
		changeVP = true;
	}
	if (config.iChromaScaling != m_iChromaScaling)
	{
		m_iChromaScaling = config.iChromaScaling;
		changeConvertShader = m_PSConvColorData.bEnable && (m_srcParams.Subsampling == 420 || m_srcParams.Subsampling == 422);
	}
	if (config.iHdrOsdBrightness != m_iHdrOsdBrightness)
	{
		m_iHdrOsdBrightness = config.iHdrOsdBrightness;
		changeBitmapShader = true;
	}
	if (config.iUpscaling != m_iUpscaling)
	{
		m_iUpscaling = config.iUpscaling;
		changeUpscalingShader = true;
	}
	if (config.iDownscaling != m_iDownscaling)
	{
		m_iDownscaling = config.iDownscaling;
		changeDowndcalingShader = true;
	}
	if (config.bUseDither != m_bUseDither)
	{
		m_bUseDither = config.bUseDither;
		changeNumTextures = m_InternalTexFmt != DXGI_FORMAT_B8G8R8A8_UNORM;
	}
	if (config.iSwapEffect != m_iSwapEffect)
	{
		m_iSwapEffect = config.iSwapEffect;
		changeWindow = !m_pFilter->m_bIsFullscreen;
	}
	if (config.bHdrPreferDoVi != m_bHdrPreferDoVi)
	{
		if (m_Dovi.bValid && !config.bHdrPreferDoVi && SourceIsPQorHLG(m_srcExFmt.HDRParams))
		{
			m_Dovi = {};
			changeVP = true;
		}
		m_bHdrPreferDoVi = config.bHdrPreferDoVi;
	}
	if (config.bHdrPassthrough != m_bHdrPassthrough)
	{
		m_bHdrPassthrough = config.bHdrPassthrough;
		changeHDR = true;
	}
	if (config.bHdrLocalToneMapping != m_bHdrLocalToneMapping ||
		config.iHdrLocalToneMappingType != m_iHdrLocalToneMappingType)
	{
		m_bHdrLocalToneMapping = config.bHdrLocalToneMapping;
		m_iHdrLocalToneMappingType = config.iHdrLocalToneMappingType;
		changeHDR = true;
	}
	if (config.fHdrDisplayMaxNits != m_fHdrDisplayMaxNits)
	{
		m_fHdrDisplayMaxNits = config.fHdrDisplayMaxNits;
		changeHDR = true;
	}
	if (config.fHdrDynamicRangeCompression != m_fHdrDynamicRangeCompression)
	{
		m_fHdrDynamicRangeCompression = config.fHdrDynamicRangeCompression;
		changeHDR = true;
	}
	if (config.fHdrShadowDetail != m_fHdrShadowDetail)
	{
		m_fHdrShadowDetail = config.fHdrShadowDetail;
		changeHDR = true;
	}
	if (config.fHdrColorVolumeAdaptation != m_fHdrColorVolumeAdaptation)
	{
		m_fHdrColorVolumeAdaptation = config.fHdrColorVolumeAdaptation;
		changeHDR = true;
	}
	if (config.fHdrSceneAdaptation != m_fHdrSceneAdaptation)
	{
		m_fHdrSceneAdaptation = config.fHdrSceneAdaptation;
		changeHDR = true;
	}
	if (changeConvertShader)
	{
		UpdateConvertColorShader();
	}
	if (config.iHdrToggleDisplay != m_iHdrToggleDisplay)
	{
		if (config.iHdrToggleDisplay == HDRTD_Disabled || m_iHdrToggleDisplay == HDRTD_Disabled)
		{
			changeHDR = true;
		}
		m_iHdrToggleDisplay = config.iHdrToggleDisplay;
	}
	if (config.bConvertToSdr != m_bConvertToSdr)
	{
		m_bConvertToSdr = config.bConvertToSdr;
		if (SourceIsHDR())
		{
			if (m_D3D11VP_StreamInfo.IsReady())
			{
				changeNumTextures = true;
				changeVP = true;
			}
			else
			{
				changeConvertShader = true;
				UpdateConvertColorShader();
			}
		}
	}
	if (config.iVPSuperRes != m_iVPSuperRes)
	{
		m_iVPSuperRes = config.iVPSuperRes;
		changeSuperRes = true;
	}
	if (config.bVPRTXVideoHDR != m_bVPRTXVideoHDR)
	{
		m_bVPRTXVideoHDR = config.bVPRTXVideoHDR;
		changeRTXVideoHDR = true;
	}
	if (config.iSDRDisplayNits != m_iSDRDisplayNits)
	{
		m_iSDRDisplayNits = config.iSDRDisplayNits;
		if (SourceIsHDR())
		{
			changeLuminanceParams = true;
		}
	}
	if (!m_pFilter->GetActive())
	{
		return;
	}
	if (changeWindow)
	{
		ReleaseSwapChain();
		EXECUTE_ASSERT(S_OK == m_pFilter->Init(true));
		if (changeHDR && (SourceIsPQorHLG(m_srcExFmt.HDRParams) || m_bVPUseRTXVideoHDR || m_bVPRTXVideoHDR) || m_iHdrToggleDisplay)
		{
			m_srcVideoTransferFunction = 0;
			InitMediaType(&m_pFilter->m_inputMT);
		}
		return;
	}
	if (changeHDR)
	{
		if (m_iSwapEffect == SWAPEFFECT_Discard)
		{
			ReleaseSwapChain();
			m_pFilter->Init(true);
		}
		m_srcVideoTransferFunction = 0;
		InitMediaType(&m_pFilter->m_inputMT);
		InitSwapChain(false);
		return;
	}
	if (m_Dovi.bValid)
	{
		changeVP = false;
	}
	if (changeVP)
	{
		InitMediaType(&m_pFilter->m_inputMT);
		if (m_bVPUseRTXVideoHDR || m_bVPRTXVideoHDR)
		{
			InitSwapChain(false);
		}
		return;
	}
	if (changeRTXVideoHDR)
	{
		InitMediaType(&m_pFilter->m_inputMT);
		return;
	}
	if (changeTextures)
	{
		UpdateTexParams(m_srcParams.CDepth);
		if (m_D3D11VP_StreamInfo.IsReady())
		{
			EXECUTE_ASSERT(S_OK == InitializeD3D11VP(m_srcParams, m_srcWidth, m_srcHeight, &m_pFilter->m_inputMT));
		}
		UpdateTexures();
		UpdatePostScaleTexures();
	}
	if (changeBitmapShader)
	{
		UpdateBitmapShader();
	}
	if (changeUpscalingShader)
	{
		UpdateUpscalingShaders();
	}
	if (changeDowndcalingShader)
	{
		UpdateDownscalingShaders();
	}
	if (changeLuminanceParams)
	{
		ShaderLuminanceParams_t lumParams = {};
		SetShaderLuminanceParams(lumParams);
	}
	if (changeNumTextures)
	{
		UpdatePostScaleTexures();
	}
	if (changeResizeStats)
	{
		UpdateStatsByWindow();
		UpdateStatsByDisplay();
	}
	if (changeSuperRes)
	{
		auto superRes = (m_bVPScaling && (m_srcParams.CDepth == 8 || !m_bACMEnabled)) ? m_iVPSuperRes : SUPERRES_Disable;
		m_bVPUseSuperRes = (m_D3D11VP_StreamInfo.SetSuperRes(superRes) == S_OK);
	}
	UpdateStatsStatic();
}
void CDX11VideoProcessor::SetRotation(int value)
{
	m_iRotation = value;
	if (m_D3D11VP_StreamInfo.IsReady())
	{
		m_D3D11VP.SetRotation(static_cast<D3D11_VIDEO_PROCESSOR_ROTATION>(value / 90));
	}
}
void CDX11VideoProcessor::SetStereo3dTransform(int value)
{
	m_iStereo3dTransform = value;
	if (m_iStereo3dTransform == 1)
	{
		if (!m_pPSHalfOUtoInterlace)
		{
			EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSHalfOUtoInterlace, IDF_PS_11_HALFOU_TO_INTERLACE));
		}
	}
	else
	{
		m_pPSHalfOUtoInterlace.Release();
	}
}
void CDX11VideoProcessor::Flush()
{
	if (m_D3D11VP_StreamInfo.IsReady())
	{
		m_D3D11VP_StreamInfo.ResetFrameOrder();
	}
	m_rtStart = 0;
}
void CDX11VideoProcessor::ClearPreScaleShaders()
{
	for (auto &pExtShader : m_pPreScaleShaders)
	{
		pExtShader.shader.Release();
	}
	m_pPreScaleShaders.clear();
	DLog(L"CDX11VideoProcessor::ClearPreScaleShaders().");
}
void CDX11VideoProcessor::ClearPostScaleShaders()
{
	for (auto &pExtShader : m_pPostScaleShaders)
	{
		pExtShader.shader.Release();
	}
	m_pPostScaleShaders.clear();
	DLog(L"CDX11VideoProcessor::ClearPostScaleShaders().");
}
HRESULT CDX11VideoProcessor::AddPreScaleShader(const std::wstring &name, const std::string &srcCode)
{
#ifdef _DEBUG
	if (!m_pDevice)
	{
		return E_ABORT;
	}
	ID3DBlob *pShaderCode = nullptr;
	HRESULT hr = CompileShader(srcCode, nullptr, "ps_4_0", &pShaderCode);
	if (S_OK == hr)
	{
		m_pPreScaleShaders.emplace_back();
		hr = m_pDevice->CreatePixelShader(pShaderCode->GetBufferPointer(), pShaderCode->GetBufferSize(), nullptr, &m_pPreScaleShaders.back().shader);
		if (S_OK == hr)
		{
			m_pPreScaleShaders.back().name = name;
			// UpdatePreScaleTexures(); //TODO
			DLog(L"CDX11VideoProcessor::AddPreScaleShader() : \"{}\" pixel shader added successfully.", name);
		}
		else
		{
			DLog(L"CDX11VideoProcessor::AddPreScaleShader() : create pixel shader \"{}\" FAILED!", name);
			m_pPreScaleShaders.pop_back();
		}
		pShaderCode->Release();
	}
	if (S_OK == hr && m_D3D11VP_StreamInfo.IsReady() && m_bVPScaling)
	{
		return S_FALSE;
	}
	return hr;
#else
	return E_NOTIMPL;
#endif
}
HRESULT CDX11VideoProcessor::AddPostScaleShader(const std::wstring &name, const std::string &srcCode)
{
	if (!m_pDevice)
	{
		return E_ABORT;
	}
	ID3DBlob *pShaderCode = nullptr;
	HRESULT hr = CompileShader(srcCode, nullptr, "ps_4_0", &pShaderCode);
	if (S_OK == hr)
	{
		m_pPostScaleShaders.emplace_back();
		hr = m_pDevice->CreatePixelShader(pShaderCode->GetBufferPointer(), pShaderCode->GetBufferSize(), nullptr, &m_pPostScaleShaders.back().shader);
		if (S_OK == hr)
		{
			m_pPostScaleShaders.back().name = name;
			UpdatePostScaleTexures();
			DLog(L"CDX11VideoProcessor::AddPostScaleShader() : \"{}\" pixel shader added successfully.", name);
		}
		else
		{
			DLog(L"CDX11VideoProcessor::AddPostScaleShader() : create pixel shader \"{}\" FAILED!", name);
			m_pPostScaleShaders.pop_back();
		}
		pShaderCode->Release();
	}
	return hr;
}
ISubPicAllocator *CDX11VideoProcessor::GetSubPicAllocator()
{
	if (!m_pSubPicAllocator && m_pDevice)
	{
		m_pSubPicAllocator = new CDX11SubPicAllocator(m_pDevice, {1280, 720});
	}
	return m_pSubPicAllocator;
}
void CDX11VideoProcessor::UpdateStatsPresent()
{
	DXGI_SWAP_CHAIN_DESC1 swapchain_desc;
	if (m_pDXGISwapChain1 && S_OK == m_pDXGISwapChain1->GetDesc1(&swapchain_desc))
	{
		m_strStatsPresent.assign(L"\nPresentation  : ");
		if (m_bVBlankBeforePresent && m_pDXGIOutput)
		{
			m_strStatsPresent.append(L"wait VBlank, ");
		}
		switch (swapchain_desc.SwapEffect)
		{
		case DXGI_SWAP_EFFECT_DISCARD:
			m_strStatsPresent.append(L"Discard");
			break;
		case DXGI_SWAP_EFFECT_SEQUENTIAL:
			m_strStatsPresent.append(L"Sequential");
			break;
		case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
			m_strStatsPresent.append(L"Flip sequential");
			break;
		case DXGI_SWAP_EFFECT_FLIP_DISCARD:
			m_strStatsPresent.append(L"Flip discard");
			break;
		}
		m_strStatsPresent.append(L", ");
		m_strStatsPresent.append(DXGIFormatToString(swapchain_desc.Format));
	}
}
void CDX11VideoProcessor::UpdateStatsStatic()
{
	if (m_srcParams.cformat)
	{
		m_strStatsHeader = std::format(L"MPC VR {}, Direct3D 11, Windows {}", _CRT_WIDE(VERSION_STR), GetWindowsVersion());
		UpdateStatsInputFmt();
		m_strStatsVProc.assign(L"\nVideoProcessor: ");
		if (m_D3D11VP_StreamInfo.IsReady())
		{
			m_strStatsVProc += std::format(L"D3D11 VP, output to {}", DXGIFormatToString(m_D3D11OutputFmt));
		}
		else
		{
			m_strStatsVProc.append(L"Shaders");
			if (m_srcParams.Subsampling == 420 || m_srcParams.Subsampling == 422)
			{
				m_strStatsVProc.append(L", Chroma scaling: ");
				switch (m_iChromaScaling)
				{
				case CHROMA_Nearest:
					m_strStatsVProc.append(L"Nearest-neighbor");
					break;
				case CHROMA_Bilinear:
					m_strStatsVProc.append(L"Bilinear");
					break;
				case CHROMA_CatmullRom:
					m_strStatsVProc.append(L"Catmull-Rom");
					break;
				}
			}
		}
		m_strStatsVProc += std::format(L"\nInternalFormat: {}", DXGIFormatToString(m_InternalTexFmt));
		if (SourceIsHDR() || m_bVPUseRTXVideoHDR)
		{
			m_strStatsHDR.assign(L"\nHDR processing: ");
			if (m_bHdrSupport && (m_bHdrPassthrough || m_bHdrLocalToneMapping))
			{
				if (m_bHdrPassthrough)
				{
					m_strStatsHDR.append(L"Passthrough");
				}
				else if (m_bHdrLocalToneMapping)
				{
					m_strStatsHDR.append(L"Local tone mapping:");
					switch (m_iHdrLocalToneMappingType)
					{
					case 1:
						m_strStatsHDR.append(L" ACES");
						break;
					case 2:
						m_strStatsHDR.append(L" Reinhard");
						break;
					case 3:
						m_strStatsHDR.append(L" Habel");
						break;
					case 4:
						m_strStatsHDR.append(L" Mobius");
						break;
					}
					m_strStatsHDR.append(std::format(L"\n Display Max Nits: {:.1f} nits", m_fHdrDisplayMaxNits));
				}
				if (m_bVPUseRTXVideoHDR)
				{
					m_strStatsHDR.append(L", RTX Video HDR*");
				}
				if (m_lastHdr10.bValid)
				{
					m_strStatsHDR += std::format(L"\n Timestamp {}", m_lastHdr10.timestamp);
					m_strStatsHDR += std::format(L"\n Mastering {}/{} nits", m_lastHdr10.hdr10.MinMasteringLuminance / 10000, m_lastHdr10.hdr10.MaxMasteringLuminance);
					m_strStatsHDR += std::format(L", maxCLL {} nits", m_lastHdr10.hdr10.MaxContentLightLevel);
					m_strStatsHDR += std::format(L", maxFALL {} nits", m_lastHdr10.hdr10.MaxFrameAverageLightLevel);
					m_strStatsHDR += std::format(L"\n RED {} {}", m_lastHdr10.hdr10.RedPrimary[0], m_lastHdr10.hdr10.RedPrimary[1]);
					m_strStatsHDR += std::format(L"\n GREEN {} {}", m_lastHdr10.hdr10.GreenPrimary[0], m_lastHdr10.hdr10.GreenPrimary[1]);
					m_strStatsHDR += std::format(L"\n BLUE {} {}", m_lastHdr10.hdr10.BluePrimary[0], m_lastHdr10.hdr10.BluePrimary[1]);
					m_strStatsHDR += std::format(L"\n White {} {}", m_lastHdr10.hdr10.WhitePoint[0], m_lastHdr10.hdr10.WhitePoint[1]);
				}
				if (m_hdr10.bValid)
				{
					m_strStatsHDR += std::format(L"\n Timestamp {}", m_hdr10.timestamp);
					m_strStatsHDR += std::format(L"\n Mastering {}/{} nits", m_hdr10.hdr10.MinMasteringLuminance / 10000, m_hdr10.hdr10.MaxMasteringLuminance);
					m_strStatsHDR += std::format(L", maxCLL {} nits", m_hdr10.hdr10.MaxContentLightLevel);
					m_strStatsHDR += std::format(L", maxFALL {} nits", m_hdr10.hdr10.MaxFrameAverageLightLevel);
					m_strStatsHDR += std::format(L"\n RED {} {}", m_hdr10.hdr10.RedPrimary[0], m_hdr10.hdr10.RedPrimary[1]);
					m_strStatsHDR += std::format(L"\n GREEN {} {}", m_hdr10.hdr10.GreenPrimary[0], m_hdr10.hdr10.GreenPrimary[1]);
					m_strStatsHDR += std::format(L"\n BLUE {} {}", m_hdr10.hdr10.BluePrimary[0], m_hdr10.hdr10.BluePrimary[1]);
					m_strStatsHDR += std::format(L"\n White {} {}", m_hdr10.hdr10.WhitePoint[0], m_hdr10.hdr10.WhitePoint[1]);
				}
			}
			else if (m_bConvertToSdr)
			{
				m_strStatsHDR.append(L"Convert to SDR");
			}
			else
			{
				m_strStatsHDR.append(L"Not used");
			}
		}
		else
		{
			m_strStatsHDR.clear();
		}
		if (m_Dovi.bValid)
		{
			m_strStatsHDR += std::format(L"\nDoblyVision: Metadata: {} Bit: ", m_Dovi.msd.ColorMetadata.signal_bit_depth);
		}
		else
		{
			m_strStatsHDR += std::format(L"\nDoblyVision: not valid");
		}
		UpdateStatsPresent();
	}
	else
	{
		m_strStatsHeader = L"Error";
		m_strStatsVProc.clear();
		m_strStatsInputFmt.clear();
		m_strStatsHDR.clear();
		m_strStatsPresent.clear();
	}
}
HRESULT CDX11VideoProcessor::DrawStats(ID3D11Texture2D *pRenderTargetTex)
{
	if (m_windowRect.IsRectEmpty())
	{
		return E_ABORT;
	}
	std::wstring str;
	str.reserve(700);
	str.assign(m_strStatsHeader);
	str.append(m_strStatsDispInfo);
	str += std::format(L"\nGraph. Adapter: {}", m_strAdapterDescription);
	wchar_t frametype = (m_SampleFormat != D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE) ? 'i' : 'p';
	str += std::format(
		L"\nFrame rate    : {:7.3f}{},{:7.3f}",
		m_pFilter->m_FrameStats.GetAverageFps(),
		frametype,
		m_pFilter->m_DrawStats.GetAverageFps());
	str.append(m_strStatsInputFmt);
	if (m_Dovi.bValid && m_Dovi.bHasMMR)
	{
		str.append(L", MMR");
	}
	str.append(m_strStatsVProc);
	const int dstW = m_videoRect.Width();
	const int dstH = m_videoRect.Height();
	if (m_iRotation)
	{
		str += std::format(L"\nScaling       : {}x{} r{}\u00B0> {}x{}", m_srcRectWidth, m_srcRectHeight, m_iRotation, dstW, dstH);
	}
	else
	{
		str += std::format(L"\nScaling       : {}x{} -> {}x{}", m_srcRectWidth, m_srcRectHeight, dstW, dstH);
	}
	if (m_srcRectWidth != dstW || m_srcRectHeight != dstH)
	{
		if (m_D3D11VP_StreamInfo.IsReady() && m_bVPScaling && !m_bVPScalingUseShaders)
		{
			str.append(L" D3D11");
			if (m_bVPUseSuperRes)
			{
				str.append(L" SuperResolution*");
			}
		}
		else
		{
			str += L' ';
			if (m_strShaderX)
			{
				str.append(m_strShaderX);
				if (m_strShaderY && m_strShaderY != m_strShaderX)
				{
					str += L'/';
					str.append(m_strShaderY);
				}
			}
			else if (m_strShaderY)
			{
				str.append(m_strShaderY);
			}
		}
	}
	if (m_strCorrection || m_pPostScaleShaders.size() || m_bDitherUsed)
	{
		str.append(L"\nPostProcessing:");
		if (m_strCorrection)
		{
			str += std::format(L" {},", m_strCorrection);
		}
		if (m_pPostScaleShaders.size())
		{
			str += std::format(L" shaders[{}],", m_pPostScaleShaders.size());
		}
		if (m_bDitherUsed)
		{
			str.append(L" dither");
		}
		str_trim_end(str, ',');
	}
	str.append(m_strStatsHDR);
	str.append(m_strStatsPresent);
	str += std::format(L"\nFrames: {:5}, skipped: {}/{}, failed: {}", m_pFilter->m_FrameStats.GetFrames(), m_pFilter->m_DrawStats.m_dropped, m_RenderStats.dropped2, m_RenderStats.failed);
	str += std::format(L"\nTimes(ms): Copy{:3}, Paint{:3}, Present{:3}", m_RenderStats.copyticks * 1000 / GetPreciseTicksPerSecondI(), m_RenderStats.paintticks * 1000 / GetPreciseTicksPerSecondI(), m_RenderStats.presentticks * 1000 / GetPreciseTicksPerSecondI());
	str += std::format(L"\nSync offset   : {:+3} ms", (m_RenderStats.syncoffset + 5000) / 10000);
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTargetTex, nullptr, &pRenderTargetView);
	if (S_OK == hr)
	{
		SIZE rtSize = m_windowRect.Size();
		m_StatsBackground.Draw(pRenderTargetView, rtSize);
		hr = m_Font3D.Draw2DText(pRenderTargetView, rtSize, m_StatsTextPoint.x, m_StatsTextPoint.y, m_dwStatsTextColor, str.c_str());
		static int col = m_StatsRect.right;
		if (--col < m_StatsRect.left)
		{
			col = m_StatsRect.right;
		}
		m_Rect3D.Set({col, m_StatsRect.bottom - 11, col + 5, m_StatsRect.bottom - 1}, rtSize, D3DCOLOR_XRGB(128, 255, 128));
		m_Rect3D.Draw(pRenderTargetView, rtSize);
		if (CheckGraphPlacement())
		{
			m_Underlay.Draw(pRenderTargetView, rtSize);
			m_Lines.Draw();
			m_SyncLine.ClearPoints(rtSize);
			m_SyncLine.AddGFPoints(m_GraphRect.left, m_Xstep, m_Yaxis, m_Yscale, m_Syncs.Data(), m_Syncs.OldestIndex(), m_Syncs.Size(), D3DCOLOR_XRGB(100, 200, 100));
			m_SyncLine.UpdateVertexBuffer();
			m_SyncLine.Draw();
		}
	}
	return hr;
}
STDMETHODIMP CDX11VideoProcessor::SetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *pValues)
{
	CheckPointer(pValues, E_POINTER);
	if (m_srcParams.cformat == CF_NONE)
	{
		return MF_E_TRANSFORM_TYPE_NOT_SET;
	}
	if (dwFlags & DXVA2_ProcAmp_Brightness)
	{
		m_DXVA2ProcAmpValues.Brightness.ll = std::clamp(pValues->Brightness.ll, m_DXVA2ProcAmpRanges[0].MinValue.ll, m_DXVA2ProcAmpRanges[0].MaxValue.ll);
	}
	if (dwFlags & DXVA2_ProcAmp_Contrast)
	{
		m_DXVA2ProcAmpValues.Contrast.ll = std::clamp(pValues->Contrast.ll, m_DXVA2ProcAmpRanges[1].MinValue.ll, m_DXVA2ProcAmpRanges[1].MaxValue.ll);
	}
	if (dwFlags & DXVA2_ProcAmp_Hue)
	{
		m_DXVA2ProcAmpValues.Hue.ll = std::clamp(pValues->Hue.ll, m_DXVA2ProcAmpRanges[2].MinValue.ll, m_DXVA2ProcAmpRanges[2].MaxValue.ll);
	}
	if (dwFlags & DXVA2_ProcAmp_Saturation)
	{
		m_DXVA2ProcAmpValues.Saturation.ll = std::clamp(pValues->Saturation.ll, m_DXVA2ProcAmpRanges[3].MinValue.ll, m_DXVA2ProcAmpRanges[3].MaxValue.ll);
	}
	if (dwFlags & DXVA2_ProcAmp_Mask)
	{
		CAutoLock cRendererLock(&m_pFilter->m_RendererLock);
		m_D3D11VP_StreamInfo.SetProcAmpValues(&m_DXVA2ProcAmpValues);
		if (!m_D3D11VP_StreamInfo.IsReady())
		{
			SetShaderConvertColorParams();
		}
	}
	return S_OK;
}
STDMETHODIMP CDX11VideoProcessor::SetAlphaBitmap(const MFVideoAlphaBitmap *pBmpParms)
{
	CheckPointer(pBmpParms, E_POINTER);
	CAutoLock cRendererLock(&m_pFilter->m_RendererLock);
	HRESULT hr = S_FALSE;
	if (pBmpParms->GetBitmapFromDC && pBmpParms->bitmap.hdc)
	{
		HBITMAP hBitmap = (HBITMAP)GetCurrentObject(pBmpParms->bitmap.hdc, OBJ_BITMAP);
		if (!hBitmap)
		{
			return E_INVALIDARG;
		}
		DIBSECTION info = {0};
		if (!::GetObjectW(hBitmap, sizeof(DIBSECTION), &info))
		{
			return E_INVALIDARG;
		}
		BITMAP &bm = info.dsBm;
		if (!bm.bmWidth || !bm.bmHeight || bm.bmBitsPixel != 32 || !bm.bmBits)
		{
			return E_INVALIDARG;
		}
		hr = m_TexAlphaBitmap.CheckCreate(m_pDevice, DXGI_FORMAT_B8G8R8A8_UNORM, bm.bmWidth, bm.bmHeight, Tex2D_DefaultShader);
		DLogIf(FAILED(hr), L"CDX11VideoProcessor::SetAlphaBitmap() : CheckCreate() failed with error {}", HR2Str(hr));
		if (S_OK == hr)
		{
			m_pDeviceContext->UpdateSubresource(m_TexAlphaBitmap.pTexture, 0, nullptr, bm.bmBits, bm.bmWidthBytes, 0);
		}
	}
	else
	{
		return E_INVALIDARG;
	}
	m_bAlphaBitmapEnable = SUCCEEDED(hr) && m_TexAlphaBitmap.pShaderResource;
	if (m_bAlphaBitmapEnable)
	{
		m_AlphaBitmapRectSrc = {0, 0, (LONG)m_TexAlphaBitmap.desc.Width, (LONG)m_TexAlphaBitmap.desc.Height};
		m_AlphaBitmapNRectDest = {0, 0, 1, 1};
		m_pAlphaBitmapVertex.Release();
		hr = UpdateAlphaBitmapParameters(&pBmpParms->params);
	}
	return hr;
}
STDMETHODIMP CDX11VideoProcessor::UpdateAlphaBitmapParameters(const MFVideoAlphaBitmapParams *pBmpParms)
{
	CheckPointer(pBmpParms, E_POINTER);
	CAutoLock cRendererLock(&m_pFilter->m_RendererLock);
	if (m_bAlphaBitmapEnable)
	{
		if (pBmpParms->dwFlags & MFVideoAlphaBitmap_SrcRect)
		{
			m_AlphaBitmapRectSrc = pBmpParms->rcSrc;
			m_pAlphaBitmapVertex.Release();
		}
		if (!m_pAlphaBitmapVertex)
		{
			HRESULT hr = CreateVertexBuffer(m_pDevice, &m_pAlphaBitmapVertex, m_TexAlphaBitmap.desc.Width, m_TexAlphaBitmap.desc.Height, m_AlphaBitmapRectSrc, 0, false);
			if (FAILED(hr))
			{
				m_bAlphaBitmapEnable = false;
				return hr;
			}
		}
		if (pBmpParms->dwFlags & MFVideoAlphaBitmap_DestRect)
		{
			m_AlphaBitmapNRectDest = pBmpParms->nrcDest;
		}
		DWORD validFlags = MFVideoAlphaBitmap_SrcRect | MFVideoAlphaBitmap_DestRect;
		return ((pBmpParms->dwFlags & validFlags) == validFlags) ? S_OK : S_FALSE;
	}
	else
	{
		return MF_E_NOT_INITIALIZED;
	}
}
CDX11VideoProcessor::~CDX11VideoProcessor()
{
	ReleaseDevice();
	ReleaseSwapChain();

	if (pOrigSetWindowPosDX11)
	{
		if (MH_DisableHook(pOrigSetWindowPosDX11) == MH_OK)
		{
			MH_RemoveHook(pOrigSetWindowPosDX11);
		}
		pOrigSetWindowPosDX11 = nullptr;
	}
	if (pOrigSetWindowLongADX11)
	{
		if (MH_DisableHook(pOrigSetWindowLongADX11) == MH_OK)
		{
			MH_RemoveHook(pOrigSetWindowLongADX11);
		}
		pOrigSetWindowLongADX11 = nullptr;
	}
	// Note: MH_Uninitialize() is typically called once at global application shutdown.
}