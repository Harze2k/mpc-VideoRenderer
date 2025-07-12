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

#pragma once

#include "IVideoRenderer.h"

// CVRMainPPage

class __declspec(uuid("DA46D181-07D6-441D-B314-019AEB10148A"))
	CVRMainPPage : public CBasePropertyPage, public CWindow
{
	CComQIPtr<IVideoRenderer> m_pVideoRenderer;
	Settings_t m_SetsPP;
	HWND m_hToolTip = nullptr;

	int m_oldSDRDisplayNits = SDR_NITS_DEF;

	// HDR parameter tracking
	float m_oldHdrDynamicRangeCompression;
	float m_oldHdrShadowDetail;
	float m_oldHdrColorVolumeAdaptation;
	float m_oldHdrSceneAdaptation;

public:
	CVRMainPPage(LPUNKNOWN lpunk, HRESULT* phr);
	~CVRMainPPage();

private:
	void SetControls();
	void EnableControls();
	void UpdateHdrParameterDisplays();
	void AddToolTip(int nID, int nStringID);

	HRESULT OnConnect(IUnknown* pUnknown) override;
	HRESULT OnDisconnect() override;
	HRESULT OnActivate() override;
	HRESULT OnApplyChanges() override;

	void SetDirty()
	{
		m_bDirty = TRUE;
		if (m_pPageSite) {
			m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
		}
	}

	INT_PTR OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;

public:
	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);
};

// CVRInfoPPage

class __declspec(uuid("6AC2FC16-C17B-4CD3-8306-8B1F7B70E8FB"))
	CVRInfoPPage : public CBasePropertyPage, public CWindow
{
	CComQIPtr<IVideoRenderer> m_pVideoRenderer;
	HFONT m_hMonoFont = nullptr;

public:
	CVRInfoPPage(LPUNKNOWN lpunk, HRESULT* phr);
	~CVRInfoPPage();

	HRESULT OnConnect(IUnknown* pUnknown) override;
	HRESULT OnDisconnect() override;
	HRESULT OnActivate() override;

public:
	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);
};