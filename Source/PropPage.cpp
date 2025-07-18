/*
 * (C) 2018-2024 see Authors.txt
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

#include "stdafx.h"
#include "resource.h"
#include "Helper.h"
#include "DisplayConfig.h"
#include "PropPage.h"
#include <CommCtrl.h>

void SetCursor(HWND hWnd, LPCWSTR lpCursorName)
{
	SetClassLongPtrW(hWnd, GCLP_HCURSOR, (LONG_PTR)::LoadCursorW(nullptr, lpCursorName));
}

void SetCursor(HWND hWnd, UINT nID, LPCWSTR lpCursorName)
{
	SetCursor(::GetDlgItem(hWnd, nID), lpCursorName);
}

inline void ComboBox_AddStringData(HWND hWnd, int nIDComboBox, LPCWSTR str, LONG_PTR data)
{
	LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_ADDSTRING, 0, (LPARAM)str);
	if (lValue != CB_ERR) {
		SendDlgItemMessageW(hWnd, nIDComboBox, CB_SETITEMDATA, lValue, data);
	}
}

inline LONG_PTR ComboBox_GetCurItemData(HWND hWnd, int nIDComboBox)
{
	LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETCURSEL, 0, 0);
	if (lValue != CB_ERR) {
		lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETITEMDATA, lValue, 0);
	}
	return lValue;
}

void ComboBox_SelectByItemData(HWND hWnd, int nIDComboBox, LONG_PTR data)
{
	LRESULT lCount = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETCOUNT, 0, 0);
	if (lCount != CB_ERR) {
		for (int idx = 0; idx < lCount; idx++) {
			const LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETITEMDATA, idx, 0);
			if (data == lValue) {
				SendDlgItemMessageW(hWnd, nIDComboBox, CB_SETCURSEL, idx, 0);
				break;
			}
		}
	}
}

// CVRMainPPage

CVRMainPPage::CVRMainPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"MainProp", lpunk, IDD_MAINPROPPAGE, IDS_MAINPROPPAGE_TITLE)
{
	DLog(L"CVRMainPPage()");
}

CVRMainPPage::~CVRMainPPage()
{
	DLog(L"~CVRMainPPage()");
}

STDMETHODIMP CVRMainPPage::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    if (riid == __uuidof(IVideoRenderer)) {
        return GetInterface((IVideoRenderer*)this, ppv);
    }
    return CBasePropertyPage::NonDelegatingQueryInterface(riid, ppv);
}

void CVRMainPPage::AddToolTip(int nID, int nStringID)
{
	if (!m_hToolTip) return;

	TOOLINFOW ti = { 0 };
	ti.cbSize = sizeof(TOOLINFOW);
	ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
	ti.hwnd = m_hWnd;
	ti.uId = (UINT_PTR)GetDlgItem(nID).m_hWnd;
	ti.lpszText = MAKEINTRESOURCEW(nStringID);
	SendMessage(m_hToolTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

void CVRMainPPage::SetControls()
{
	CheckDlgButton(IDC_CHECK1, m_SetsPP.bUseD3D11             ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK2, m_SetsPP.bShowStats            ? BST_CHECKED : BST_UNCHECKED);

	ComboBox_SelectByItemData(m_hWnd, IDC_COMBO1, m_SetsPP.iTexFormat);

	CheckDlgButton(IDC_CHECK7, m_SetsPP.VPFmts.bNV12          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK8, m_SetsPP.VPFmts.bP01x          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK9, m_SetsPP.VPFmts.bYUY2          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK4, m_SetsPP.VPFmts.bOther         ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK3, m_SetsPP.bDeintDouble          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK5, m_SetsPP.bVPScaling            ? BST_CHECKED : BST_UNCHECKED);
	SendDlgItemMessageW(IDC_COMBO8, CB_SETCURSEL, m_SetsPP.iVPSuperRes, 0);
	CheckDlgButton(IDC_CHECK19, m_SetsPP.bVPRTXVideoHDR       ? BST_CHECKED : BST_UNCHECKED);

	if (m_SetsPP.bHdrPassthrough)
	{
		ComboBox_SelectByItemData(m_hWnd, IDC_COMBO9, 0);
	} else if (m_SetsPP.bHdrLocalToneMapping)
	{
		ComboBox_SelectByItemData(m_hWnd, IDC_COMBO9, m_SetsPP.iHdrLocalToneMappingType);
	} else
	{
		ComboBox_SelectByItemData(m_hWnd, IDC_COMBO9, -1);
	}

	CheckDlgButton(IDC_CHECK18, m_SetsPP.bHdrPreferDoVi       ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK14, m_SetsPP.bConvertToSdr        ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessageW(IDC_COMBO7, CB_SETCURSEL, m_SetsPP.iHdrToggleDisplay, 0);
	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETPOS, 1, m_SetsPP.iHdrOsdBrightness);

	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETPOS, 1, m_SetsPP.iSDRDisplayNits / SDR_NITS_STEP);
	GetDlgItem(IDC_EDIT1).SetWindowTextW(std::to_wstring(m_SetsPP.iSDRDisplayNits).c_str());

	CheckDlgButton(IDC_CHECK6, m_SetsPP.bInterpolateAt50pct   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK10, m_SetsPP.bUseDither           ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK17, m_SetsPP.bDeintBlend          ? BST_CHECKED : BST_UNCHECKED);

	CheckDlgButton(IDC_CHECK11, m_SetsPP.bExclusiveFS         ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK15, m_SetsPP.bVBlankBeforePresent ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK13, m_SetsPP.bAdjustPresentTime   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK16, m_SetsPP.bReinitByDisplay     ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessageW(IDC_COMBO6, CB_SETCURSEL, m_SetsPP.iResizeStats, 0);

	SendDlgItemMessageW(IDC_COMBO5, CB_SETCURSEL, m_SetsPP.iChromaScaling, 0);
	SendDlgItemMessageW(IDC_COMBO2, CB_SETCURSEL, m_SetsPP.iUpscaling, 0);
	SendDlgItemMessageW(IDC_COMBO3, CB_SETCURSEL, m_SetsPP.iDownscaling, 0);
	SendDlgItemMessageW(IDC_COMBO4, CB_SETCURSEL, m_SetsPP.iSwapEffect, 0);

	if (!(m_SetsPP.fHdrDisplayMaxNits >= 1.0f && m_SetsPP.fHdrDisplayMaxNits <= 10000.0f))
	{
		m_SetsPP.fHdrDisplayMaxNits = 2000.0f;
	}
	wchar_t buffer[32] = {};
	swprintf_s(buffer, L"%.1f", m_SetsPP.fHdrDisplayMaxNits);
	SetDlgItemTextW(IDC_EDIT_DISPLAYMAX, buffer);

	SendDlgItemMessageW(IDC_SLIDER3, TBM_SETRANGE, 1, MAKELONG(0, 100));
	SendDlgItemMessageW(IDC_SLIDER4, TBM_SETRANGE, 1, MAKELONG(0, 200));
	SendDlgItemMessageW(IDC_SLIDER5, TBM_SETRANGE, 1, MAKELONG(0, 100));
	SendDlgItemMessageW(IDC_SLIDER6, TBM_SETRANGE, 1, MAKELONG(0, 100));

	SendDlgItemMessageW(IDC_SLIDER3, TBM_SETPOS, 1, (LONG)(m_SetsPP.fHdrDynamicRangeCompression * 100));
	SendDlgItemMessageW(IDC_SLIDER4, TBM_SETPOS, 1, (LONG)(m_SetsPP.fHdrShadowDetail * 100));
	SendDlgItemMessageW(IDC_SLIDER5, TBM_SETPOS, 1, (LONG)(m_SetsPP.fHdrColorVolumeAdaptation * 100));
	SendDlgItemMessageW(IDC_SLIDER6, TBM_SETPOS, 1, (LONG)(m_SetsPP.fHdrSceneAdaptation * 100));

	UpdateHdrParameterDisplays();

	m_oldHdrDynamicRangeCompression = m_SetsPP.fHdrDynamicRangeCompression;
	m_oldHdrShadowDetail = m_SetsPP.fHdrShadowDetail;
	m_oldHdrColorVolumeAdaptation = m_SetsPP.fHdrColorVolumeAdaptation;
	m_oldHdrSceneAdaptation = m_SetsPP.fHdrSceneAdaptation;
}

void CVRMainPPage::EnableControls()
{
	if (!IsWindows8OrGreater()) {
		const BOOL bEnable = !m_SetsPP.bUseD3D11;
		GetDlgItem(IDC_STATIC1).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC2).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK7).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK8).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK9).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK4).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK3).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK5).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC3).EnableWindow(bEnable);
		GetDlgItem(IDC_COMBO4).EnableWindow(bEnable);
	}
	else if (IsWindows10OrGreater()) {
		const BOOL bEnable = m_SetsPP.bUseD3D11;
		GetDlgItem(IDC_COMBO9).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC5).EnableWindow(bEnable);
		GetDlgItem(IDC_COMBO7).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC6).EnableWindow(bEnable);
		GetDlgItem(IDC_SLIDER1).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC7).EnableWindow(bEnable && m_SetsPP.bVPScaling);
		GetDlgItem(IDC_COMBO8).EnableWindow(bEnable && m_SetsPP.bVPScaling);
		GetDlgItem(IDC_CHECK19).EnableWindow(bEnable && m_SetsPP.bHdrPassthrough);
	}

	GetDlgItem(IDC_STATIC8).EnableWindow(m_SetsPP.bConvertToSdr);
	GetDlgItem(IDC_EDIT1).EnableWindow(m_SetsPP.bConvertToSdr);
	GetDlgItem(IDC_SLIDER2).EnableWindow(m_SetsPP.bConvertToSdr);

	GetDlgItem(IDC_EDIT_DISPLAYMAX).EnableWindow(m_SetsPP.bHdrLocalToneMapping);

	GetDlgItem(IDC_STATIC101).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
	GetDlgItem(IDC_STATIC102).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
	GetDlgItem(IDC_STATIC103).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
	GetDlgItem(IDC_STATIC104).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
	GetDlgItem(IDC_STATIC105).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
	GetDlgItem(IDC_SLIDER3).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
	GetDlgItem(IDC_SLIDER4).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
	GetDlgItem(IDC_SLIDER5).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
	GetDlgItem(IDC_SLIDER6).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
	GetDlgItem(IDC_EDIT_DRC).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
	GetDlgItem(IDC_EDIT_SHADOW).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
	GetDlgItem(IDC_EDIT_COLORVOL).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
	GetDlgItem(IDC_EDIT_SCENE).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
}

void CVRMainPPage::UpdateHdrParameterDisplays()
{
	wchar_t buffer[16];

	swprintf_s(buffer, L"%.2f", m_SetsPP.fHdrDynamicRangeCompression);
	SetDlgItemTextW(IDC_EDIT_DRC, buffer);

	swprintf_s(buffer, L"%.2f", m_SetsPP.fHdrShadowDetail);
	SetDlgItemTextW(IDC_EDIT_SHADOW, buffer);

	swprintf_s(buffer, L"%.2f", m_SetsPP.fHdrColorVolumeAdaptation);
	SetDlgItemTextW(IDC_EDIT_COLORVOL, buffer);

	swprintf_s(buffer, L"%.2f", m_SetsPP.fHdrSceneAdaptation);
	SetDlgItemTextW(IDC_EDIT_SCENE, buffer);
}

HRESULT CVRMainPPage::OnConnect(IUnknown *pUnk)
{
	if (pUnk == nullptr) return E_POINTER;

	m_pVideoRenderer = pUnk;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVRMainPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}
	
	if (m_hToolTip) {
		::DestroyWindow(m_hToolTip);
		m_hToolTip = nullptr;
	}

	if (m_SetsPP.iSDRDisplayNits != m_oldSDRDisplayNits) {
		m_pVideoRenderer->GetSettings(m_SetsPP);
		m_SetsPP.iSDRDisplayNits = m_oldSDRDisplayNits;
		m_pVideoRenderer->SetSettings(m_SetsPP);
	}

	m_pVideoRenderer.Release();

	return S_OK;
}

HRESULT CVRMainPPage::OnActivate()
{
	m_hWnd = m_hwnd;

	m_hToolTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
								 WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
								 CW_USEDEFAULT, CW_USEDEFAULT,
								 CW_USEDEFAULT, CW_USEDEFAULT,
								 m_hWnd, NULL, g_hInst, NULL);
    SendMessage(m_hToolTip, TTM_SETMAXTIPWIDTH, 0, 300);

	m_pVideoRenderer->GetSettings(m_SetsPP);
	m_oldSDRDisplayNits = m_SetsPP.iSDRDisplayNits;

	if (!IsWindows7SP1OrGreater()) {
		GetDlgItem(IDC_CHECK1).EnableWindow(FALSE);
		m_SetsPP.bUseD3D11 = false;
	}
	if (!IsWindows10OrGreater()) {
		GetDlgItem(IDC_COMBO9).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC5).EnableWindow(FALSE);
		GetDlgItem(IDC_COMBO7).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC6).EnableWindow(FALSE);
		GetDlgItem(IDC_SLIDER1).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC7).EnableWindow(FALSE);
		GetDlgItem(IDC_COMBO8).EnableWindow(FALSE);
		GetDlgItem(IDC_CHECK19).EnableWindow(FALSE);
	}

	EnableControls();

	SendDlgItemMessageW(IDC_COMBO6, CB_ADDSTRING, 0, (LPARAM)L"Fixed font size");
	SendDlgItemMessageW(IDC_COMBO6, CB_ADDSTRING, 0, (LPARAM)L"Increase font by window");

	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"Auto 8/10-bit Integer",  0);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"8-bit Integer",          8);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"10-bit Integer",        10);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"16-bit Floating Point", 16);

	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"Disable");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for SD");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for \x2264 720p");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for \x2264 1080p");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for \x2264 1440p");

	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Do not change");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on (fullscreen)");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on/off (fullscreen)");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on/off");

	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Nearest-neighbor");
	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Bilinear");
	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Catmull-Rom");

	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Nearest-neighbor");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Mitchell-Netravali");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Catmull-Rom");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos2");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos3");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Jinc2m");

	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Box");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bilinear");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Hamming");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bicubic");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bicubic sharp");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Lanczos");

	SendDlgItemMessageW(IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)L"Discard");
	SendDlgItemMessageW(IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)L"Flip");

	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETRANGE, 0, MAKELONG(0, 2));
	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETTIC, 0, 1);

	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETRANGE, 0, MAKELONG(SDR_NITS_MIN / SDR_NITS_STEP, SDR_NITS_MAX / SDR_NITS_STEP));
	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETTIC, 0, SDR_NITS_DEF / SDR_NITS_STEP);
	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETLINESIZE, 0, 1);
	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETPAGESIZE, 0, 5);

	SetDlgItemTextW(IDC_EDIT2, GetNameAndVersion());

	ComboBox_AddStringData(m_hWnd, IDC_COMBO9, L"Ignore", -1);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO9, L"Passthrough to display", 0);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO9, L"Local: ACES Enhanced", 1);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO9, L"Local: Reinhard", 2);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO9, L"Local: Hable", 3);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO9, L"Local: Mobius", 4);

	SetControls();
	
	AddToolTip(IDC_CHECK1, IDS_TT_USE_D3D11);
    AddToolTip(IDC_COMBO1, IDS_TT_TEXTURE_FORMAT);
    AddToolTip(IDC_CHECK2, IDS_TT_SHOW_STATS);
    AddToolTip(IDC_COMBO9, IDS_TT_HDR_TONE_MAPPING);
    AddToolTip(IDC_EDIT_DISPLAYMAX, IDS_TT_HDR_DISPLAY_NITS);
    AddToolTip(IDC_SLIDER3, IDS_TT_DYNAMIC_RANGE);
    AddToolTip(IDC_SLIDER4, IDS_TT_SHADOW_DETAIL);
    AddToolTip(IDC_SLIDER5, IDS_TT_COLOR_VOLUME);
    AddToolTip(IDC_SLIDER6, IDS_TT_SCENE_ADAPT);
    AddToolTip(IDC_CHECK14, IDS_TT_CONVERT_SDR);
    AddToolTip(IDC_SLIDER2, IDS_TT_SDR_NITS);
    AddToolTip(IDC_CHECK10, IDS_TT_USE_DITHERING);
    AddToolTip(IDC_CHECK11, IDS_TT_EXCLUSIVE_FS);
    AddToolTip(IDC_CHECK15, IDS_TT_VBLANK);
    AddToolTip(IDC_CHECK13, IDS_TT_FRAME_TIME);
    AddToolTip(IDC_CHECK16, IDS_TT_REINIT_DISPLAY);

	SetCursor(m_hWnd, IDC_ARROW);
	SetCursor(m_hWnd, IDC_COMBO1, IDC_HAND);

	return S_OK;
}

INT_PTR CVRMainPPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_COMMAND) {
		LRESULT lValue;
		const int nID = LOWORD(wParam);
		int action = HIWORD(wParam);

		if (action == BN_CLICKED) {
			if (nID == IDC_CHECK1) {
				m_SetsPP.bUseD3D11 = IsDlgButtonChecked(IDC_CHECK1) == BST_CHECKED;
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK2) {
				m_SetsPP.bShowStats = IsDlgButtonChecked(IDC_CHECK2) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK3) {
				m_SetsPP.bDeintDouble = IsDlgButtonChecked(IDC_CHECK3) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK5) {
				m_SetsPP.bVPScaling = IsDlgButtonChecked(IDC_CHECK5) == BST_CHECKED;
				SetDirty();
				GetDlgItem(IDC_STATIC7).EnableWindow(m_SetsPP.bVPScaling && m_SetsPP.bUseD3D11 && IsWindows10OrGreater());
				GetDlgItem(IDC_COMBO8).EnableWindow(m_SetsPP.bVPScaling && m_SetsPP.bUseD3D11 && IsWindows10OrGreater());
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK6) {
				m_SetsPP.bInterpolateAt50pct = IsDlgButtonChecked(IDC_CHECK6) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK7) {
				m_SetsPP.VPFmts.bNV12 = IsDlgButtonChecked(IDC_CHECK7) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK8) {
				m_SetsPP.VPFmts.bP01x = IsDlgButtonChecked(IDC_CHECK8) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK9) {
				m_SetsPP.VPFmts.bYUY2 = IsDlgButtonChecked(IDC_CHECK9) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK4) {
				m_SetsPP.VPFmts.bOther = IsDlgButtonChecked(IDC_CHECK4) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK10) {
				m_SetsPP.bUseDither = IsDlgButtonChecked(IDC_CHECK10) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK17) {
				m_SetsPP.bDeintBlend = IsDlgButtonChecked(IDC_CHECK17) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK11) {
				m_SetsPP.bExclusiveFS = IsDlgButtonChecked(IDC_CHECK11) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK15) {
				m_SetsPP.bVBlankBeforePresent = IsDlgButtonChecked(IDC_CHECK15) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK13) {
				m_SetsPP.bAdjustPresentTime = IsDlgButtonChecked(IDC_CHECK13) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK16) {
				m_SetsPP.bReinitByDisplay = IsDlgButtonChecked(IDC_CHECK16) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK18) {
				m_SetsPP.bHdrPreferDoVi = IsDlgButtonChecked(IDC_CHECK18) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK14) {
				m_SetsPP.bConvertToSdr = IsDlgButtonChecked(IDC_CHECK14) == BST_CHECKED;
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK19) {
				m_SetsPP.bVPRTXVideoHDR = IsDlgButtonChecked(IDC_CHECK19) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}

			if (nID == IDC_BUTTON1) {
				m_SetsPP.SetDefault();
				SetControls();
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
		}

		if (action == CBN_SELCHANGE) {
			if (nID == IDC_COMBO6) {
				lValue = SendDlgItemMessageW(IDC_COMBO6, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iResizeStats) {
					m_SetsPP.iResizeStats = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO1) {
				lValue = ComboBox_GetCurItemData(m_hWnd, IDC_COMBO1);
				if (lValue != m_SetsPP.iTexFormat) {
					m_SetsPP.iTexFormat = lValue;
					SetDirty();

					GetDlgItem(IDC_CHECK19).EnableWindow(m_SetsPP.bUseD3D11 && m_SetsPP.bHdrPassthrough && m_SetsPP.iTexFormat != TEXFMT_8INT);
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO8) {
				lValue = SendDlgItemMessageW(IDC_COMBO8, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iVPSuperRes) {
					m_SetsPP.iVPSuperRes = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO7) {
				lValue = SendDlgItemMessageW(IDC_COMBO7, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iHdrToggleDisplay) {
					m_SetsPP.iHdrToggleDisplay = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO5) {
				lValue = SendDlgItemMessageW(IDC_COMBO5, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iChromaScaling) {
					m_SetsPP.iChromaScaling = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO2) {
				lValue = SendDlgItemMessageW(IDC_COMBO2, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iUpscaling) {
					m_SetsPP.iUpscaling = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO3) {
				lValue = SendDlgItemMessageW(IDC_COMBO3, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iDownscaling) {
					m_SetsPP.iDownscaling = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO4) {
				lValue = SendDlgItemMessageW(IDC_COMBO4, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iSwapEffect) {
					m_SetsPP.iSwapEffect = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO9) {
				lValue = SendDlgItemMessageW(IDC_COMBO9, CB_GETCURSEL, 0, 0);
				switch (lValue) {
				case 0:
					m_SetsPP.bHdrPassthrough = false;
					m_SetsPP.bHdrLocalToneMapping = false;
					break;
				case 1:
					m_SetsPP.bHdrPassthrough = true;
					m_SetsPP.bHdrLocalToneMapping = false;
					break;
				case 2:
					m_SetsPP.bHdrPassthrough = false;
					m_SetsPP.bHdrLocalToneMapping = true;
					m_SetsPP.iHdrLocalToneMappingType = 1;
					break;
				case 3:
					m_SetsPP.bHdrPassthrough = false;
					m_SetsPP.bHdrLocalToneMapping = true;
					m_SetsPP.iHdrLocalToneMappingType = 2;
					break;
				case 4:
					m_SetsPP.bHdrPassthrough = false;
					m_SetsPP.bHdrLocalToneMapping = true;
					m_SetsPP.iHdrLocalToneMappingType = 3;
					break;
				case 5:
					m_SetsPP.bHdrPassthrough = false;
					m_SetsPP.bHdrLocalToneMapping = true;
					m_SetsPP.iHdrLocalToneMappingType = 4;
					break;
				default:
					break;
				}
				SetDirty();
				EnableControls();
				return (LRESULT)1;
			}
		}
		if (action == EN_CHANGE)
		{
			if (nID == IDC_EDIT_DISPLAYMAX)
			{
				SetDirty();
			}
		}
	}
	else if (uMsg == WM_HSCROLL) {
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER1)) {
			LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER1, TBM_GETPOS, 0, 0);
			if (lValue != m_SetsPP.iHdrOsdBrightness) {
				m_SetsPP.iHdrOsdBrightness = lValue;
				SetDirty();
			}
			return (LRESULT)1;
		}
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER3)) {
			LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER3, TBM_GETPOS, 0, 0);
			float newValue = lValue / 100.0f;
			if (abs(newValue - m_SetsPP.fHdrDynamicRangeCompression) > 0.001f) {
				m_SetsPP.fHdrDynamicRangeCompression = newValue;
				UpdateHdrParameterDisplays();
				SetDirty();
			}
			return (LRESULT)1;
		}
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER4)) {
			LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER4, TBM_GETPOS, 0, 0);
			float newValue = lValue / 100.0f;
			if (abs(newValue - m_SetsPP.fHdrShadowDetail) > 0.001f) {
				m_SetsPP.fHdrShadowDetail = newValue;
				UpdateHdrParameterDisplays();
				SetDirty();
			}
			return (LRESULT)1;
		}
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER5)) {
			LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER5, TBM_GETPOS, 0, 0);
			float newValue = lValue / 100.0f;
			if (abs(newValue - m_SetsPP.fHdrColorVolumeAdaptation) > 0.001f) {
				m_SetsPP.fHdrColorVolumeAdaptation = newValue;
				UpdateHdrParameterDisplays();
				SetDirty();
			}
			return (LRESULT)1;
		}
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER6)) {
			LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER6, TBM_GETPOS, 0, 0);
			float newValue = lValue / 100.0f;
			if (abs(newValue - m_SetsPP.fHdrSceneAdaptation) > 0.001f) {
				m_SetsPP.fHdrSceneAdaptation = newValue;
				UpdateHdrParameterDisplays();
				SetDirty();
			}
			return (LRESULT)1;
		}
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER2)) {
			LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER2, TBM_GETPOS, 0, 0);
			lValue *= SDR_NITS_STEP;
			if (lValue != m_SetsPP.iSDRDisplayNits) {
				m_SetsPP.iSDRDisplayNits = lValue;
				GetDlgItem(IDC_EDIT1).SetWindowTextW(std::to_wstring(m_SetsPP.iSDRDisplayNits).c_str());
				SetDirty();
				{
					Settings_t sets;
					m_pVideoRenderer->GetSettings(sets);
					sets.iSDRDisplayNits = m_SetsPP.iSDRDisplayNits;
					m_pVideoRenderer->SetSettings(sets);
				}
			}
			return (LRESULT)1;
		}
	}

	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CVRMainPPage::OnApplyChanges()
{
	wchar_t data[32] = {};
	GetDlgItemTextW(IDC_EDIT_DISPLAYMAX, data, 32);
	float displayMaxNits;
	try {
		displayMaxNits = std::stof(data);
	} catch (const std::exception&) {
		MessageBoxW(L"Invalid HDR Brightness. Please enter a valid number from 1.0 to 10000.0.", L"Error", MB_OK | MB_ICONERROR);
		return S_FALSE;
	}

	if (displayMaxNits < HDR_NITS_MIN || displayMaxNits > HDR_NITS_MAX) {
		MessageBoxW(L"Invalid HDR Brightness. Please enter a valid number from 1.0 to 10000.0.", L"Error", MB_OK | MB_ICONERROR);
		return S_FALSE;
	}
	m_SetsPP.fHdrDisplayMaxNits = displayMaxNits;

	m_pVideoRenderer->SetSettings(m_SetsPP);
	m_pVideoRenderer->SaveSettings();

	m_oldSDRDisplayNits = m_SetsPP.iSDRDisplayNits;
	
	m_oldHdrDynamicRangeCompression = m_SetsPP.fHdrDynamicRangeCompression;
	m_oldHdrShadowDetail = m_SetsPP.fHdrShadowDetail;
	m_oldHdrColorVolumeAdaptation = m_SetsPP.fHdrColorVolumeAdaptation;
	m_oldHdrSceneAdaptation = m_SetsPP.fHdrSceneAdaptation;

	return S_OK;
}

// CVRInfoPPage

CVRInfoPPage::CVRInfoPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"InfoProp", lpunk, IDD_INFOPROPPAGE, IDS_INFOPROPPAGE_TITLE)
{
	DLog(L"CVRInfoPPage()");
}

CVRInfoPPage::~CVRInfoPPage()
{
	DLog(L"~CVRInfoPPage()");

	if (m_hMonoFont) {
		DeleteObject(m_hMonoFont);
		m_hMonoFont = 0;
	}
}

STDMETHODIMP CVRInfoPPage::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    if (riid == __uuidof(IVideoRenderer)) {
        return GetInterface((IVideoRenderer*)this, ppv);
    }
    return CBasePropertyPage::NonDelegatingQueryInterface(riid, ppv);
}


HRESULT CVRInfoPPage::OnConnect(IUnknown *pUnk)
{
	if (pUnk == nullptr) return E_POINTER;

	m_pVideoRenderer = pUnk;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVRInfoPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}

	m_pVideoRenderer.Release();

	return S_OK;
}

HWND GetParentOwner(HWND hwnd)
{
	HWND hWndParent = hwnd;
	HWND hWndT;
	while ((::GetWindowLongPtrW(hWndParent, GWL_STYLE) & WS_CHILD) &&
		(hWndT = ::GetParent(hWndParent)) != NULL) {
		hWndParent = hWndT;
	}

	return hWndParent;
}

static WNDPROC OldControlProc;
static LRESULT CALLBACK ControlProc(HWND control, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_KEYDOWN && LOWORD(wParam) == VK_ESCAPE) {
		HWND parentOwner = GetParentOwner(control);
		if (parentOwner) {
			::PostMessageW(parentOwner, WM_COMMAND, IDCANCEL, 0);
		}
		return TRUE;
	}

	return CallWindowProcW(OldControlProc, control, message, wParam, lParam);
}

HRESULT CVRInfoPPage::OnActivate()
{
	m_hWnd = m_hwnd;

	SetDlgItemTextW(IDC_EDIT2, GetNameAndVersion());

	LOGFONTW lf = {};
	HDC hdc = GetWindowDC();
	lf.lfHeight = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(hdc);
	lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
	wcscpy_s(lf.lfFaceName, L"Consolas");
	m_hMonoFont = CreateFontIndirectW(&lf);

	GetDlgItem(IDC_EDIT1).SetFont(m_hMonoFont);
	ASSERT(m_pVideoRenderer);

	if (!m_pVideoRenderer->GetActive()) {
		SetDlgItemTextW(IDC_EDIT1, L"filter is not active");
		return S_OK;
	}

	std::wstring strInfo(L"Windows ");
	strInfo.append(GetWindowsVersion());
	strInfo.append(L"\r\n");

	std::wstring strVP;
	if (S_OK == m_pVideoRenderer->GetVideoProcessorInfo(strVP)) {
		str_replace(strVP, L"\n", L"\r\n");
		strInfo.append(strVP);
	}

#ifdef _DEBUG
	{
		std::vector<DisplayConfig_t> displayConfigs;

		bool ret = GetDisplayConfigs(displayConfigs);

		strInfo.append(L"\r\n");

		for (const auto& dc : displayConfigs) {
			double freq = (double)dc.refreshRate.Numerator / (double)dc.refreshRate.Denominator;
			strInfo += std::format(L"\r\n{} - {:.3f} Hz", dc.displayName, freq);

			if (dc.bitsPerChannel) {
				const wchar_t* colenc = ColorEncodingToString(dc.colorEncoding);
				if (colenc) {
					strInfo += std::format(L" {}", colenc);
				}
				strInfo += std::format(L" {}-bit", dc.bitsPerChannel);
			}

			const wchar_t* output = OutputTechnologyToString(dc.outputTechnology);
			if (output) {
				strInfo += std::format(L" {}", output);
			}
		}
	}
#endif

	SetDlgItemTextW(IDC_EDIT1, strInfo.c_str());

	OldControlProc = (WNDPROC)::SetWindowLongPtrW(::GetDlgItem(m_hWnd, IDC_EDIT1), GWLP_WNDPROC, (LONG_PTR)ControlProc);
	return S_OK;
}