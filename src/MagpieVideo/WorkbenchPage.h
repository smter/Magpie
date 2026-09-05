#pragma once
#include "WorkbenchPage.g.h"

namespace winrt::MagpieVideo::implementation {

struct WorkbenchPage : WorkbenchPageT<WorkbenchPage> {
	WorkbenchPage();
};

}

BASIC_FACTORY(WorkbenchPage)
