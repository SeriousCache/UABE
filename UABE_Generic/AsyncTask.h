#pragma once
#include "api.h"
#include <string>
#include <list>
#include <vector>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>

//result -128 : canceled; result < 0 : error. result >= 0 : success.
typedef int TaskResult;
static const int TaskResult_Canceled = -128;

class ITask;
class TaskProgressCallback
{
public:
	UABE_Generic_API TaskProgressCallback();
	UABE_Generic_API virtual ~TaskProgressCallback();
	//Callbacks can be called from the main thread as well as from any task thread!
	UABE_Generic_API virtual void OnAdd(std::shared_ptr<ITask> &pTask);
	UABE_Generic_API virtual void OnProgress(std::shared_ptr<ITask> &pTask, unsigned int progress, unsigned int range);
	UABE_Generic_API virtual void OnProgressDesc(std::shared_ptr<ITask> &pTask, const std::string &desc);
	UABE_Generic_API virtual void OnLogMessage(std::shared_ptr<ITask> &pTask, const std::string &msg);
	UABE_Generic_API virtual void OnCompletion(std::shared_ptr<ITask> &pTask, TaskResult result); //Result 0 means success, < 0 means error. Specific values depend on the task.
	UABE_Generic_API virtual void OnCancelableChange(std::shared_ptr<ITask>& pTask, bool cancelable);
};

class TaskProgressManager;
class ITask
{
public:
	UABE_Generic_API ITask();
	UABE_Generic_API virtual ~ITask();
	UABE_Generic_API virtual const std::string &getName()=0;
	//Configure the task progress manager before it lands in the queue. Default : stub.
	UABE_Generic_API virtual void onEnqueue(TaskProgressManager &progressManager);
	UABE_Generic_API virtual TaskResult execute(TaskProgressManager &progressManager)=0;
	//Assumes that if this task is not ready, there is another task in queue that is ready and that works toward making this task ready.
	//Should return very fast as this is executed while the task manager is locked.
	//Default: return true.
	UABE_Generic_API virtual bool isReady();
};

class TaskManager;
class TaskProgressManager
{
	TaskManager *pManager;
	std::shared_ptr<ITask> pTask;
	std::atomic_bool cancelable;
	std::atomic_bool canceled;
	TaskProgressManager(TaskManager *pManager, std::shared_ptr<ITask> pTask);
public:
	UABE_Generic_API std::shared_ptr<ITask> &getTask();
	UABE_Generic_API void setProgress(unsigned int progress, unsigned int range);
	UABE_Generic_API void setProgressDesc(const std::string &desc);
	UABE_Generic_API void logMessage(const std::string &msg);
	UABE_Generic_API void setCancelable();
	UABE_Generic_API bool isCanceled();
	friend class TaskManager;
};

class TaskManager
{
	struct TaskDesc
	{
		std::shared_ptr<ITask> pTask;
		TaskProgressManager *pProgMgr;
		bool taken;
	};
protected:
	void LockCallbacksDelete();
	void UnlockCallbacksDelete();
	std::vector<TaskProgressCallback*> callbacks;
private:
	unsigned int nMaxThreads;
	std::atomic_uint nThreads;
	std::atomic_uint nThreadsCommit;
	std::atomic_uint nThreadsReady;
	std::atomic_uint nThreadsWorking;
	std::atomic_uint nThreadsProcessingCallbacks;
	std::atomic_uint nCallbackRemovalsInProgress;
	std::list<TaskDesc> tasks;
	//Only used by the main thread.
	std::vector<std::jthread> threads;

	std::recursive_mutex taskListMutex;
	std::mutex threadEventMutex;
	std::condition_variable threadEventVar;
	bool newTaskEvent;
	bool threadCloseEvent;
	std::condition_variable threadClosedEventVar;
	bool threadClosedEvent;

	static void TaskThread(void *param);

public:
	UABE_Generic_API TaskManager(unsigned int nMaxThreads = 1);
	UABE_Generic_API ~TaskManager();
	
	//All of these functions except removeCallback must be called by one thread at a time only.

	//Sets the maximum amount of threads.
	// If there are more than nMaxThreads running, waits for the excess threads to stop, unless waitClose is set to false.
	// However, a wait will be neccessary if nMaxThreads is reduced and then raised with threads still pending to close.
	//   This ensures that the internal thread indices are sequential from 0 to n-1.
	//   If allowRequiredWait is set to false in this case, the function will fail and return false.
	// -> Win32 threads with open window handles should set both allowMandatoryWait and waitClose to false to prevent ugly deadlocks.
	//    See the Microsoft documentation on WaitForSingleObject.
	UABE_Generic_API bool setMaxThreads(unsigned int nMaxThreads, bool allowRequiredWait = true, bool waitClose = true);
	inline unsigned int getNumThreads()
	{
		return this->nThreads.load();
	}
	//Note: getNumThreadsWorking() changes asynchronously.
	inline unsigned int getNumThreadsWorking()
	{
		return this->nThreadsWorking.load();
	}

	UABE_Generic_API void addCallback(TaskProgressCallback *pCallback);
	UABE_Generic_API void removeCallback(TaskProgressCallback *pCallback);
	//Enqueues a new task. May or may not create new worker threads.
	UABE_Generic_API bool enqueue(std::shared_ptr<ITask> pTask);
	//If the task does not run yet, removes it from the list and sets *taskRunning to false.
	//If the task runs and is cancelable, sets the isCanceled flag in TaskProgressManager for the specified task and sets *taskRunning to true.
	//Returns true if the task was found and cancelled or removed.
	UABE_Generic_API bool cancel(ITask *pTask, bool *taskRunning = nullptr);

	friend class TaskProgressManager;
};