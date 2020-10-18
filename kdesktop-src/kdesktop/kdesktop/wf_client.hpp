// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WF_CLIENT_HPP_
#define WF_CLIENT_HPP_

#include <freerdp/freerdp.h>
// #include <freerdp/channels/channels.h>
#include <freerdp/client/cliprdr.h>

// System menu constants
// #define SYSCOMMAND_ID_SMARTSIZING 1000
	struct wfContext
	{
		rdpContext context;
		// DEFINE_RDP_CLIENT_COMMON();

		void* decoder;
/*
		int offset_x;
		int offset_y;
		int fullscreen_toggle;
		int fullscreen;
		int percentscreen;
		WCHAR* window_title;
		int client_x;
		int client_y;
		int client_width;
		int client_height;

		HANDLE keyboardThread;

		HICON icon;
		HWND hWndParent;
		HINSTANCE hInstance;
		WNDCLASSEX wndClass;
		LPCTSTR wndClassName;
		HCURSOR hDefaultCursor;

		HWND hwnd;
		POINT diff;

		wfBitmap* primary;
		wfBitmap* drawing;
		HCURSOR cursor;
		HBRUSH brush;
		HBRUSH org_brush;
		RECT update_rect;
		RECT scale_update_rect;

		DWORD mainThreadId;
		DWORD keyboardThreadId;

		// rdpFile* connectionRdpFile;

		BOOL disablewindowtracking;

		BOOL updating_scrollbars;
		BOOL xScrollVisible;
		int xMinScroll;
		int xCurrentScroll;
		int xMaxScroll;

		BOOL yScrollVisible;
		int yMinScroll;
		int yCurrentScroll;
		int yMaxScroll;

		void* clipboard;
*/
		CliprdrClientContext* cliprdr;
/*
		// wfFloatBar* floatbar;

		// RailClientContext* rail;
		wHashTable* railWindows;
		BOOL isConsole;
*/
	};

	/**
	 * Client Interface
	 */

	// FREERDP_API int RdpClientEntry(RDP_CLIENT_ENTRY_POINTS* pEntryPoints);
	// FREERDP_API int freerdp_client_set_window_size(wfContext* wfc, int width, int height);
	// FREERDP_API void wf_size_scrollbars(wfContext* wfc, UINT32 client_width, UINT32 client_height);

	BOOL wf_cliprdr_init(wfContext* wfc, CliprdrClientContext* cliprdr);
	void wf_cliprdr_uninit(wfContext* wfc, CliprdrClientContext* cliprdr);

#endif // WF_CLIENT_HPP_
