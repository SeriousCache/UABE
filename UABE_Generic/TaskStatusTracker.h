#pragma once
#include "AsyncTask.h"
#include "AppContext.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <concepts>

struct TaskStatusDesc
{
	std::list<TaskStatusDesc>::iterator selfRef;

	std::weak_ptr<ITask> wpTask;
	std::string name;
	unsigned int progress = 0, range = 100;
	std::vector<std::string> messages;
	std::string curProgressDesc;
	unsigned int progressDescNumber = 0; //Higher: more recent

	bool cancelable = false;

	bool hasResult = false;
	//Invalid as long as !hasResult.
	TaskResult result = 0;

	//Reference into staleTaskRefs (sorry for the type).
	std::list<std::list<TaskStatusDesc>::iterator>::iterator staleTaskSelfRef;
	bool eraseIfStale = true;

	//Additional field not touched by the TaskStatusTracker base class.
	uintptr_t auxData = 0;

	inline TaskStatusDesc(const std::shared_ptr<ITask>& pTask, decltype(selfRef) selfRef, decltype(staleTaskSelfRef) staleTaskSelfRef)
		: selfRef(selfRef), wpTask(pTask), staleTaskSelfRef(staleTaskSelfRef)
	{}
};
class TaskStatusTracker : public TaskProgressCallback
{
	void updateTotalProgress();
	void updateLatestProgressMessage();
	unsigned int progressDescCounter = 0;
	bool erasingStaleElements = false, repeatEraseStaleElements = false;
protected:
	class AppContext& appContext;
	//Main task status list.
	std::list<TaskStatusDesc> taskList;
	//Fast lookup for the On* main thread callback.
	std::unordered_map<ITask*, decltype(taskList)::iterator> taskByPtr;
	//Main task status list.
	std::list<decltype(taskList)::iterator> staleTaskRefs;

	float totalProgress = 0.0f;
	std::string latestProgressMessage;
	TaskResult lastTaskResult = 0;
public:
	UABE_Generic_API TaskStatusTracker(class AppContext& appContext);
	UABE_Generic_API ~TaskStatusTracker();

	UABE_Generic_API void OnAdd(std::shared_ptr<ITask>& pTask);
	UABE_Generic_API void OnProgress(std::shared_ptr<ITask>& pTask, unsigned int progress, unsigned int range);
	UABE_Generic_API void OnProgressDesc(std::shared_ptr<ITask>& pTask, const std::string& desc);
	UABE_Generic_API void OnLogMessage(std::shared_ptr<ITask>& pTask, const std::string& msg);
	UABE_Generic_API void OnCompletion(std::shared_ptr<ITask>& pTask, TaskResult result);
	UABE_Generic_API void OnCancelableChange(std::shared_ptr<ITask>& pTask, bool cancelable);

	UABE_Generic_API void eraseStaleElements();

public:
	UABE_Generic_API virtual void preEraseElement(std::list<TaskStatusDesc>::iterator listEntry);
	UABE_Generic_API virtual void mainOnTaskAdd(std::list<TaskStatusDesc>::iterator listEntry);
	UABE_Generic_API virtual void mainOnTaskProgressUpdate(std::list<TaskStatusDesc>::iterator listEntry);
	UABE_Generic_API virtual void mainOnTaskProgressDescUpdate(std::list<TaskStatusDesc>::iterator listEntry);
	UABE_Generic_API virtual void mainOnTaskAddLogMessage(std::list<TaskStatusDesc>::iterator listEntry);
	UABE_Generic_API virtual void mainOnTaskCompletion(std::list<TaskStatusDesc>::iterator listEntry);
	UABE_Generic_API virtual void mainOnTaskCancelableChange(std::list<TaskStatusDesc>::iterator listEntry);

	UABE_Generic_API virtual void mainOnTotalProgressUpdate();
	UABE_Generic_API virtual void mainOnProgressMessageUpdate();
};
