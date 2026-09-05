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
#include "Logger.h"
#include "MagpieVideoConstants.h"
#include "Win32Helper.h"

using namespace MagpieVideo;
using namespace winrt::MagpieVideo::implementation;

// 将当前目录设为程序所在目录
static void SetWorkingDir() noexcept {
	FAIL_FAST_IF_WIN32_BOOL_FALSE(SetCurrentDirectory(
		Magpie::Win32Helper::GetExePath().parent_path().c_str()));
}

int APIENTRY wWinMain(
	_In_ HINSTANCE /*hInstance*/,
	_In_opt_ HINSTANCE /*hPrevInstance*/,
	_In_ wchar_t* /*lpCmdLine*/,
	_In_ int /*nCmdShow*/
) {
	// 堆损坏时终止进程
	HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, nullptr, 0);

	SetWorkingDir();

	Logger::Get().Initialize(
		spdlog::level::info,
		MagpieVideoConstants::LOG_PATH,
		MagpieVideoConstants::LOG_MAX_SIZE,
		1
	);

	Logger::Get().Info("MagpieVideo 启动");

	// 程序结束时也不应调用 uninit_apartment
	// 见 https://kennykerr.ca/2018/03/24/cppwinrt-hosting-the-windows-runtime/
	winrt::init_apartment(winrt::apartment_type::single_threaded);

	auto& app = App::Get();
	if (!app.Initialize(nullptr)) {
		return 0;
	}

	return app.Run();
}
