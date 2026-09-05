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
#include "WorkbenchPage.h"
#if __has_include("WorkbenchPage.g.cpp")
#include "WorkbenchPage.g.cpp"
#endif

namespace winrt::MagpieVideo::implementation {

WorkbenchPage::WorkbenchPage() {
	// T1：单页工作台壳，仅渲染布局占位；交互逻辑（选文件/参数/对比/处理）在后续票接入。
}

}
