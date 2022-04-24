#include "AsyncTask.h"
#include <chrono>
#include <cassert>

ITask::ITask()
{}
ITask::~ITask()
{}
void ITask::onEnqueue(TaskProgressManager &progressManager)
{}
bool ITask::isReady()
{
	return true;
}

TaskProgressCallback::TaskProgressCallback() {}
TaskProgressCallback::~TaskProgressCallback() {}
void TaskProgressCallback::OnAdd(std::shared_ptr<ITask> &pTask){}
void TaskProgressCallback::OnProgress(std::shared_ptr<ITask> &pTask, unsigned int progress, unsigned int range){}
void TaskProgressCallback::OnProgressDesc(std::shared_ptr<ITask> &pTask, const std::string &desc){}
void TaskProgressCallback::OnLogMessage(std::shared_ptr<ITask> &pTask, const std::string &msg){}
void TaskProgressCallback::OnCompletion(std::shared_ptr<ITask> &pTask, TaskResult result){}
void TaskProgressCallback::OnCancelableChange(std::shared_ptr<ITask>& pTask, bool cancelable) {}

TaskProgressManager::TaskProgressManager(TaskManager *pManager, std::shared_ptr<ITask> _pTask)
	: pManager(pManager), pTask(std::move(_pTask)), cancelable(false), canceled(false)
{}

std::shared_ptr<ITask> &TaskProgressManager::getTask()
{
	return this->pTask;
}
void TaskProgressManager::setProgress(unsigned int progress, unsigned int range)
{
	this->pManager->LockCallbacksDelete();
	std::unique_lock<std::recursive_mutex> taskListLock(this->pManager->taskListMutex);
	std::vector<TaskProgressCallback*> tempCallbacks(this->pManager->callbacks);
	taskListLock.unlock();

	for (size_t i = 0; i < tempCallbacks.size(); i++)
		tempCallbacks[i]->OnProgress(this->pTask, progress, range);

	this->pManager->UnlockCallbacksDelete();
}
void TaskProgressManager::setProgressDesc(const std::string &desc)
{
	this->pManager->LockCallbacksDelete();
	std::unique_lock<std::recursive_mutex> taskListLock(this->pManager->taskListMutex);
	std::vector<TaskProgressCallback*> tempCallbacks(this->pManager->callbacks);
	taskListLock.unlock();

	for (size_t i = 0; i < tempCallbacks.size(); i++)
		tempCallbacks[i]->OnProgressDesc(this->pTask, desc);

	this->pManager->UnlockCallbacksDelete();
}
void TaskProgressManager::logMessage(const std::string &msg)
{
	this->pManager->LockCallbacksDelete();
	std::unique_lock<std::recursive_mutex> taskListLock(this->pManager->taskListMutex);
	std::vector<TaskProgressCallback*> tempCallbacks(this->pManager->callbacks);
	taskListLock.unlock();

	for (size_t i = 0; i < tempCallbacks.size(); i++)
		tempCallbacks[i]->OnLogMessage(this->pTask, msg);

	this->pManager->UnlockCallbacksDelete();
}
void TaskProgressManager::setCancelable()
{
	bool prevCancelable = this->cancelable.exchange(true);
	if (prevCancelable)
		return;
	this->pManager->LockCallbacksDelete();
	std::unique_lock<std::recursive_mutex> taskListLock(this->pManager->taskListMutex);
	std::vector<TaskProgressCallback*> tempCallbacks(this->pManager->callbacks);
	taskListLock.unlock();

	for (size_t i = 0; i < tempCallbacks.size(); i++)
		tempCallbacks[i]->OnCancelableChange(this->pTask, true);

	this->pManager->UnlockCallbacksDelete();
}
bool TaskProgressManager::isCanceled()
{
	return this->canceled.load();
}

struct TaskThreadParam
{
	TaskManager *pThis;
	unsigned int threadIdx;
};
void TaskManager::TaskThread(void *param)
{
	TaskManager *pThis;
	unsigned int threadIdx;
	{
		TaskThreadParam *pParam = (TaskThreadParam*)param;
		pThis = pParam->pThis;
		threadIdx = pParam->threadIdx;
		delete pParam;
	}
	++pThis->nThreadsReady;
	bool lastHadTask = false;
	//HANDLE waitHandles[2] = {pThis->hNewTaskEvent, pThis->hThreadCloseEvent};
	while (true)
	{
		std::unique_lock<std::mutex> threadEventLock(pThis->threadEventMutex);
		if (!lastHadTask)
		{
			pThis->threadEventVar.wait(threadEventLock, [pThis]() {return pThis->newTaskEvent || pThis->threadCloseEvent; });
		}
		//if (lastHadTask || WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE) == WAIT_OBJECT_0)
		if (lastHadTask || pThis->newTaskEvent)
		{
			//Have to unlock threadEventLock here to prevent deadlocks
			// if the owner of taskListMutex waits for threadEventMutex.
			threadEventLock.unlock();
			std::unique_lock<std::recursive_mutex> taskListLock(pThis->taskListMutex);
			if (pThis->tasks.size() < 1)
			{
				//newTaskEvent is always set within 
				pThis->newTaskEvent = false;
				taskListLock.unlock();
				lastHadTask = false;
			}
			else
			{
				//New tasks are always pushed to the front.
				//std::list has persistent iterators, i.e. the iterator stays valid until the element is removed.
				auto elemIterator = pThis->tasks.begin();
				while (!elemIterator->taken && !elemIterator->pTask->isReady()) {
					elemIterator++;
				}
				bool taken = (elemIterator == pThis->tasks.end()) || (elemIterator->taken);
				std::shared_ptr<ITask> pCurTask;
				TaskProgressManager *pCurProgMgr;
				if (!taken)
				{
					TaskDesc &curTaskDesc = *elemIterator;
					pCurTask = curTaskDesc.pTask;
					pCurProgMgr = curTaskDesc.pProgMgr;
					//Take this task and move it to the back.
					curTaskDesc.taken = true;

					TaskDesc curTaskDescCopy = curTaskDesc;
					pThis->tasks.erase(elemIterator);
					pThis->tasks.push_back(curTaskDescCopy);
					elemIterator = pThis->tasks.end();
					--elemIterator;

					--pThis->nThreadsReady;
					++pThis->nThreadsWorking;
					//Reset the new task event if no more tasks are waiting.
					if (pThis->tasks.size() >= 1 && pThis->tasks.front().taken)
						pThis->newTaskEvent = false;
				}
				taskListLock.unlock();
				if (!taken)
				{
					pThis->LockCallbacksDelete();
					taskListLock.lock();
					std::vector<TaskProgressCallback*> tempCallbacks(pThis->callbacks);
					taskListLock.unlock();

					for (size_t i = 0; i < tempCallbacks.size(); i++)
						tempCallbacks[i]->OnCancelableChange(pCurTask, false);
					tempCallbacks.clear();
					pThis->UnlockCallbacksDelete();

					//Execute the task.
					TaskResult result = pCurTask->execute(*pCurProgMgr);

					pThis->LockCallbacksDelete();
					taskListLock.lock();
					tempCallbacks = pThis->callbacks;
					//Remove the task from the list.
					pThis->tasks.erase(elemIterator);
					taskListLock.unlock();

					for (size_t i = 0; i < tempCallbacks.size(); i++)
						tempCallbacks[i]->OnCompletion(pCurTask, result);
					pThis->UnlockCallbacksDelete();


					delete pCurProgMgr;

					--pThis->nThreadsWorking;
					++pThis->nThreadsReady;
					lastHadTask = true;
				}
				else
					lastHadTask = false;
			}
		}
		else if (threadIdx >= pThis->nMaxThreads)
			break;
		else
		{
			threadEventLock.unlock();
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(10ms);
		}
	}
	--pThis->nThreadsReady;
	--pThis->nThreads;
	{
		std::scoped_lock<std::mutex> lock(pThis->threadEventMutex);
		pThis->threadClosedEvent = true;
	}
	pThis->threadClosedEventVar.notify_all();
	--pThis->nThreadsCommit;
	return;
}
void TaskManager::LockCallbacksDelete()
{
	//Make sure no callbacks are being removed while we operate. Busy waiting.
	while (true) {
		++this->nThreadsProcessingCallbacks;
		//Alternative : Surround increment and if condition in critical section (also see removeCallback).
		std::atomic_thread_fence(std::memory_order::seq_cst);
		if (this->nCallbackRemovalsInProgress)
		{
			--this->nThreadsProcessingCallbacks;
			while (this->nCallbackRemovalsInProgress) {std::this_thread::yield();}
			continue;
		}
		else
			break;
	}
}
void TaskManager::UnlockCallbacksDelete()
{
	--this->nThreadsProcessingCallbacks;
}

TaskManager::TaskManager(unsigned int nMaxThreads)
{
	this->newTaskEvent = false;
	this->threadCloseEvent = false;
	this->threadClosedEvent = false;
	this->nMaxThreads = nMaxThreads;
	this->nThreads = 0;
	this->nThreadsCommit = 0;
	this->nThreadsReady = 0;
	this->nThreadsProcessingCallbacks = 0;
	this->nCallbackRemovalsInProgress = 0;
}
TaskManager::~TaskManager()
{
	setMaxThreads(0);
	//Make sure we don't delete hThreadClosedEvent before the last SetEvent call has been processed.
	while (this->nThreadsCommit > 0){}
	auto taskIterator = this->tasks.cbegin();
	for (; taskIterator != this->tasks.cend(); ++taskIterator) {delete taskIterator->pProgMgr;}
	this->tasks.clear();
}
bool TaskManager::setMaxThreads(unsigned int nMaxThreads, bool allowRequiredWait, bool waitClose)
{
	auto waitThreadsClosed = [this]()
	{
		{
			std::scoped_lock<std::mutex> lock(this->threadEventMutex);
			this->threadCloseEvent = true;
		}
		this->threadEventVar.notify_all();
		while (this->nThreads > this->nMaxThreads)
		{
			std::unique_lock<std::mutex> lock(this->threadEventMutex);
			this->threadClosedEventVar.wait(lock, [this]() {return this->threadClosedEvent; });
			this->threadClosedEvent = false;
		}
		threads.erase(threads.begin() + nThreads, threads.end());
	};
	if (this->nThreads > this->nMaxThreads && nMaxThreads > this->nMaxThreads)
	{
		//Still waiting for some threads to close after having reduced nMaxThreads.
		//Raising nMaxThreads again could otherwise cause gaps in the thread ID assignment.
		if (allowRequiredWait)
		{
			waitThreadsClosed();
		}
		else
		{
			//Make sure the value is updated if setMaxThreads is called again.
			std::atomic_thread_fence(std::memory_order::seq_cst);
			return false;
		}
	}
	this->nMaxThreads = nMaxThreads;
	waitThreadsClosed();
	//ResetEvent(this->hThreadCloseEvent);
	return true;
}
bool TaskManager::enqueue(std::shared_ptr<ITask> pTask)
{
	TaskDesc taskDesc;
	taskDesc.pProgMgr = new TaskProgressManager(this, pTask);
	taskDesc.pTask = pTask; taskDesc.taken = false;
	pTask->onEnqueue(*taskDesc.pProgMgr);

	bool startNewThread = false;
	unsigned int newThreadIdx;

	//callbacks is only modified by the main thread.
	for (size_t i = 0; i < this->callbacks.size(); i++)
	{
		this->callbacks[i]->OnAdd(pTask);
		this->callbacks[i]->OnCancelableChange(pTask, true);
	}

	std::unique_lock<std::recursive_mutex> taskListLock(this->taskListMutex);
	auto taskIterator = this->tasks.cbegin();
	for (; taskIterator != this->tasks.cend() && !taskIterator->taken; ++taskIterator) {}
	this->tasks.emplace(taskIterator, taskDesc);
	if (this->nThreadsReady == 0 && this->nThreads < this->nMaxThreads)
	{
		newThreadIdx = this->nThreads;
		++this->nThreads;
		startNewThread = true;
	}
	{
		std::scoped_lock<std::mutex> threadEventLock(threadEventMutex);
		newTaskEvent = true;
	}
	//notify_one would probably be more performant since each Task can only be run by one thread.
	threadEventVar.notify_all();
	taskListLock.unlock();

	if (startNewThread)
	{
		TaskThreadParam *pParam = new TaskThreadParam;
		pParam->pThis = this;
		pParam->threadIdx = newThreadIdx;
		++this->nThreadsCommit;
		assert(newThreadIdx == threads.size());
		threads.emplace_back(&TaskThread, (void*)pParam);
	}
	return true;
}
bool TaskManager::cancel(ITask *pTask, bool *out_taskRunning)
{
	bool ret = false, taskRunning = false;
	if (out_taskRunning)
		*out_taskRunning = false;
	std::unique_lock<std::recursive_mutex> taskListLock(this->taskListMutex);
	//callbacks is only modified by the main thread.
	std::vector<TaskProgressCallback*> tempCallbacks = this->callbacks;
	auto taskIterator = this->tasks.cbegin();
	std::shared_ptr<ITask> taskRef;
	for (; taskIterator != this->tasks.cend() && taskIterator->pTask.get() != pTask; ++taskIterator) {}
	if (taskIterator != this->tasks.cend())
	{
		taskRef = taskIterator->pTask;
		if (taskIterator->taken)
		{
			if (taskIterator->pProgMgr->cancelable)
			{
				taskIterator->pProgMgr->canceled.store(true);
				ret = true;
				taskRunning = true;
				if (out_taskRunning)
					*out_taskRunning = true;
			}
		}
		else
		{
			delete taskIterator->pProgMgr;
			this->tasks.erase(taskIterator);
			ret = true;
		}
	}
	taskListLock.unlock();
	if (ret)
	{
		for (size_t i = 0; i < tempCallbacks.size(); i++)
		{
			if (taskRunning)
				tempCallbacks[i]->OnCancelableChange(taskRef, false);
			else
				tempCallbacks[i]->OnCompletion(taskRef, TaskResult_Canceled);
		}
	}
	return ret;
}
void TaskManager::addCallback(TaskProgressCallback *pCallback)
{
	std::scoped_lock<std::recursive_mutex> taskListLock(this->taskListMutex);
	this->callbacks.push_back(pCallback);
}
void TaskManager::removeCallback(TaskProgressCallback *pCallback)
{
	//Custom critical section : no thread may use callbacks while the main thread removes one to prevent use-after-free conditions.
	++this->nCallbackRemovalsInProgress;
	//Alternative : Surround "...=true;" and first condition check in critical section (also see TaskThread).
	std::atomic_thread_fence(std::memory_order::seq_cst);
	while (this->nThreadsProcessingCallbacks > 0) {std::this_thread::yield();}
	
	//Inner critical section specifically for callbacks.
	std::unique_lock<std::recursive_mutex> taskListLock(this->taskListMutex);
	for (size_t _i = this->callbacks.size(); _i > 0; _i--)
	{
		size_t i = _i - 1;
		if (this->callbacks[i] == pCallback)
		{
			this->callbacks.erase(this->callbacks.begin() + i);
			break;
		}
	}
	taskListLock.unlock();
	--this->nCallbackRemovalsInProgress;
}