#include "stdafx.h"
#include "resource.h"
#include "ProgressDialog.h"

#include "../libStringConverter/convert.h"

#include <vector>

#include <WindowsX.h>

CProgressIndicator::CProgressIndicator(HINSTANCE hInstance)
	: hInstance(hInstance)
{
	started = false;
	dialogHasFocus = false;
	dialogIsActive = false;
	dontCloseIfLog = false;
	logWasCalled = false;
	totalRange = 0;
	curStepBasePos = 0;
	curStep = 0;
	curStepProgress = 0;
	hStartEvent = NULL;
	hEndEvent = NULL;
}
CProgressIndicator::~CProgressIndicator()
{
	if (this->hStartEvent != NULL)
		CloseHandle(this->hStartEvent);
	if (this->hEndEvent != NULL)
		CloseHandle(this->hEndEvent);
}
INT_PTR CALLBACK CProgressIndicator::WindowHandler(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	INT_PTR ret = (INT_PTR)FALSE;
	switch (message)
	{
	case WM_DESTROY:
		{
			CProgressIndicator *pThis = (CProgressIndicator*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			std::scoped_lock endLock(pThis->endMutex);
			SetEvent(pThis->hEndEvent);
			SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);
			pThis->selfRef.reset();

			PostQuitMessage(0);
		}
		break;
	case WM_TIMER:
		{
			CProgressIndicator *pThis = (CProgressIndicator*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if (wParam == (WPARAM)pThis)
			{
				pThis->showDelayElapsed = true;
				KillTimer(hDlg, wParam);
				ShowWindow(hDlg, SW_SHOW);
			}
		}
		break;
	case WM_INITDIALOG:
		{
			SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
			CProgressIndicator *pThis = (CProgressIndicator*)lParam;

			HWND hWndOptions = GetDlgItem(hDlg, IDC_COMBOOPTIONLIST);
			pThis->hDialog = hDlg;

			pThis->dialogHasFocus = (hDlg == GetFocus());

			SetEvent(pThis->hStartEvent);

			if (pThis->showDelay > 0)
			{
				pThis->showDelayElapsed = false;
				ShowWindow(hDlg, SW_HIDE);
				SetTimer(hDlg, (uintptr_t)pThis, pThis->showDelay, NULL);
			}
			else
			{
				pThis->showDelayElapsed = true;
				ShowWindow(hDlg, SW_SHOW);
			}

			HWND hCancelButton = GetDlgItem(hDlg, IDCANCEL);
			HWND hOKButton = GetDlgItem(hDlg, IDOK);
			ShowWindow(hCancelButton, SW_SHOW);
			ShowWindow(hOKButton, SW_HIDE);

			HWND hWndProgress = GetDlgItem(hDlg, IDC_PROG);
			EnableWindow(hWndProgress, TRUE);
		}
		return (INT_PTR)TRUE;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		switch (wmId)
		{
			case IDOK:
				{
					CProgressIndicator *pThis = (CProgressIndicator*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
					if (pThis->IsCancelled())
					{
						SendMessage(hDlg, WM_APP+0, 0, 0);
					}
				}
				return (INT_PTR)TRUE;
			case IDCANCEL:
				{
					CProgressIndicator *pThis = (CProgressIndicator*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
					if (pThis->cancellable)
					{
						pThis->SetCancellable(false);
						pThis->forceUncancellable = false;
						pThis->SetCancelled(true);
					}
				}
				return (INT_PTR)TRUE;
		}
		break;
	case WM_SETFOCUS:
		{
			CProgressIndicator *pThis = (CProgressIndicator*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			pThis->dialogHasFocus = true;
		}
		break;
	case WM_KILLFOCUS:
		{
			CProgressIndicator *pThis = (CProgressIndicator*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			pThis->dialogHasFocus = false;
		}
		break;
	case WM_ACTIVATE:
		{
			CProgressIndicator *pThis = (CProgressIndicator*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			pThis->dialogIsActive = (LOWORD(wParam) != WA_INACTIVE);
		}
		break;
	case WM_CLOSE:
		{
			CProgressIndicator *pThis = (CProgressIndicator*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if (pThis->cancellable)
				pThis->SetCancelled(true);
		}
		break;
	case WM_APP+0:
		{
			CProgressIndicator *pThis = (CProgressIndicator*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if ((!pThis->showDelayElapsed || pThis->dialogHasFocus || pThis->dialogIsActive) && pThis->hParentWindow != NULL)
				SetForegroundWindow(pThis->hParentWindow);
		}
		DestroyWindow(hDlg);
		//EndDialog(hDlg, wParam);
		return (INT_PTR)TRUE;
	case WM_APP+1: //Cancelled/Ended but show an OK button first.
		{
			CProgressIndicator *pThis = (CProgressIndicator*)GetWindowLongPtr(hDlg, GWLP_USERDATA);

			HWND hCancelButton = GetDlgItem(hDlg, IDCANCEL);
			HWND hOKButton = GetDlgItem(hDlg, IDOK);
			ShowWindow(hCancelButton, SW_HIDE);
			ShowWindow(hOKButton, SW_SHOW);

			HWND hWndProgress = GetDlgItem(hDlg, IDC_PROG);
			EnableWindow(hWndProgress, FALSE);
		}
		break;
	}
	return (INT_PTR)FALSE;
}
bool GetTargetWindowRect(IN HWND hParentWnd, IN HINSTANCE hInstance, IN LPCWSTR lpTemplateName, OUT RECT *pRect)
{
	struct _DLGTEMPLATEEX_HEADER{
		uint16_t dlgVer;
		uint16_t signature;
		DWORD helpID;
		DWORD exStyle;
		DWORD style;
		uint16_t cDlgItems;
		short x;
		short y;
		short cx;
		short cy;
	};
	TCHAR tempClassName[32];
	_stprintf_s(tempClassName, TEXT("UABE_PROGIND_TEMP%u"), GetCurrentThreadId());

	HRSRC hResource = FindResourceExW(hInstance, RT_DIALOG, lpTemplateName, 0);
	if (hResource == NULL)
		return false;
	HGLOBAL hLoadedResource = LoadResource(hInstance, hResource);
	if (hLoadedResource == NULL)
		return false;
	bool ret = false;
	LPVOID pResourceData = LockResource(hLoadedResource);
	if (pResourceData != NULL)
	{
		DWORD size = SizeofResource(hInstance, hResource);
		if (size >= sizeof(_DLGTEMPLATEEX_HEADER))
		{
			_DLGTEMPLATEEX_HEADER *pTmpl = (_DLGTEMPLATEEX_HEADER*)pResourceData;
			{
				WNDCLASSW wc = {};
				wc.lpfnWndProc = DefWindowProcW;
				wc.hInstance = hInstance;
				wc.lpszClassName = tempClassName;
				RegisterClass(&wc);
				HWND hWnd = CreateWindowExW(WS_EX_NOPARENTNOTIFY, tempClassName, L"", hParentWnd ? WS_CHILD : WS_POPUP, 
					hParentWnd ? pTmpl->x : CW_USEDEFAULT, hParentWnd ? pTmpl->y : SW_HIDE, 
					pTmpl->cx, pTmpl->cy, hParentWnd, NULL, hInstance, NULL);
				if (hWnd != NULL)
				{
					ret = GetWindowRect(hWnd, pRect) != FALSE;
					DestroyWindow(hWnd);
				}
				UnregisterClass(tempClassName, hInstance);
			}
		}
		UnlockResource(hLoadedResource);
	}
	FreeResource(hLoadedResource);
	return ret;
}
DWORD WINAPI CProgressIndicator::WindowHandlerThread(PVOID param)
{
	CProgressIndicator *pThis = (CProgressIndicator*)param;

	RECT targetWindowRect = {};
	bool doSetTargetRect = GetTargetWindowRect(pThis->hParentWindow, pThis->hInstance, MAKEINTRESOURCE(IDD_PROGRESS), &targetWindowRect);

	pThis->dialogHasFocus = false;
	pThis->dialogIsActive = false;

	HWND hDialog = CreateDialogParam(pThis->hInstance, MAKEINTRESOURCE(IDD_PROGRESS), NULL, WindowHandler, (LPARAM)pThis);
	if (doSetTargetRect)
	{
		SetWindowPos(hDialog, NULL, 
			targetWindowRect.left, targetWindowRect.top, 0, 0, 
			SWP_NOOWNERZORDER | SWP_NOSIZE);
	}
	MSG msg; 
	while (GetMessage(&msg, NULL, 0, 0))
	{
		DispatchMessage(&msg);
	}
	return 0;
}
bool CProgressIndicator::Start(HWND hParentWindow, std::shared_ptr<IProgressIndicator> _selfRef, unsigned int showDelay)
{
	if (!started)
	{
		if (!(this->hStartEvent = CreateEvent(NULL, TRUE, FALSE, NULL)))
			return false;
		if (!(this->hEndEvent = CreateEvent(NULL, TRUE, FALSE, NULL)))
		{
			CloseHandle(this->hStartEvent);
			return false;
		}
		this->selfRef = std::move(_selfRef);
		bool status = true;
		this->hParentWindow = hParentWindow;
		this->showDelay = showDelay;
		this->logWasCalled = false;
		HANDLE hThread = CreateThread(NULL, 0, WindowHandlerThread, this, 0, NULL);
		if (hThread)
		{
			if (WaitForSingleObject(this->hStartEvent, INFINITE) != WAIT_OBJECT_0)
			{
				TerminateThread(hThread, 0);
				SetEvent(this->hEndEvent);
				status = false;
			}
			CloseHandle(hThread);
		}
		else
		{
			SetEvent(this->hEndEvent);
			status = false;
		}
		CloseHandle(this->hStartEvent);
		this->hStartEvent = NULL;
		if (status)
		{
			if (this->stepRanges.size() == 0)
				this->stepRanges.push_back(0);
			UpdateRange();
			
			cancelled = false;
			cancellable = true;
			forceUncancellable = false;

			started = true;
		}
		return status;
	}
	return true;
}
void CProgressIndicator::End()
{
	if (started)
	{
		if (!logWasCalled || !dontCloseIfLog)
		{
			started = false;

			HANDLE hEndEvent = this->hEndEvent;
			SendMessage(this->hDialog, WM_APP+0, 0, 0);
			WaitForSingleObject(hEndEvent, 100);
		}
		else
		{
			cancelled = true;
			SendMessage(this->hDialog, WM_APP+1, 0, 0);
		}
	}
}
void CProgressIndicator::Free()
{
	std::unique_lock endLock(this->endMutex, std::defer_lock);
	if (this->hEndEvent)
		endLock.lock();
	if (!dontCloseIfLog || !logWasCalled || !started)
	{
		if (endLock.owns_lock())
			endLock.unlock();
		End();
	}
}
void CProgressIndicator::UpdateRange()
{
	HWND hWndProgress = GetDlgItem(this->hDialog, IDC_PROG);
	if (totalRange == 0)
	{
		SetWindowLongPtr(hWndProgress, GWL_STYLE, 
			(GetWindowLongPtr(hWndProgress, GWL_STYLE) & (~(PBS_SMOOTH | PBS_SMOOTHREVERSE))) | PBS_MARQUEE);
		SendMessage(hWndProgress, PBM_SETMARQUEE, (WPARAM)1, 0);
	}
	else
	{
		//Don't set the style if it's not necessary since setting the style resets the progress animation back to 0.
		LONG_PTR oldStyle = GetWindowLongPtr(hWndProgress, GWL_STYLE);
		if (oldStyle & PBS_MARQUEE || !(oldStyle & (PBS_SMOOTH | PBS_SMOOTHREVERSE)))
		{
			SetWindowLongPtr(hWndProgress, GWL_STYLE, (oldStyle & (~PBS_MARQUEE)) | PBS_SMOOTH | PBS_SMOOTHREVERSE);
		}

		SendMessage(hWndProgress, PBM_SETRANGE32, (WPARAM)0, (LPARAM)totalRange);
		
		UpdateProgress();
	}
}
void CProgressIndicator::UpdateProgress()
{
	if (totalRange > 0)
	{
		HWND hWndProgress = GetDlgItem(this->hDialog, IDC_PROG);
		SendMessage(hWndProgress, PBM_SETPOS, (WPARAM)(curStepBasePos + curStepProgress), 0);
	}
}
void CProgressIndicator::SetDontCloseIfLog(bool dontclose)
{
	this->dontCloseIfLog = dontclose;
}
size_t CProgressIndicator::AddStep(unsigned int range)
{
	if (started)
	{
		size_t ret = stepRanges.size();
		stepRanges.push_back(range);
		totalRange += range;
		
		UpdateRange();
		return ret;
	}
	return 0;
}
bool CProgressIndicator::SetStepRange(size_t idx, unsigned int range)
{
	if (started)
	{
		if (stepRanges.size() > idx)
		{
			unsigned int offset = range - stepRanges[idx];
			totalRange += offset;
			stepRanges[idx] = range;

			UpdateRange();
			if (curStep > idx)
			{
				curStepBasePos += offset;
				UpdateProgress();
			}
			return true;
		}
	}
	return false;
}
bool CProgressIndicator::SetStepStatus(unsigned int progress)
{
	if (started)
	{
		if (progress > stepRanges[curStep])
			progress = stepRanges[curStep];
		curStepProgress = progress;
		UpdateProgress();
		return true;
	}
	return false;
}
bool CProgressIndicator::JumpToStep(size_t idx, unsigned int progress)
{
	if (started)
	{
		if (idx >= stepRanges.size())
			return false;
		if (idx >= curStep)
		{
			for (size_t i = curStep; i < idx; i++)
				curStepBasePos += stepRanges[i];
		}
		else
		{
			for (size_t i = idx; i < curStep; i++)
				curStepBasePos -= stepRanges[i];
		}
		curStep = idx;
		SetStepStatus(progress);
		return true;
	}
	return false;
}
size_t CProgressIndicator::GoToNextStep()
{
	if (JumpToStep(curStep + 1))
		return curStep;
	else
		return (size_t)-1;
}

bool CProgressIndicator::SetTitle(const std::string &title)
{
	if (started)
	{
		size_t strLen = 0;
		wchar_t *wTitle = _MultiByteToWide(title.c_str(), strLen);
		if (wTitle != nullptr)
		{
			SetWindowTextW(this->hDialog, wTitle);
			_FreeWCHAR(wTitle);
			return true;
		}
	}
	return false;
}
bool CProgressIndicator::SetTitle(const std::wstring &title)
{
	if (started)
	{
		SetWindowTextW(this->hDialog, title.c_str());
		return true;
	}
	return false;
}

bool CProgressIndicator::SetDescription(const std::string &desc)
{
	if (started)
	{
		size_t strLen = 0;
		wchar_t *wDesc = _MultiByteToWide(desc.c_str(), strLen);
		if (wDesc != nullptr)
		{
			HWND hWndDesc = GetDlgItem(this->hDialog, IDC_SDESC);
			Static_SetText(hWndDesc, wDesc);
			_FreeWCHAR(wDesc);
			return true;
		}
	}
	return false;
}
bool CProgressIndicator::SetDescription(const std::wstring &desc)
{
	if (started)
	{
		HWND hWndDesc = GetDlgItem(this->hDialog, IDC_SDESC);
		Static_SetText(hWndDesc, desc.c_str());
		return true;
	}
	return false;
}

bool CProgressIndicator::AddLogText(const std::string &text)
{
	if (started)
	{
		size_t strLen = 0;
		wchar_t *wText = _MultiByteToWide(text.c_str(), strLen);
		if (wText != nullptr)
		{
			bool ret = AddLogText(std::wstring(wText));
			_FreeWCHAR(wText);
			return ret;
		}
	}
	return false;
}
bool CProgressIndicator::AddLogText(const std::wstring &text)
{
	if (started)
	{
		HWND hWndStatus = GetDlgItem(this->hDialog, IDC_EDITSTATUS);

		int editLen = Edit_GetTextLength(hWndStatus);
		int oldSelStart = editLen;
		int oldSelEnd = editLen;
		SendMessage(hWndStatus, EM_GETSEL, (WPARAM)&oldSelStart, (LPARAM)&oldSelEnd);

		Edit_SetSel(hWndStatus, editLen, editLen);
		Edit_ReplaceSel(hWndStatus, text.c_str());

		if ((oldSelEnd != editLen) || (oldSelStart != oldSelEnd))
			Edit_SetSel(hWndStatus, oldSelStart, oldSelEnd);
		else
			Edit_SetSel(hWndStatus, editLen + text.size(), editLen + text.size());
		
		this->logWasCalled = true;
		return true;
	}
	return false;
}

bool CProgressIndicator::SetCancellable(bool cancellable)
{
	if (started)
	{
		HWND hWndCancel = GetDlgItem(this->hDialog, IDCANCEL);
		Button_Enable(hWndCancel, cancellable ? TRUE : FALSE);
		this->cancellable = cancellable;
		this->forceUncancellable = true;
		return true;
	}
	return false;
}
bool CProgressIndicator::AddCancelCallback(std::unique_ptr<ICancelCallback> pCallback)
{
	if (started)
	{
		cancelCallbacks.push_back(std::move(pCallback));
		return true;
	}
	return false;
}
bool CProgressIndicator::IsCancelled()
{
	if (started)
	{
		return cancelled;
	}
	return false;
}
bool CProgressIndicator::SetCancelled(bool cancelled)
{
	if (started)
	{
		bool prevCancelled = this->cancelled;
		this->cancelled = cancelled;
		for (size_t i = 0; i < cancelCallbacks.size(); i++)
		{
			cancelCallbacks[i]->OnCancelEvent(cancelled);
		}
		if (prevCancelled && !cancelled && !cancellable && !forceUncancellable)
		{
			SetCancellable(true); //Reenable the button, which will get disabled when the user presses cancel.
		}
		return true;
	}
	return false;
}