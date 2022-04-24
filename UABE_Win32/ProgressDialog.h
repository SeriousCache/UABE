#pragma once
#include "api.h"
#include "../UABE_Generic/IProgressIndicator.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <vector>
#include <mutex>

typedef void(_cdecl *cbFreeProgressIndicator)(class CProgressIndicator *pIndicator);
//Progress indicator that runs a message handler in a separate thread.
//Useful if longer operations need to run in the main thread
// and messages are not processed during that operation.
//Note that the main thread windows still end up labeled
// as 'not responding' depending on how long the operation is.
class CProgressIndicator : public IProgressIndicator
{
	HWND hDialog;
	HWND hParentWindow;
	HINSTANCE hInstance;
	unsigned int showDelay;
	bool started;
	bool showDelayElapsed;
	bool dialogHasFocus;
	bool dialogIsActive;
	
	HANDLE hStartEvent;
	HANDLE hEndEvent;
	std::mutex endMutex;

	std::vector<unsigned int> stepRanges;
	unsigned int totalRange;
	unsigned int curStepBasePos;
	unsigned int curStepProgress;
	size_t curStep;

	bool cancelled;
	bool cancellable;
	bool forceUncancellable;
	bool dontCloseIfLog;
	bool logWasCalled;
	std::shared_ptr<IProgressIndicator> selfRef;
	std::vector<std::unique_ptr<ICancelCallback>> cancelCallbacks;

	static INT_PTR CALLBACK WindowHandler(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	static DWORD WINAPI WindowHandlerThread(PVOID param);
public:
	UABE_Win32_API CProgressIndicator(HINSTANCE hInstance);
	UABE_Win32_API ~CProgressIndicator();
	UABE_Win32_API bool Start(HWND hParentWindow, std::shared_ptr<IProgressIndicator> selfRef, unsigned int showDelay);
	UABE_Win32_API void End();
	UABE_Win32_API void Free();
	UABE_Win32_API void UpdateRange();
	UABE_Win32_API void UpdateProgress();

	//Instructs the progress indicator to stay open or not to stay open (default) with an OK button instead of Cancel if something was written to the log.
	UABE_Win32_API void SetDontCloseIfLog(bool dontclose = true);

	//Adds a new step and returns its index. The step with index 0 already exists before calling AddStep the first time. 
	UABE_Win32_API size_t AddStep(unsigned int range = 0);
	//Sets the range of a step. The progress bar range is set to match the range of all steps combined.
	UABE_Win32_API bool SetStepRange(size_t idx, unsigned int range);
	//Sets the progress of the current step, which should be a value from 0 to (including) range.
	UABE_Win32_API bool SetStepStatus(unsigned int progress);

	//Jumps to a step and sets its progress.
	UABE_Win32_API bool JumpToStep(size_t idx, unsigned int progress = 0);
	//Goes to the next step, setting its progress to 0.
	UABE_Win32_API size_t GoToNextStep();

	//Sets the progress indicator's window title.
	UABE_Win32_API bool SetTitle(const std::string &title);
	UABE_Win32_API bool SetTitle(const std::wstring &title);
	//Sets the description of the progress indicator, usually referring to the current step.
	UABE_Win32_API bool SetDescription(const std::string &desc);
	UABE_Win32_API bool SetDescription(const std::wstring &desc);

	//Adds text to the log.
	UABE_Win32_API bool AddLogText(const std::string &text);
	UABE_Win32_API bool AddLogText(const std::wstring &text);
	
	//Enables or disables the cancel button.
	UABE_Win32_API bool SetCancellable(bool cancellable);

	//Adds a cancel callback. Called by the window handler from another thread, or by SetCancelled.
	//pCallback must not be freed before destroying the progress indicator.
	UABE_Win32_API bool AddCancelCallback(std::unique_ptr<ICancelCallback> pCallback);
	//Retrieves the current cancel status.
	UABE_Win32_API bool IsCancelled();
	//Sets the current cancel status.
	UABE_Win32_API bool SetCancelled(bool cancelled);
};