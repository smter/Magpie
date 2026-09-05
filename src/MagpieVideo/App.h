#pragma once
#include "App.g.h"
#include <winrt/Windows.UI.Xaml.Hosting.h>

namespace MagpieVideo {
class MainWindow;
}

namespace winrt::MagpieVideo::implementation {

class App : public App_base<App, Markup::IXamlMetadataProvider> {
public:
	static App& Get();

	App();
	App(const App&) = delete;
	App(App&&) = delete;

	bool Initialize(const wchar_t* arguments);

	int Run();

	const DispatcherQueue& Dispatcher() const noexcept {
		return _dispatcher;
	}

	const ::MagpieVideo::MainWindow& MainWindow() const noexcept {
		return *_mainWindow;
	}

	::MagpieVideo::MainWindow& MainWindow() noexcept {
		return *_mainWindow;
	}

	void Quit();

private:
	void _Uninitialize();

	Hosting::WindowsXamlManager _windowsXamlManager{ nullptr };

	std::unique_ptr<::MagpieVideo::MainWindow> _mainWindow;

	DispatcherQueue _dispatcher{ nullptr };

	////////////////////////////////////////////////////
	//
	// IXamlMetadataProvider 相关
	//
	/////////////////////////////////////////////////////
public:
	Markup::IXamlType GetXamlType(Interop::TypeName const& type) {
		return _AppProvider()->GetXamlType(type);
	}

	Markup::IXamlType GetXamlType(hstring const& fullName) {
		return _AppProvider()->GetXamlType(fullName);
	}

	com_array<Markup::XmlnsDefinition> GetXmlnsDefinitions() {
		return _AppProvider()->GetXmlnsDefinitions();
	}

private:
	com_ptr<XamlMetaDataProvider> _AppProvider() {
		if (!_appProvider) {
			_appProvider = make_self<XamlMetaDataProvider>();
		}
		return _appProvider;
	}

	com_ptr<XamlMetaDataProvider> _appProvider;
};

}

BASIC_FACTORY(App)
