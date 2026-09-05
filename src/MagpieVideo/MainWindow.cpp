// Copyright (c) Xu
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.


#include "pch.h"
#include "MainWindow.h"
#include "Logger.h"
#include "MagpieVideoConstants.h"
#include "resource.h"
#include "WorkbenchPage.h"

using namespace winrt;
using namespace winrt::MagpieVideo::implementation;

namespace MagpieVideo {

bool MainWindow::Create() {
	[[maybe_unused]] static Ignore _ = [] {
		const HINSTANCE hInstance = wil::GetModuleInstanceHandle();

		WNDCLASSEXW wcex{
			.cbSize = sizeof(wcex),
			.lpfnWndProc = ::Magpie::WindowBaseT<MainWindow>::_WndProc,
			.hInstance = hInstance,
			.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP)),
			.hCursor = LoadCursor(nullptr, IDC_ARROW),
			.hbrBackground = CreateSolidBrush(RGB(27, 27, 27)),
			.lpszClassName = MagpieVideoConstants::MAIN_WINDOW_CLASS_NAME
		};
		RegisterClassEx(&wcex);

		return Ignore();
	}();

	CreateWindowEx(
		0,
		MagpieVideoConstants::MAIN_WINDOW_CLASS_NAME,
		L"MagpieVideo",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		1040,
		760,
		nullptr,
		nullptr,
		wil::GetModuleInstanceHandle(),
		this
	);

	if (!Handle()) {
		Logger::Get().Win32Error("创建主窗口失败");
		return false;
	}

	// 初始化 XAML Islands
	_xamlSource = Hosting::DesktopWindowXamlSource();
	_xamlSourceNative2 = _xamlSource.try_as<IDesktopWindowXamlSourceNative2>();
	_xamlSourceNative2->AttachToWindow(Handle());
	_xamlSourceNative2->get_WindowHandle(&_hwndXamlIsland);

	_content = make_self<WorkbenchPage>().as<winrt::Windows::UI::Xaml::UIElement>();
	_xamlSource.Content(_content);

	Show();

	return true;
}

void MainWindow::Show() const {
	ShowWindow(Handle(), SW_SHOWNORMAL);
	SetForegroundWindow(Handle());
}

void MainWindow::HandleMessage(const MSG& msg) {
	// XAML Islands 会吞掉 Alt+F4，需要特殊处理
	// https://github.com/microsoft/microsoft-ui-xaml/issues/2408
	if (msg.message == WM_SYSKEYDOWN && msg.wParam == VK_F4) [[unlikely]] {
		SendMessage(GetAncestor(msg.hwnd, GA_ROOT), msg.message, msg.wParam, msg.lParam);
		return;
	}

	if (_xamlSourceNative2) {
		BOOL processed = FALSE;
		HRESULT hr = _xamlSourceNative2->PreTranslateMessage(&msg, &processed);
		if (SUCCEEDED(hr) && processed) {
			return;
		}
	}

	TranslateMessage(&msg);
	DispatchMessage(&msg);
}

LRESULT MainWindow::_MessageHandler(UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
	switch (msg) {
	case WM_SIZE:
	{
		if (wParam != SIZE_MINIMIZED && _hwndXamlIsland) {
			_UpdateIslandPosition(LOWORD(lParam), HIWORD(lParam));
		}
		return 0;
	}
	case WM_DESTROY:
	{
		// 确保关闭过程中 _content 已经为空
		_content = nullptr;

		_xamlSourceNative2 = nullptr;
		// 必须手动重置 Content，否则会内存泄露，使 WorkbenchPage 无法析构
		_xamlSource.Content(nullptr);
		_xamlSource.Close();
		_xamlSource = nullptr;
		_hwndXamlIsland = nullptr;

		LRESULT ret = base_type::_MessageHandler(msg, wParam, lParam);

		// 关闭 DesktopWindowXamlSource 后应清空消息队列以确保 WorkbenchPage 析构
		MSG msg1;
		while (PeekMessage(&msg1, nullptr, 0, 0, PM_REMOVE)) {
			DispatchMessage(&msg1);
		}

		PostQuitMessage(0);

		return ret;
	}
	}

	return base_type::_MessageHandler(msg, wParam, lParam);
}

void MainWindow::_UpdateIslandPosition(int width, int height) noexcept {
	SetWindowPos(
		_hwndXamlIsland,
		NULL,
		0,
		0,
		width,
		height,
		SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW
	);
}

}
