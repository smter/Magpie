#pragma once
#include "WindowBase.h"
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>

namespace MagpieVideo {

// 单页工作台的 Win32 + XAML Islands 宿主窗口。
// 只承载一个 DesktopWindowXamlSource，无自定义标题栏（T1 简化）。
class MainWindow : public ::Magpie::WindowBaseT<MainWindow> {
public:
	friend ::Magpie::WindowBaseT<MainWindow>;

	MainWindow() = default;

	bool Create();

	void Show() const;

	// XAML Islands 会吞掉 Alt+F4，需要特殊处理
	// https://github.com/microsoft/microsoft-ui-xaml/issues/2408
	void HandleMessage(const MSG& msg);

	winrt::Windows::UI::Xaml::UIElement Content() const noexcept {
		return _content;
	}

protected:
	LRESULT _MessageHandler(UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

private:
	void _UpdateIslandPosition(int width, int height) noexcept;

	HWND _hwndXamlIsland = nullptr;
	winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource _xamlSource{ nullptr };
	winrt::com_ptr<IDesktopWindowXamlSourceNative2> _xamlSourceNative2;

	winrt::Windows::UI::Xaml::UIElement _content{ nullptr };
};

}
