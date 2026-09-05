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
#include "App.h"
#if __has_include("App.g.cpp")
#include "App.g.cpp"
#endif
#include "Logger.h"
#include "MainWindow.h"

using namespace ::MagpieVideo;
using namespace winrt;

namespace winrt::MagpieVideo::implementation {

// 提前加载 twinapi.appcore.dll 和 threadpoolwinrt.dll 以避免退出时崩溃。应在 Windows.UI.Xaml.dll
// 被加载前调用，注意避免初始化全局变量时意外加载这个 dll，尤其是为了注册 DependencyProperty。
// 来自 https://github.com/CommunityToolkit/Microsoft.Toolkit.Win32/blob/6fb2c3e00803ea563af20f6bc9363091b685d81f/Microsoft.Toolkit.Win32.UI.XamlApplication/XamlApplication.cpp#L140
// 参见 https://github.com/microsoft/microsoft-ui-xaml/issues/7260#issuecomment-1231314776
static void FixThreadPoolCrash() noexcept {
	assert(!GetModuleHandle(L"Windows.UI.Xaml.dll"));
	LoadLibraryEx(L"twinapi.appcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	LoadLibraryEx(L"threadpoolwinrt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

App& App::Get() {
	static com_ptr<App> instance = [] {
		FixThreadPoolCrash();
		return make_self<App>();
	}();

	return *instance;
}

App::App() {
	UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e) {
		Logger::Get().ComCritical("未处理的异常", e.Exception().value);

		if (IsDebuggerPresent()) {
			hstring errorMessage = e.Message();
			__debugbreak();
		}
	});
}

bool App::Initialize(const wchar_t*) {
	// 初始化 XAML 框架。退出时也不要关闭，如果正在播放动画会崩溃。文档中的清空消息队列的做法无用。
	_windowsXamlManager = Hosting::WindowsXamlManager::InitializeForCurrentThread();

	// 初始化 WindowsXamlManager 时已经创建 DispatcherQueue。
	_dispatcher = winrt::DispatcherQueue::GetForCurrentThread();

	_mainWindow = std::make_unique<class MainWindow>();

	if (!_mainWindow->Create()) {
		_Uninitialize();
		return false;
	}

	return true;
}

int App::Run() {
	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		_mainWindow->HandleMessage(msg);
	}

	_Uninitialize();

	Logger::Get().Info("程序退出");
	Logger::Get().Flush();

	return (int)msg.wParam;
}

void App::Quit() {
	_mainWindow->Destroy();
	PostQuitMessage(0);
}

void App::_Uninitialize() {
	_mainWindow.reset();
}

}
