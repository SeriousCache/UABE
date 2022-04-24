#pragma once
#include "../UABE_Generic/TaskStatusTracker.h"
#include "Win32AppContext.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "SplitterControlHandler.h"
#include <vector>

class Win32TaskStatusTracker : public TaskStatusTracker
{
	HWND hDlg = NULL;
	HWND hMainWndProgress = NULL;
	HWND hMainWndStatusText = NULL;

	SplitterControlHandler<true> mainPanelSplitter;

	std::vector<int> runningTaskNameExtents;
	std::vector<int> completeTaskNameExtents;

	bool processingSelection = false;
	size_t numShownLogEntries = 0;
	bool dlgAddLogText(const std::string& text);
	bool dlgPutLogText(TaskStatusDesc* pDesc);
	bool dlgFindListCtrlFor(TaskStatusDesc* pDesc, HWND& hList, int& listIdx);
	void dlgSwitchShownTask(TaskStatusDesc* pDesc);
	void dlgHideTask(TaskStatusDesc* pDesc, bool eraseStale = true);
	inline size_t dlgGetListIdx(TaskStatusDesc* pDesc)
	{
		return (size_t)pDesc->auxData;
	}
	static INT_PTR CALLBACK DlgHandler(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	void onResize(bool defer = true);
	void handleTaskCompletion(std::list<TaskStatusDesc>::iterator listEntry, bool eraseStaleEntries = false);

public:
	inline HWND getDialog() { return hDlg; }
	Win32TaskStatusTracker(class Win32AppContext& appContext, HWND hMainWndProgress, HWND hMainWndStatusText);
	void open();
	void close();

	void preEraseElement(std::list<TaskStatusDesc>::iterator listEntry);
	void mainOnTaskAdd(std::list<TaskStatusDesc>::iterator listEntry);
	void mainOnTaskProgressUpdate(std::list<TaskStatusDesc>::iterator listEntry);
	void mainOnTaskProgressDescUpdate(std::list<TaskStatusDesc>::iterator listEntry);
	void mainOnTaskAddLogMessage(std::list<TaskStatusDesc>::iterator listEntry);
	void mainOnTaskCompletion(std::list<TaskStatusDesc>::iterator listEntry);
	void mainOnTaskCancelableChange(std::list<TaskStatusDesc>::iterator listEntry);

	void mainOnTotalProgressUpdate();
	void mainOnProgressMessageUpdate();
};
