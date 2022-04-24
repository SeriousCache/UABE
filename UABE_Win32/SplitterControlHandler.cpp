#include "stdafx.h"
#include "SplitterControlHandler.h"
#include <stdint.h>
#include <windowsx.h>

template <>
bool SplitterControlHandler<true>::testCursorOverSplitter()
{
	//Horizontal check (x-axis).
	POINT cursor;
	RECT separateRect;
	if (GetCursorPos(&cursor) && ScreenToClient(hContentSeparate, &cursor)
		&& GetClientRect(hContentSeparate, &separateRect))
	{
		int x = cursor.x;
		int y = cursor.y;
		if (x >= -3 && x <= 3 && y >= 0 && y <= (separateRect.bottom - separateRect.top))
		{
			return true;
		}
	}
	return false;
}
template <>
bool SplitterControlHandler<false>::testCursorOverSplitter()
{
	//Vertical check (y-axis).
	POINT cursor;
	RECT separateRect;
	if (GetCursorPos(&cursor) && ScreenToClient(hContentSeparate, &cursor)
		&& GetClientRect(hContentSeparate, &separateRect))
	{
		int x = cursor.x;
		int y = cursor.y;
		if (y >= -3 && y <= 3 && x >= 0 && x <= (separateRect.right - separateRect.left))
		{
			return true;
		}
	}
	return false;
}

template <>
bool SplitterControlHandler<true>::onMouseMove(HWND hParent, int x, int y)
{
	if (!this->splitterDown)
		return false;
	//Horizontal check (x-axis).

	LONG newSplitterX = x - this->splitterOffset;

	RECT clientRect;
	if (GetClientRect(hParent, &clientRect) && (clientRect.right - clientRect.left) > 20)
	{
		LONG windowWidth = clientRect.right - clientRect.left;
		if (newSplitterX < 5) newSplitterX = 5;
		else if (newSplitterX > (windowWidth - 5)) newSplitterX = windowWidth - 5;

		float newRatio = (float)newSplitterX / (float)windowWidth;
		if (newRatio < ratioMin) newRatio = ratioMin;
		else if (newRatio > ratioMax) newRatio = ratioMax;

		this->leftOrTopPanelRatio = newRatio;
		this->requestResize = true;
		return true;
	}
	return false;
}

template <>
bool SplitterControlHandler<false>::onMouseMove(HWND hParent, int x, int y)
{
	if (!this->splitterDown)
		return false;
	//Vertical check (y-axis).

	LONG newSplitterY = y - this->splitterOffset;

	RECT clientRect;
	if (GetClientRect(hParent, &clientRect) && (clientRect.bottom - clientRect.top) > 20)
	{
		LONG windowHeight = clientRect.bottom - clientRect.top;
		if (newSplitterY < 5) newSplitterY = 5;
		else if (newSplitterY > (windowHeight - 5)) newSplitterY = windowHeight - 5;

		float newRatio = (float)newSplitterY / (float)windowHeight;
		if (newRatio < ratioMin) newRatio = ratioMin;
		else if (newRatio > ratioMax) newRatio = ratioMax;

		this->leftOrTopPanelRatio = newRatio;
		this->requestResize = true;
		return true;
	}
	return false;
}

template <bool horizontal>
bool SplitterControlHandler<horizontal>::handleWin32Message(HWND hParent, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
		this->splitterDown = false;
		break;
	case WM_MOUSEMOVE:
		if (this->hContentSeparate != NULL)
			return this->onMouseMove(hParent, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		break;
	case WM_LBUTTONDOWN:
		if (this->hContentSeparate != NULL && !this->splitterDown && testCursorOverSplitter())
		{
			this->splitterDown = true;
			SetCapture(hParent);

			int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);

			//Get the x/y position of the splitter in hDlg client coordinates.
			RECT separateRect = {};
			GetWindowRect(hContentSeparate, &separateRect);
			ScreenToClient(hParent, &reinterpret_cast<POINT*>(&separateRect)[0]);
			
			if (horizontal)
				this->splitterOffset = x - separateRect.left;
			else
				this->splitterOffset = y - separateRect.top;
			return true;
		}
		break;
	case WM_LBUTTONUP:
		if (this->hContentSeparate != NULL && this->splitterDown)
		{
			this->splitterDown = false;
			ReleaseCapture();
			return true;
		}
		break;
	case WM_KILLFOCUS:
		if (this->hContentSeparate != NULL && this->splitterDown)
		{
			this->splitterDown = false;
			ReleaseCapture();
			//Let the caller also process this message.
		}
		break;
	case WM_SETCURSOR:
		if (this->hContentSeparate != NULL && testCursorOverSplitter())
		{
			SetCursor(LoadCursor(NULL, horizontal ? IDC_SIZEWE : IDC_SIZENS));
			return true;
		}
		break;
	}
	return false;
}

template class SplitterControlHandler<false>; //vertical
template class SplitterControlHandler<true>; //horizontal
