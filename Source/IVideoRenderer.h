/*
 * (C) 2018-2025 see Authors.txt
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

#include <dxva2api.h>
#include <algorithm> // For std::clamp

enum :int {
	TEXFMT_AUTOINT = 0,
	TEXFMT_8INT = 8,
	TEXFMT_10INT = 10,
	TEXFMT_16FLOAT = 16,
};

enum :int {
	SUPERRES_Disable = 0,
	SUPERRES_SD,
	SUPERRES_720p,
	SUPERRES_1080p,
	SUPERRES_1440p,
	SUPERRES_COUNT
};

enum :int {
	CHROMA_Nearest = 0,
	CHROMA_Bilinear,
	CHROMA_CatmullRom,
	CHROMA_COUNT
};

enum :int {
	UPSCALE_Nearest = 0,
	UPSCALE_Mitchell,
	UPSCALE_CatmullRom,
	UPSCALE_Lanczos2,
	UPSCALE_Lanczos3,
	UPSCALE_Jinc2,
	UPSCALE_COUNT
};

enum :int {
	DOWNSCALE_Box = 0,
	DOWNSCALE_Bilinear,
	DOWNSCALE_Hamming,
	DOWNSCALE_Bicubic,
	DOWNSCALE_BicubicSharp,
	DOWNSCALE_Lanczos,
	DOWNSCALE_COUNT
};

enum :int {
	SWAPEFFECT_Discard = 0,
	SWAPEFFECT_Flip,
	SWAPEFFECT_COUNT
};

enum :int {
	HDRTD_Disabled = 0,
	HDRTD_On_Fullscreen,
	HDRTD_On,
	HDRTD_OnOff_Fullscreen,
	HDRTD_OnOff
};

// HDR Tone Mapping Types
enum :int {
	HDR_TM_ACES = 1,
	HDR_TM_REINHARD = 2,
	HDR_TM_HABLE = 3,
	HDR_TM_MOBIUS = 4,
	HDR_TM_COUNT
};

#define SDR_NITS_DEF 125
#define SDR_NITS_MIN  25
#define SDR_NITS_MAX 400
#define SDR_NITS_STEP  5

#define HDR_NITS_DEF 2000.0f
#define HDR_NITS_MIN 1.0f
#define HDR_NITS_MAX 10000.0f

// HDR Enhancement parameter ranges
#define HDR_DRC_MIN 0.0f
#define HDR_DRC_MAX 1.0f
#define HDR_DRC_DEF 0.5f

#define HDR_SHADOW_MIN 0.0f
#define HDR_SHADOW_MAX 2.0f
#define HDR_SHADOW_DEF 1.2f

#define HDR_COLORVOL_MIN 0.0f
#define HDR_COLORVOL_MAX 1.0f
#define HDR_COLORVOL_DEF 0.8f

#define HDR_SCENE_MIN 0.0f
#define HDR_SCENE_MAX 1.0f
#define HDR_SCENE_DEF 0.6f

struct VPEnableFormats_t {
	bool bNV12;
	bool bP01x;
	bool bYUY2;
	bool bOther;
};

struct Settings_t {
	bool bUseD3D11;
	bool bShowStats;
	int  iResizeStats;
	int  iTexFormat;
	VPEnableFormats_t VPFmts;
	bool bDeintDouble;
	bool bVPScaling;
	int iVPSuperRes;
	bool bVPRTXVideoHDR;
	int  iChromaScaling;
	int  iUpscaling;
	int  iDownscaling;
	bool bInterpolateAt50pct;
	bool bUseDither;
	bool bDeintBlend;
	int  iSwapEffect;
	bool bExclusiveFS;
	bool bVBlankBeforePresent;
	bool bAdjustPresentTime;
	bool bReinitByDisplay;
	bool bHdrPreferDoVi;
	bool bHdrPassthrough;
	int  iHdrToggleDisplay;
	int  iHdrOsdBrightness;
	bool bConvertToSdr;
	int  iSDRDisplayNits;
	bool bHdrLocalToneMapping;
	int  iHdrLocalToneMappingType;
	float fHdrDisplayMaxNits;
	
	float fHdrDynamicRangeCompression;
	float fHdrShadowDetail;
	float fHdrColorVolumeAdaptation;
	float fHdrSceneAdaptation;
	
	float fHdrHighlightRecovery;
	float fHdrSaturationBoost;
	float fHdrToneMappingExposure;
	float fHdrContentMaxNits;
	float fHdrGammaCorrection;
	float fHdrColorTemperature;

	Settings_t() {
		SetDefault();
	}

	void SetDefault() {
		if (IsWindows8OrGreater()) {
			bUseD3D11                   = true;
		} else {
			bUseD3D11                   = false;
		}
		bShowStats                      = false;
		iResizeStats                    = 0;
		iTexFormat                      = TEXFMT_AUTOINT;
		VPFmts.bNV12                    = true;
		VPFmts.bP01x                    = true;
		VPFmts.bYUY2                    = true;
		VPFmts.bOther                   = true;
		bDeintDouble                    = true;
		bVPScaling                      = true;
		iVPSuperRes                     = SUPERRES_Disable;
		bVPRTXVideoHDR                  = false;
		iChromaScaling                  = CHROMA_Bilinear;
		iUpscaling                      = UPSCALE_CatmullRom;
		iDownscaling                    = DOWNSCALE_Hamming;
		bInterpolateAt50pct             = true;
		bUseDither                      = true;
		bDeintBlend                     = false;
		iSwapEffect                     = SWAPEFFECT_Flip;
		bExclusiveFS                    = false;
		bVBlankBeforePresent            = false;
		bAdjustPresentTime              = true;
		bReinitByDisplay                = false;
		bHdrPreferDoVi                  = true; // FIX: Default to checked
		if (IsWindows10OrGreater()) {
			bHdrLocalToneMapping        = true;
			bHdrPassthrough             = false;
			iHdrLocalToneMappingType    = HDR_TM_ACES;
			fHdrDisplayMaxNits          = 600.0f; // FIX: Default nits to 600
		} else {
			bHdrLocalToneMapping        = false;
			bHdrPassthrough             = false;
			iHdrLocalToneMappingType    = 0;
			fHdrDisplayMaxNits          = 600.0f;
		}
		iHdrToggleDisplay               = HDRTD_Disabled;
		bConvertToSdr                   = true;
		iHdrOsdBrightness               = 0;
		iSDRDisplayNits                 = SDR_NITS_DEF;
		
		fHdrDynamicRangeCompression     = HDR_DRC_DEF;
		fHdrShadowDetail                = HDR_SHADOW_DEF;
		fHdrColorVolumeAdaptation       = HDR_COLORVOL_DEF;
		fHdrSceneAdaptation             = HDR_SCENE_DEF;
		
		fHdrHighlightRecovery           = 0.3f;
		fHdrSaturationBoost             = 1.1f;
		fHdrToneMappingExposure         = 1.0f;
		fHdrContentMaxNits              = 4000.0f;
		fHdrGammaCorrection             = 2.2f;
		fHdrColorTemperature            = 6500.0f;
	}

	void ValidateHDRParams() {
		fHdrDynamicRangeCompression = std::clamp(fHdrDynamicRangeCompression, HDR_DRC_MIN, HDR_DRC_MAX);
		fHdrShadowDetail = std::clamp(fHdrShadowDetail, HDR_SHADOW_MIN, HDR_SHADOW_MAX);
		fHdrColorVolumeAdaptation = std::clamp(fHdrColorVolumeAdaptation, HDR_COLORVOL_MIN, HDR_COLORVOL_MAX);
		fHdrSceneAdaptation = std::clamp(fHdrSceneAdaptation, HDR_SCENE_MIN, HDR_SCENE_MAX);
		fHdrDisplayMaxNits = std::clamp(fHdrDisplayMaxNits, HDR_NITS_MIN, HDR_NITS_MAX);
		
		fHdrHighlightRecovery = std::clamp(fHdrHighlightRecovery, 0.0f, 1.0f);
		fHdrSaturationBoost = std::clamp(fHdrSaturationBoost, 0.5f, 2.0f);
		fHdrToneMappingExposure = std::clamp(fHdrToneMappingExposure, 0.1f, 3.0f);
		fHdrContentMaxNits = std::clamp(fHdrContentMaxNits, 100.0f, 10000.0f);
		fHdrGammaCorrection = std::clamp(fHdrGammaCorrection, 1.8f, 2.6f);
		fHdrColorTemperature = std::clamp(fHdrColorTemperature, 3000.0f, 9000.0f);
	}
};

interface __declspec(uuid("1AB00F10-5F55-42AC-B53F-38649F11BE3E"))
IVideoRenderer : public IUnknown {
	STDMETHOD(GetVideoProcessorInfo) (std::wstring& str) PURE;
	STDMETHOD_(bool, GetActive()) PURE;

	STDMETHOD_(void, GetSettings(Settings_t& settings)) PURE;
	STDMETHOD_(void, SetSettings(const Settings_t& settings)) PURE;

	STDMETHOD(SaveSettings()) PURE;
};