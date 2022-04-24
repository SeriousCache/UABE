#pragma once
#include "api.h"
#include <string>
#include <memory>

class IProgressIndicator
{
public:
	UABE_Generic_API IProgressIndicator();
	UABE_Generic_API virtual ~IProgressIndicator();
	//Cancel status callback interface.
	class ICancelCallback
	{
	public:
		//Called when the progress indicator's cancel status changes. There is no guarantee which thread this is called from.
		virtual void OnCancelEvent(bool cancel) = 0;
	};

	//Instructs the progress indicator to stay open or not to stay open (default) with an OK button instead of Cancel if something was written to the log.
	virtual void SetDontCloseIfLog(bool dontclose = true) = 0;

	//Adds a new step and returns its index. The step with index 0 already exists before calling AddStep the first time.
	virtual size_t AddStep(unsigned int range = 0) = 0;
	//Sets the range of a step, which can represent the smallest unit by which the progress of this step can advance.
	virtual bool SetStepRange(size_t idx, unsigned int range) = 0;
	//Sets the progress of the current step, which should be a value from 0 to (including) range.
	virtual bool SetStepStatus(unsigned int progress) = 0;

	//Jumps to a step and sets its progress.
	virtual bool JumpToStep(size_t idx, unsigned int progress = 0) = 0;
	//Goes to the next step, setting its progress to 0. Returns (size_t)-1 on failure, the new idx otherwise.
	virtual size_t GoToNextStep() = 0;

	//Sets the progress indicator's window title.
	virtual bool SetTitle(const std::string& title) = 0;
	virtual bool SetTitle(const std::wstring& title) = 0;
	//Sets the description of the progress indicator, usually referring to the current step.
	virtual bool SetDescription(const std::string& desc) = 0;
	virtual bool SetDescription(const std::wstring& desc) = 0;

	//Adds text to the log.
	virtual bool AddLogText(const std::string& text) = 0;
	virtual bool AddLogText(const std::wstring& text) = 0;
	//Adds a line of text to the log.
	inline bool AddLogLine(std::string line) { return AddLogText(line + "\r\n"); }
	inline bool AddLogLine(std::wstring line) { return AddLogText(line + L"\r\n"); }

	//Enables or disables the cancel button.
	virtual bool SetCancellable(bool cancellable) = 0;

	//Adds a cancel callback. Called by the window handler from another thread, or by SetCancelled.
	virtual bool AddCancelCallback(std::unique_ptr<ICancelCallback> pCallback) = 0;
	//Retrieves the current cancel status.
	virtual bool IsCancelled() = 0;
	//Sets the current cancel status.
	virtual bool SetCancelled(bool cancelled) = 0;
};
