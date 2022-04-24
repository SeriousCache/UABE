#pragma once
#include "api.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

//horizontal: Set to true if the splitter can be dragged across the x-axis;
//                   false if the splitter can be dragged across the y-axis.
template <bool horizontal>
class SplitterControlHandler
{
	HWND hContentSeparate = NULL;
	bool requestResize = false;
	float leftOrTopPanelRatio, ratioMin, ratioMax;

	bool splitterDown = false;
	int splitterOffset = 0;

	bool testCursorOverSplitter();
	bool onMouseMove(HWND hParent, int x, int y);
public:
	inline SplitterControlHandler(float leftOrTopPanelRatio, float ratioMin = 0.15f, float ratioMax = 0.8f)
		: hContentSeparate(hContentSeparate), leftOrTopPanelRatio(leftOrTopPanelRatio),
		  ratioMin(ratioMin), ratioMax(ratioMax)
	{}
	inline void setSplitterWindow(HWND hContentSeparate)
	{
		this->hContentSeparate = hContentSeparate;
	}
	//Handles a window message, and returns true if the caller shouldn't process the message further.
	UABE_Win32_API bool handleWin32Message(HWND hParent, UINT message, WPARAM wParam, LPARAM lParam);
	//Retrieves the resize flag and clears it.
	//Call after handleWin32Message to check whether the splitter position has changed.
	inline bool shouldResize()
	{
		bool ret = requestResize;
		requestResize = false;
		return ret;
	}
	inline float getLeftOrTopPanelRatio()
	{
		return leftOrTopPanelRatio;
	}
};
