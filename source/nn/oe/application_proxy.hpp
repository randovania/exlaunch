#pragma once

#include <nn/nn_common.hpp>

namespace nn::oe
{
	typedef struct
	{
		char displayVersion[16];
	} DisplayVersion;

	void GetDisplayVersion(DisplayVersion *displayVersion);
}