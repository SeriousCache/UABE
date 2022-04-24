#include "stdafx.h"
#include "Win32TaskStatusTracker.h"
#include <array>
#include "resource.h"
#include <windowsx.h>
#include "../libStringConverter/convert.h"

static int GetTextExtent(HWND hListBox, const TCHAR* text)
{
	HDC hListDC = GetDC(hListBox);
	HGDIOBJ hOrigObject = SelectObject(hListDC, GetWindowFont(hListBox));
	RECT textRect = {};
	DrawText(hListDC, text, -1, &textRect, DT_SINGLELINE | DT_CALCRECT);
	SelectObject(hListDC, hOrigObject);
	ReleaseDC(hListBox, hListDC);

	return textRect.right - textRect.left + 4;
}

static std::string processLogEntry(const std::string& text)
{
	//Convert "\n" (LF) line ends to "\r\n" (CRLF).
	std::string outText;
	size_t curLineEnd = SIZE_MAX; size_t prevLineEnd = 0;
	while (curLineEnd = text.find_first_of('\n', curLineEnd + 1), curLineEnd != std::string::npos)
	{
		outText += text.substr(prevLineEnd, curLineEnd - prevLineEnd);
		if (curLineEnd == 0 || text[curLineEnd - 1] != '\r')
			outText += '\r';
		prevLineEnd = curLineEnd;
	}
	outText += text.substr(prevLineEnd);
	return outText;
}
bool Win32TaskStatusTracker::dlgAddLogText(const std::string& text)
{
	if (hDlg == NULL)
		return false;
	HWND hWndStatus = GetDlgItem(hDlg, IDC_EDITSTATUS);

	int editLen = Edit_GetTextLength(hWndStatus);
	assert(editLen >= 0);
	if (editLen < 0)
		return false;
	int oldSelStart = editLen;
	int oldSelEnd = editLen;
	SendMessage(hWndStatus, EM_GETSEL, (WPARAM)&oldSelStart, (LPARAM)&oldSelEnd);

	std::string outText = processLogEntry(text);
	size_t outTextTLen = 0;
	auto pOutTextT = unique_MultiByteToTCHAR(outText.c_str(), outTextTLen);
	if (outTextTLen > INT_MAX || (INT_MAX - outTextTLen) < editLen)
		return false;

	Edit_SetSel(hWndStatus, editLen, editLen);
	Edit_ReplaceSel(hWndStatus, pOutTextT.get());

	if ((oldSelEnd != editLen) || (oldSelStart != oldSelEnd))
		Edit_SetSel(hWndStatus, oldSelStart, oldSelEnd);
	else
		Edit_SetSel(hWndStatus, -1, -1);

	return true;
}
bool Win32TaskStatusTracker::dlgPutLogText(TaskStatusDesc* pDesc)
{
	if (hDlg == NULL)
		return false;
	HWND hWndStatus = GetDlgItem(hDlg, IDC_EDITSTATUS);

	std::basic_string<TCHAR> fullLog;
	for (const std::string& message : pDesc->messages)
	{
		std::string text = processLogEntry(message);
		size_t textTLen = 0;
		auto pTextT = unique_MultiByteToTCHAR(text.c_str(), textTLen);
		fullLog.insert(fullLog.end(), pTextT.get(), pTextT.get() + textTLen);
	}
	if (fullLog.size() >= INT_MAX)
	{
		Edit_SetText(hWndStatus, TEXT(""));
		return false;
	}
	Edit_SetText(hWndStatus, fullLog.c_str());
	Edit_SetSel(hWndStatus, -1, -1);
	numShownLogEntries = pDesc->messages.size();
	return true;
}
inline auto getListCtrls(HWND hDlg)
{
	return std::array{ GetDlgItem(hDlg, IDC_LISTRUNNING), GetDlgItem(hDlg, IDC_LISTCOMPLETE) };
}
bool Win32TaskStatusTracker::dlgFindListCtrlFor(TaskStatusDesc* pDesc, HWND& hList, int& listIdx)
{
	assert(pDesc != nullptr);
	hList = NULL;
	listIdx = 0;
	if (hDlg == NULL || pDesc == nullptr)
		return false;
	
	auto listCtrls = getListCtrls(hDlg);
	
	for (size_t i = 0; i < listCtrls.size(); ++i)
	{
		HWND hListCtrl = listCtrls[i];
		assert(hListCtrl != NULL);
		if (hListCtrl == NULL)
			continue;
		int iItem = (int)dlgGetListIdx(pDesc);

		int numItems = ListBox_GetCount(hListCtrl);
		if (numItems >= 0 && numItems > iItem
			&& (ListBox_GetItemData(hListCtrl, iItem) == (LRESULT)pDesc))
		{
			hList = hListCtrl;
			listIdx = iItem;
			return true;
		}
	}
	return false;
}
void Win32TaskStatusTracker::dlgSwitchShownTask(TaskStatusDesc* pDesc)
{
	if (hDlg == NULL)
		return;
	if (processingSelection)
		return;
	HWND hListComplete = GetDlgItem(hDlg, IDC_LISTCOMPLETE);
	bool hasAnyCompleteItems = (ListBox_GetCount(hListComplete) > 0);
	EnableWindow(GetDlgItem(hDlg, IDC_CLEARALL), hasAnyCompleteItems ? TRUE : FALSE);

	auto listCtrls = getListCtrls(hDlg);
	if (pDesc == nullptr)
	{
		processingSelection = true;
		for (size_t i = 0; i < listCtrls.size(); ++i)
			ListBox_SetCurSel(listCtrls[i], -1);
		processingSelection = false;
		numShownLogEntries = 0;
		ShowWindow(GetDlgItem(hDlg, IDC_EDITSTATUS), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_PROG), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_SDESC), SW_HIDE);
		EnableWindow(GetDlgItem(hDlg, IDC_CLEAR), FALSE);
		EnableWindow(GetDlgItem(hDlg, IDC_CANCELTASK), FALSE);
		return;
	}
	HWND hListCtrl = NULL; int listIdx = 0;
	if (!dlgFindListCtrlFor(pDesc, hListCtrl, listIdx))
		return;
	if (hListCtrl == GetDlgItem(hDlg, IDC_LISTCOMPLETE))
		pDesc->eraseIfStale = false;
	processingSelection = true;
	for (size_t i = 0; i < listCtrls.size(); ++i)
	{
		if (listCtrls[i] != hListCtrl)
			ListBox_SetCurSel(listCtrls[i], -1);
	}
	ListBox_SetCurSel(hListCtrl, listIdx);
	processingSelection = false;
	dlgPutLogText(pDesc);
	mainOnTaskProgressUpdate(pDesc->selfRef);
	mainOnTaskProgressDescUpdate(pDesc->selfRef);
	ShowWindow(GetDlgItem(hDlg, IDC_EDITSTATUS), SW_SHOW);
	ShowWindow(GetDlgItem(hDlg, IDC_PROG), SW_SHOW);
	ShowWindow(GetDlgItem(hDlg, IDC_SDESC), SW_SHOW);
	EnableWindow(GetDlgItem(hDlg, IDC_CLEAR), (hListCtrl == hListComplete) ? TRUE : FALSE);
	EnableWindow(GetDlgItem(hDlg, IDC_CANCELTASK), pDesc->cancelable ? TRUE : FALSE);
}
static TaskStatusDesc* listCtrlGetItem(HWND hListCtrl, int listIdx)
{
	LRESULT itemData = ListBox_GetItemData(hListCtrl, listIdx);
	assert(itemData != LB_ERR);
	if (itemData == LB_ERR)
		return nullptr;
	return reinterpret_cast<TaskStatusDesc*>(itemData);
}
void Win32TaskStatusTracker::dlgHideTask(TaskStatusDesc* pDesc, bool eraseStale)
{
	HWND hListCtrl = NULL; int listIdx = 0;
	if (!dlgFindListCtrlFor(pDesc, hListCtrl, listIdx))
		return;
	HWND hListComplete = GetDlgItem(hDlg, IDC_LISTCOMPLETE);
	pDesc->eraseIfStale = true;
	bool wasSelected = (ListBox_GetCurSel(hListCtrl) == listIdx);
	processingSelection = true;

	auto& taskNameExtents = (hListCtrl == hListComplete) ? completeTaskNameExtents : runningTaskNameExtents;
	assert(ListBox_GetCount(hListCtrl) == taskNameExtents.size());
	assert(ListBox_GetCount(hListCtrl) > listIdx);
	ListBox_DeleteString(hListCtrl, listIdx);
	for (int i = listIdx; i < ListBox_GetCount(hListCtrl); ++i)
	{
		TaskStatusDesc* pDesc = reinterpret_cast<TaskStatusDesc*>(ListBox_GetItemData(hListCtrl, i));
		if (pDesc == nullptr)
			continue;
		assert(pDesc->auxData == i + 1);
		pDesc->auxData = (uintptr_t)i;
	}

	assert(hListCtrl == hListComplete || hListCtrl == GetDlgItem(hDlg, IDC_LISTRUNNING));
	if (listIdx < taskNameExtents.size())
		taskNameExtents.erase(taskNameExtents.begin() + listIdx);
	auto maxIt = std::max_element(taskNameExtents.begin(), taskNameExtents.end());
	int extent = (maxIt == taskNameExtents.end()) ? 10 : *maxIt;
	ListBox_SetHorizontalExtent(hListComplete, extent);

	processingSelection = false;

	bool hasAnyCompleteItems = (ListBox_GetCount(hListComplete) > 0);
	EnableWindow(GetDlgItem(hDlg, IDC_CLEARALL), hasAnyCompleteItems ? TRUE : FALSE);

	if (wasSelected)
	{
		auto listCtrls = getListCtrls(hDlg);
		bool foundItem = false;
		for (size_t i = 0; i < listCtrls.size(); ++i)
		{
			if (ListBox_GetCount(listCtrls[i]) > 0)
			{
				foundItem = true;
				EnableWindow(GetDlgItem(hDlg, IDC_CLEARALL), TRUE);
				ListBox_SetCurSel(listCtrls[i], 0);
				if (TaskStatusDesc* pItemDesc = listCtrlGetItem(listCtrls[i], 0))
				{
					this->dlgSwitchShownTask(pItemDesc);
					pItemDesc->eraseIfStale = false;
				}
				else
					this->dlgSwitchShownTask(nullptr);
				break;
			}
		}
		if (!foundItem)
		{
			EnableWindow(GetDlgItem(hDlg, IDC_CLEARALL), FALSE);
			dlgSwitchShownTask(nullptr);
		}
	}
	if (eraseStale)
		eraseStaleElements();
}
void Win32TaskStatusTracker::onResize(bool defer)
{
	RECT client = {};
	GetClientRect(hDlg, &client);
	long width = client.right - client.left;
	long height = client.bottom - client.top;

	HDWP deferCtx = defer ? BeginDeferWindowPos(12) : NULL;
	bool retry = false;
	std::vector<RECT> invalidateRects;
	auto doMoveWindow = [&deferCtx, &retry, &invalidateRects](HWND hWnd, int x, int y, int w, int h, bool invalidate = false)
	{
		if (invalidate)
			invalidateRects.emplace_back((LONG)x, (LONG)y, (LONG)x + w, (LONG)y + h);
		if (deferCtx)
		{
			deferCtx = DeferWindowPos(deferCtx, hWnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
			if (!deferCtx)
				retry = true;
		}
		else
			SetWindowPos(hWnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
	};

	long fontHeight = 16;
	long bottomDistance = 7, topDistance = 4;
	long leftDistance = 7, rightDistance = 7;

	long panelTop = topDistance;
	long leftPanelLeft = leftDistance;
	long leftPanelWidth = (long)(width * this->mainPanelSplitter.getLeftOrTopPanelRatio() - leftPanelLeft);
	long panelHeight = height - bottomDistance - topDistance;

	long buttonHeight = 25, progbarHeight = 25;
	long buttonWidth = 80;

	{
		long taskLabelListsHeight = (panelHeight - buttonHeight - 3);
		long runningLabelListTop = panelTop;
		long runningLabelListHeight = taskLabelListsHeight / 2 + (taskLabelListsHeight % 2);
		doMoveWindow(GetDlgItem(hDlg, IDC_SRUNNINGTASKS), leftPanelLeft, runningLabelListTop, leftPanelWidth - 3, fontHeight, true);
		doMoveWindow(GetDlgItem(hDlg, IDC_LISTRUNNING), leftPanelLeft, runningLabelListTop + fontHeight + 4, leftPanelWidth - 3, runningLabelListHeight - fontHeight - 8);
		long completeLabelListTop = runningLabelListTop + runningLabelListHeight;
		long completeLabelListHeight = taskLabelListsHeight / 2;
		doMoveWindow(GetDlgItem(hDlg, IDC_SCOMPLETETASKS), leftPanelLeft, completeLabelListTop, leftPanelWidth - 3, fontHeight, true);
		doMoveWindow(GetDlgItem(hDlg, IDC_LISTCOMPLETE), leftPanelLeft, completeLabelListTop + fontHeight + 4, leftPanelWidth - 3, completeLabelListHeight - fontHeight - 8);
	}

	doMoveWindow(GetDlgItem(hDlg, IDC_CLEARALL), leftPanelLeft, height - bottomDistance - buttonHeight, buttonWidth, buttonHeight);
	doMoveWindow(GetDlgItem(hDlg, IDC_CLEAR), leftPanelLeft + leftPanelWidth - 3 - buttonWidth, height - bottomDistance - buttonHeight, buttonWidth, buttonHeight, true);

	long rightPanelLeft = leftPanelLeft + leftPanelWidth + 3;
	long rightPanelWidth = width - rightDistance - rightPanelLeft;

	doMoveWindow(GetDlgItem(hDlg, IDC_SDESC), rightPanelLeft, panelTop, rightPanelWidth, fontHeight, true);
	doMoveWindow(GetDlgItem(hDlg, IDC_PROG), rightPanelLeft, panelTop + fontHeight + 4, rightPanelWidth, progbarHeight);
	long editstatusTop = panelTop + fontHeight + 4 + progbarHeight + 8;
	long editstatusHeight = panelHeight - (editstatusTop - panelTop) - buttonHeight - 3;
	doMoveWindow(GetDlgItem(hDlg, IDC_EDITSTATUS), rightPanelLeft, editstatusTop, rightPanelWidth, editstatusHeight);

	doMoveWindow(GetDlgItem(hDlg, IDC_CANCELTASK), rightPanelLeft, height - bottomDistance - buttonHeight, buttonWidth, buttonHeight, true);
	doMoveWindow(GetDlgItem(hDlg, IDOK), std::max(rightPanelLeft, rightPanelLeft + rightPanelWidth - buttonWidth),
		height - bottomDistance - buttonHeight, buttonWidth, buttonHeight, true);

	long contentSeparateLeft = leftPanelLeft + leftPanelWidth, contentPanelTop = panelTop;
	long contentSeparateHeight = panelHeight;
	doMoveWindow(GetDlgItem(hDlg, IDC_CONTENTSEPARATE), contentSeparateLeft, -2, 3, height + 2);

	if (defer)
	{
		if (retry || !EndDeferWindowPos(deferCtx))
		{
			invalidateRects.clear();
			onResize(false);
		}
		deferCtx = NULL;
	}

	UpdateWindow(hDlg);
	//Workaround for now (broken labels and buttons occur when resizing).
	for (RECT &rect : invalidateRects)
		InvalidateRect(hDlg, &rect, FALSE);
}
INT_PTR CALLBACK Win32TaskStatusTracker::DlgHandler(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	INT_PTR ret = (INT_PTR)FALSE;
	Win32TaskStatusTracker* pThis = (Win32TaskStatusTracker*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
	if (pThis && pThis->mainPanelSplitter.handleWin32Message(hDlg, message, wParam, lParam))
	{
		if (pThis->mainPanelSplitter.shouldResize())
			pThis->onResize();
		return (message == WM_SETCURSOR) ? (INT_PTR)TRUE : (INT_PTR)0;
	}

	switch (message)
	{
	case WM_DESTROY:
		break;
	case WM_NCDESTROY:
		break;
	case WM_CLOSE:
		if (pThis)
			pThis->hDlg = NULL;
		DestroyWindow(hDlg);
		ret = (INT_PTR)TRUE;
		break;
	case WM_INITDIALOG:
		{
			SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
			pThis = (Win32TaskStatusTracker*)lParam;
			pThis->mainPanelSplitter.setSplitterWindow(GetDlgItem(hDlg, IDC_CONTENTSEPARATE));
			pThis->mainPanelSplitter.handleWin32Message(hDlg, message, wParam, lParam);
			pThis->hDlg = hDlg;

			pThis->processingSelection = false;
			pThis->numShownLogEntries = 0;

			ShowWindow(GetDlgItem(hDlg, IDC_EDITSTATUS), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_PROG), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_SDESC), SW_HIDE);
			EnableWindow(GetDlgItem(hDlg, IDC_CLEARALL), FALSE);
			EnableWindow(GetDlgItem(hDlg, IDC_CLEAR), FALSE);
			EnableWindow(GetDlgItem(hDlg, IDC_CANCELTASK), FALSE);
			for (auto it = pThis->taskList.begin(); it != pThis->taskList.end(); ++it)
			{
				if (it->hasResult)
					it->eraseIfStale = (it->result >= 0);
				else
					it->eraseIfStale = true;
			}
			pThis->eraseStaleElements();
			for (auto it = pThis->taskList.begin(); it != pThis->taskList.end(); ++it)
			{
				if (it->hasResult)
					pThis->handleTaskCompletion(it, false);
				else
					pThis->mainOnTaskAdd(it);
			}
			HWND hListRunning = GetDlgItem(hDlg, IDC_LISTRUNNING);
			if (ListBox_GetCount(hListRunning) > 0)
			{
				if (TaskStatusDesc *pDesc = listCtrlGetItem(hListRunning, 0))
					pThis->dlgSwitchShownTask(pDesc);
			}

			ShowWindow(hDlg, SW_SHOW);
			PostMessage(hDlg, WM_SIZE, 0, 0);
			ret = (INT_PTR)TRUE;
		}
		break;
	case WM_SIZE:
		if (pThis)
		{
			pThis->onResize();
			ret = (INT_PTR)TRUE;
		}
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_CLEARALL:
			if (pThis)
			{
				for (std::list<TaskStatusDesc>::iterator staleTaskRef : pThis->staleTaskRefs)
				{
					staleTaskRef->eraseIfStale = true;
					pThis->dlgHideTask(&*staleTaskRef, false);
				}
				pThis->eraseStaleElements();
				pThis->mainOnProgressMessageUpdate();
			}
			ret = (INT_PTR)TRUE;
			break;
		case IDC_CLEAR:
			if (pThis)
			{
				HWND hListComplete = GetDlgItem(hDlg, IDC_LISTCOMPLETE);
				
				int iItem = ListBox_GetCurSel(hListComplete);
				if (iItem != LB_ERR)
				{
					if (TaskStatusDesc* pDesc = listCtrlGetItem(hListComplete, iItem))
						pThis->dlgHideTask(pDesc);
					pThis->mainOnProgressMessageUpdate();
				}
			}
			ret = (INT_PTR)TRUE;
			break;
		case IDC_CANCELTASK:
			if (pThis)
			{
				HWND hListRunning = GetDlgItem(hDlg, IDC_LISTRUNNING);
				int iItem = ListBox_GetCurSel(hListRunning);
				if (iItem != LB_ERR)
				{
					TaskStatusDesc* pDesc = listCtrlGetItem(hListRunning, iItem);
					std::shared_ptr<ITask> pTask;
					if (pDesc && (pTask = pDesc->wpTask.lock()))
					{
						//The cancel button will be disabled in response to the "OnCancelableChange" event.
						pThis->appContext.taskManager.cancel(pTask.get());
					}
				}
			}
			ret = (INT_PTR)TRUE;
			break;
		case IDCANCEL:
		case IDOK:
			SendMessage(hDlg, WM_CLOSE, 0, 0);
			ret = (INT_PTR)TRUE;
			break;
		case IDC_LISTRUNNING:
		case IDC_LISTCOMPLETE:
			{
				HWND hList = (HWND)lParam;
				if (HIWORD(wParam) == LBN_SELCHANGE)
				{
					auto listCtrls = getListCtrls(hDlg);
					if (std::find(listCtrls.begin(), listCtrls.end(), hList) == listCtrls.end())
						break; //Only looking for the task list views.
					TaskStatusDesc* pDesc = listCtrlGetItem(hList, ListBox_GetCurSel(hList));
					pThis->dlgSwitchShownTask(pDesc);
				}
			}
			break;
		}
		break;
	}
	return ret;
}

Win32TaskStatusTracker::Win32TaskStatusTracker(class Win32AppContext& appContext, HWND hMainWndProgress, HWND hMainWndStatusText)
	: TaskStatusTracker(appContext), hMainWndProgress(hMainWndProgress), hMainWndStatusText(hMainWndStatusText), mainPanelSplitter(0.3f, 0.2f, 0.8f)
{
	mainOnTotalProgressUpdate();
	mainOnProgressMessageUpdate();
}
void Win32TaskStatusTracker::open()
{
	if (hDlg != NULL)
		return;
	runningTaskNameExtents.clear();
	completeTaskNameExtents.clear();
	Win32AppContext& appContext = reinterpret_cast<Win32AppContext&>(this->appContext);
	hDlg = CreateDialogParam(appContext.getMainWindow().getHInstance(), MAKEINTRESOURCE(IDD_PROGRESS2),
		appContext.getMainWindow().getWindow(), DlgHandler, (LPARAM)this);
}
void Win32TaskStatusTracker::close()
{
	if (hDlg == NULL)
		return;
	SendMessage(hDlg, WM_CLOSE, 0, 0);
}

void Win32TaskStatusTracker::preEraseElement(std::list<TaskStatusDesc>::iterator listEntry)
{
	dlgHideTask(&*listEntry);
}
void Win32TaskStatusTracker::mainOnTaskAdd(std::list<TaskStatusDesc>::iterator listEntry)
{
	if (hDlg == NULL)
		return;
	HWND hListRunning = GetDlgItem(hDlg, IDC_LISTRUNNING);
	auto nameT = unique_MultiByteToTCHAR(listEntry->name.c_str());
	int iItem = ListBox_GetCount(hListRunning);
	ListBox_InsertString(hListRunning, iItem, nameT.get());
	ListBox_SetItemData(hListRunning, iItem, (LPARAM)&*listEntry);
	listEntry->auxData = (uintptr_t)iItem;

	runningTaskNameExtents.push_back(GetTextExtent(hListRunning, nameT.get()));
	assert(runningTaskNameExtents.size() == ListBox_GetCount(hListRunning));
	ListBox_SetHorizontalExtent(hListRunning, std::max(runningTaskNameExtents.back(), ListBox_GetHorizontalExtent(hListRunning)));
	
	EnableWindow(GetDlgItem(hDlg, IDC_CLEARALL), TRUE);
	bool hasAnySelection = false;
	auto listCtrls = getListCtrls(hDlg);
	for (size_t i = 0; i < listCtrls.size(); ++i)
	{
		if (ListBox_GetCurSel(listCtrls[i]) != LB_ERR)
		{
			hasAnySelection = true;
			break;
		}
	}
	if (!hasAnySelection)
	{
		ListBox_SetCurSel(hListRunning, iItem);
		this->dlgSwitchShownTask(&*listEntry);
	}
}
void Win32TaskStatusTracker::mainOnTaskProgressUpdate(std::list<TaskStatusDesc>::iterator listEntry)
{
	if (hDlg == NULL)
		return;
	HWND hListCtrl = NULL; int listIdx = 0;
	if (!dlgFindListCtrlFor(&*listEntry, hListCtrl, listIdx))
		return;
	if (ListBox_GetCurSel(hListCtrl) != listIdx)
		return;
	HWND hWndProgress = GetDlgItem(hDlg, IDC_PROG);
	int oldRange = (int)SendMessage(hWndProgress, PBM_GETRANGE, (WPARAM)FALSE, (LPARAM)NULL);
	int newRange = (int)std::min<unsigned int>(listEntry->range, INT_MAX);
	int progress = (int)std::min<unsigned int>(listEntry->progress, INT_MAX);
	if (listEntry->hasResult)
	{
		if (newRange == 0)
			newRange = 1;
		progress = newRange;
	}
	if (listEntry->hasResult && listEntry->result < 0)
		SendMessage(hWndProgress, PBM_SETSTATE, (WPARAM)PBST_ERROR, (LPARAM)0);
	else
		SendMessage(hWndProgress, PBM_SETSTATE, (WPARAM)PBST_NORMAL, (LPARAM)0);
	if (newRange == 0)
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
		if (oldRange != newRange)
			SendMessage(hWndProgress, PBM_SETRANGE32, (WPARAM)0, (LPARAM)newRange);
	}
	if (newRange > 0)
	{
		SendMessage(hWndProgress, PBM_SETPOS, (WPARAM)std::min<unsigned int>(progress, INT_MAX), (LPARAM)0);
	}
}
void Win32TaskStatusTracker::mainOnTaskProgressDescUpdate(std::list<TaskStatusDesc>::iterator listEntry)
{
	if (hDlg == NULL)
		return;
	HWND hListCtrl = NULL; int listIdx = 0;
	if (!dlgFindListCtrlFor(&*listEntry, hListCtrl, listIdx))
		return;
	if (ListBox_GetCurSel(hListCtrl) == listIdx)
	{
		HWND hWndDesc = GetDlgItem(hDlg, IDC_SDESC);
		if (listEntry->hasResult)
		{
			std::string desc;
			if (listEntry->result == TaskResult_Canceled)
				desc.assign("Canceled: ");
			else if (listEntry->result < 0)
				desc.assign("Failed: ");
			else
				desc.assign("Succeeded: ");
			desc += listEntry->name;
			auto pDescT = unique_MultiByteToTCHAR(desc.c_str());
			Static_SetText(hWndDesc, pDescT.get());
		}
		else
		{
			auto pDescT = unique_MultiByteToTCHAR(listEntry->curProgressDesc.c_str());
			Static_SetText(hWndDesc, pDescT.get());
		}
	}
}
void Win32TaskStatusTracker::mainOnTaskAddLogMessage(std::list<TaskStatusDesc>::iterator listEntry)
{
	if (hDlg == NULL)
		return;
	HWND hListCtrl = NULL; int listIdx = 0;
	if (!dlgFindListCtrlFor(&*listEntry, hListCtrl, listIdx))
		return;
	if (ListBox_GetCurSel(hListCtrl) == listIdx)
	{
		assert(listEntry->messages.size() >= numShownLogEntries);
		if (numShownLogEntries >= listEntry->messages.size())
			return;
		for (size_t i = numShownLogEntries; i < listEntry->messages.size(); ++i)
			dlgAddLogText(listEntry->messages[i]);
		numShownLogEntries = listEntry->messages.size();
	}
}
void Win32TaskStatusTracker::handleTaskCompletion(std::list<TaskStatusDesc>::iterator listEntry, bool eraseStaleEntries)
{
	if (listEntry->hasResult && listEntry->result < 0)
		listEntry->eraseIfStale = false;
	if (hDlg == NULL)
		return;
	mainOnTaskProgressUpdate(listEntry);
	HWND hListCtrl = NULL; int listIdx = 0;
	bool wasSelected = false;
	HWND hListRunning = GetDlgItem(hDlg, IDC_LISTRUNNING);
	HWND hListComplete = GetDlgItem(hDlg, IDC_LISTCOMPLETE);
	if (dlgFindListCtrlFor(&*listEntry, hListCtrl, listIdx) && hListCtrl == hListRunning)
	{
		processingSelection = true;
		wasSelected = ListBox_GetCurSel(hListCtrl) == listIdx;
		if (wasSelected)
			ListBox_SetCurSel(hListCtrl, -1);

		assert(ListBox_GetCount(hListCtrl) == runningTaskNameExtents.size());
		assert(ListBox_GetCount(hListCtrl) > listIdx);
		ListBox_DeleteString(hListCtrl, listIdx);
		for (int i = listIdx; i < ListBox_GetCount(hListCtrl); ++i)
		{
			TaskStatusDesc *pDesc = reinterpret_cast<TaskStatusDesc*>(ListBox_GetItemData(hListCtrl, i));
			if (pDesc == nullptr)
				continue;
			assert(pDesc->auxData == i + 1);
			pDesc->auxData = (uintptr_t)i;
		}
		listEntry->auxData = (uintptr_t)-1;

		assert(hListCtrl == hListRunning);
		if (listIdx < runningTaskNameExtents.size())
			runningTaskNameExtents.erase(runningTaskNameExtents.begin() + listIdx);
		auto maxIt = std::max_element(runningTaskNameExtents.begin(), runningTaskNameExtents.end());
		int extent = (maxIt == runningTaskNameExtents.end()) ? 10 : *maxIt;
		ListBox_SetHorizontalExtent(hListRunning, extent);

		processingSelection = false;
	}

	processingSelection = true;
	auto nameT = unique_MultiByteToTCHAR(listEntry->name.c_str());

	int iItem = ListBox_GetCount(hListComplete);
	ListBox_InsertString(hListComplete, iItem, nameT.get());
	ListBox_SetItemData(hListComplete, iItem, (LPARAM)&*listEntry);
	listEntry->auxData = (uintptr_t)iItem;

	completeTaskNameExtents.push_back(GetTextExtent(hListComplete, nameT.get()));
	assert(completeTaskNameExtents.size() == ListBox_GetCount(hListComplete));
	ListBox_SetHorizontalExtent(hListComplete, std::max(completeTaskNameExtents.back(), ListBox_GetHorizontalExtent(hListComplete)));

	if (wasSelected)
	{
		ListBox_SetCurSel(hListComplete, iItem);
		EnableWindow(GetDlgItem(hDlg, IDC_CLEARALL), TRUE);
		EnableWindow(GetDlgItem(hDlg, IDC_CLEAR), TRUE);
	}
	processingSelection = false;
	if (wasSelected && !listEntry->messages.empty())
		listEntry->eraseIfStale = false;

	bool hasAnySelection = wasSelected;
	auto listCtrls = getListCtrls(hDlg);
	for (size_t i = 0; i < listCtrls.size() && !hasAnySelection; ++i)
	{
		if (ListBox_GetCurSel(listCtrls[i]) != LB_ERR)
		{
			hasAnySelection = true;
		}
	}
	if (!hasAnySelection)
	{
		if (!listEntry->messages.empty() || !listEntry->eraseIfStale)
		{
			ListBox_SetCurSel(hListComplete, iItem);
			this->dlgSwitchShownTask(&*listEntry);
			listEntry->eraseIfStale = false;
		}
		else
			this->dlgSwitchShownTask(nullptr);
	}
	if (eraseStaleEntries)
		eraseStaleElements();
}
void Win32TaskStatusTracker::mainOnTaskCompletion(std::list<TaskStatusDesc>::iterator listEntry)
{
	handleTaskCompletion(listEntry, true);
}
void Win32TaskStatusTracker::mainOnTaskCancelableChange(std::list<TaskStatusDesc>::iterator listEntry)
{
	if (hDlg == NULL)
		return;
	HWND hListCtrl = NULL; int listIdx = 0;
	if (!dlgFindListCtrlFor(&*listEntry, hListCtrl, listIdx))
		return;
	if (ListBox_GetCurSel(hListCtrl) == listIdx)
	{
		EnableWindow(GetDlgItem(hDlg, IDC_CANCELTASK), (listEntry->cancelable) ? TRUE : FALSE);
	}
}

void Win32TaskStatusTracker::mainOnTotalProgressUpdate()
{
	if (hMainWndProgress == NULL)
		return;

	//Don't set the style if it's not necessary since setting the style resets the progress animation back to 0.
	LONG_PTR oldStyle = GetWindowLongPtr(hMainWndProgress, GWL_STYLE);

	size_t numTasks = taskList.size() - staleTaskRefs.size();
	if (numTasks == 0)
	{
		SendMessage(hMainWndProgress, PBM_SETMARQUEE, (WPARAM)FALSE, (LPARAM)0);
		if (oldStyle & PBS_MARQUEE)
		{
			SetWindowLongPtr(hMainWndProgress, GWL_STYLE, (oldStyle & (~PBS_MARQUEE)) | PBS_SMOOTH | PBS_SMOOTHREVERSE);
		}

		if (lastTaskResult < 0 && lastTaskResult != TaskResult_Canceled)
			SendMessage(hMainWndProgress, PBM_SETSTATE, (WPARAM)PBST_ERROR, (LPARAM)0);
		else
			SendMessage(hMainWndProgress, PBM_SETSTATE, (WPARAM)PBST_NORMAL, (LPARAM)0);
		SendMessage(hMainWndProgress, PBM_SETRANGE32, (WPARAM)0, (LPARAM)1);
		SendMessage(hMainWndProgress, PBM_SETPOS, (WPARAM)0, (LPARAM)0);
	}
	else
	{
		SendMessage(hMainWndProgress, PBM_SETSTATE, (WPARAM)PBST_NORMAL, (LPARAM)0);
		
		unsigned int range = 10000;
		unsigned int progress = std::min(static_cast<unsigned int>(totalProgress * range), range);
		SendMessage(hMainWndProgress, PBM_SETMARQUEE, (WPARAM)FALSE, (LPARAM)0);
		if (oldStyle & PBS_MARQUEE)
		{
			SetWindowLongPtr(hMainWndProgress, GWL_STYLE, (oldStyle & (~PBS_MARQUEE)) | PBS_SMOOTH | PBS_SMOOTHREVERSE);
		}

		SendMessage(hMainWndProgress, PBM_SETRANGE32, (WPARAM)0, (LPARAM)range);
		SendMessage(hMainWndProgress, PBM_SETPOS, (WPARAM)progress, (LPARAM)0);
	}
}
void Win32TaskStatusTracker::mainOnProgressMessageUpdate()
{
	if (hMainWndStatusText == NULL)
		return;

	size_t numTasks = taskList.size() - staleTaskRefs.size();
	if (numTasks == 0)
	{
		size_t numFailedTasks = 0;
		for (std::list<TaskStatusDesc>::iterator staleTaskRef : this->staleTaskRefs)
		{
			if (staleTaskRef->hasResult && staleTaskRef->result < 0)
				++numFailedTasks;
		}
		if (numFailedTasks == 0)
			Static_SetText(hMainWndStatusText, TEXT("Ready"));
		else
		{
			std::basic_string<TCHAR> failMsg = std::format(
				TEXT("Ready - {} task{} completed with errors, see the Task Progress tracker"),
				numFailedTasks, (numFailedTasks > 1) ? TEXT("s") : TEXT(""));
			Static_SetText(hMainWndStatusText, failMsg.c_str());
		}
	}
	else
	{
		auto numThreads = appContext.taskManager.getNumThreadsWorking();
		std::string fullDesc = std::format("({} task{}, {} thread{} working) {}",
			numTasks, (numTasks!=1) ? "s" : "",
			numThreads, (numThreads!=1) ? "s" : "",
			latestProgressMessage);
		auto lastTaskDescT = unique_MultiByteToTCHAR(fullDesc.c_str());
		Static_SetText(hMainWndStatusText, lastTaskDescT.get());
	}
}
