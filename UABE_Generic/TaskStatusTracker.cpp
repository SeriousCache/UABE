#include "TaskStatusTracker.h"
#include <numeric>

TaskStatusTracker::TaskStatusTracker(AppContext& appContext)
	: appContext(appContext)
{
	appContext.taskManager.addCallback(this);
}
TaskStatusTracker::~TaskStatusTracker()
{
	appContext.taskManager.removeCallback(this);
}

void TaskStatusTracker::updateTotalProgress()
{
	if (taskList.empty())
	{
		totalProgress = 0.0f;
		this->mainOnTotalProgressUpdate();
		return;
	}
	float progressPerTask = 1.0f / (float)(taskList.size() - staleTaskRefs.size());
	totalProgress = std::transform_reduce(taskList.begin(), taskList.end(), 0.0f,
		[](float a, float b) {return a + b; },
		[progressPerTask](const TaskStatusDesc& desc) {
			if (desc.hasResult)
				return 0.0f;
			if (desc.range == 0)
				return 0.0f;
			if (desc.progress >= desc.range)
				return 1.0f;
			return progressPerTask * static_cast<float>(static_cast<double>(desc.progress) / static_cast<double>(desc.range));
		});
	this->mainOnTotalProgressUpdate();
}
void TaskStatusTracker::updateLatestProgressMessage()
{
	auto latestElemIt = std::max_element(taskList.begin(), taskList.end(),
		[](const TaskStatusDesc& a, const TaskStatusDesc& b) { return a.progressDescNumber < b.progressDescNumber; });
	if (latestElemIt == taskList.end())
	{
		latestProgressMessage.clear();
		this->mainOnProgressMessageUpdate();
		return;
	}
	latestProgressMessage = latestElemIt->curProgressDesc;
	this->mainOnProgressMessageUpdate();
}

void TaskStatusTracker::OnAdd(std::shared_ptr<ITask>& pTask)
{
	if (pTask == nullptr)
		return;
	appContext.postMainThreadCallback([this, pTask]() {
		auto taskListIt = taskList.end();
		auto taskMapIt = taskByPtr.find(pTask.get());
		if (taskMapIt != taskByPtr.end())
			return;
		taskListIt = taskList.insert(taskList.end(), TaskStatusDesc(pTask, taskList.end(), staleTaskRefs.end()));
		taskListIt->selfRef = taskListIt;
		taskListIt->name = pTask->getName();
		taskListIt->progress = 0;
		taskListIt->range = 0;
		taskByPtr.insert({ pTask.get(), taskListIt });
		this->updateTotalProgress();
		this->mainOnTaskAdd(taskListIt);
		});
}

void TaskStatusTracker::OnProgress(std::shared_ptr<ITask>& pTask, unsigned int progress, unsigned int range)
{
	if (pTask == nullptr)
		return;
	appContext.postMainThreadCallback([this, pTask, progress, range]() {
		auto taskMapIt = taskByPtr.find(pTask.get());
		if (taskMapIt == taskByPtr.end())
			return;
		auto taskListIt = taskMapIt->second;
		taskListIt->progress = progress;
		taskListIt->range = range;
		this->updateTotalProgress();
		this->mainOnTaskProgressUpdate(taskListIt);
	});
}

void TaskStatusTracker::OnProgressDesc(std::shared_ptr<ITask>& pTask, const std::string& desc)
{
	if (pTask == nullptr)
		return;
	appContext.postMainThreadCallback([this, pTask, desc]() {
		auto taskMapIt = taskByPtr.find(pTask.get());
		if (taskMapIt == taskByPtr.end())
			return;
		auto taskListIt = taskMapIt->second;
		taskListIt->curProgressDesc.assign(desc);
		if (!desc.empty())
		{
			if (this->progressDescCounter == std::numeric_limits<decltype(this->progressDescCounter)>::max())
			{
				this->progressDescCounter = 1;
				for (TaskStatusDesc& desc : taskList)
					desc.progressDescNumber = (desc.curProgressDesc.empty() ? 0 : 1);
			}
			taskListIt->progressDescNumber = ++this->progressDescCounter;
			this->latestProgressMessage = desc;
			this->mainOnProgressMessageUpdate();
		}
		else
		{
			taskListIt->progressDescNumber = 0;
			this->updateLatestProgressMessage();
		}
		this->mainOnTaskProgressDescUpdate(taskListIt);
	});
}
void TaskStatusTracker::OnLogMessage(std::shared_ptr<ITask>& pTask, const std::string& msg)
{
	if (pTask == nullptr)
		return;
	
	appContext.postMainThreadCallback([this, pTask, msg]() {
		auto taskMapIt = taskByPtr.find(pTask.get());
		if (taskMapIt == taskByPtr.end())
			return;
		auto taskListIt = taskMapIt->second;
		if (!msg.ends_with('\n'))
			taskListIt->messages.emplace_back(msg + "\n");
		else
			taskListIt->messages.emplace_back(msg);
		this->mainOnTaskAddLogMessage(taskListIt);
	});
}
void TaskStatusTracker::OnCompletion(std::shared_ptr<ITask>& pTask, TaskResult result)
{
	if (pTask == nullptr)
		return;
	appContext.postMainThreadCallback([this, pTask, result]() {
		auto taskMapIt = taskByPtr.find(pTask.get());
		if (taskMapIt == taskByPtr.end())
			return;
		auto taskListIt = taskMapIt->second;
		taskByPtr.erase(taskMapIt);
		assert(taskListIt->hasResult == false);
		taskListIt->hasResult = true;
		taskListIt->result = result;
		this->lastTaskResult = result;
		unsigned int progressDescNumber = taskListIt->progressDescNumber;
		taskListIt->progressDescNumber = 0;
		bool taskProgressDescUpdate = (!taskListIt->curProgressDesc.empty());
		taskListIt->curProgressDesc.clear();
		staleTaskRefs.push_back(taskListIt);
		if (progressDescNumber == this->progressDescCounter)
			this->updateLatestProgressMessage();
		else
			this->mainOnProgressMessageUpdate(); //Let the tracker subclass update task counter labels, etc..
		this->updateTotalProgress();
		if (taskProgressDescUpdate)
			this->mainOnTaskProgressDescUpdate(taskListIt);
		if (taskListIt->cancelable)
		{
			taskListIt->cancelable = false;
			this->mainOnTaskCancelableChange(taskListIt);
		}
		this->mainOnTaskCompletion(taskListIt);
	});
}
void TaskStatusTracker::OnCancelableChange(std::shared_ptr<ITask>& pTask, bool cancelable)
{
	if (pTask == nullptr)
		return;
	appContext.postMainThreadCallback([this, pTask, cancelable]() {
		auto taskMapIt = taskByPtr.find(pTask.get());
		if (taskMapIt == taskByPtr.end())
			return;
		auto taskListIt = taskMapIt->second;
		taskListIt->cancelable = cancelable;
		this->mainOnTaskCancelableChange(taskListIt);
	});
}

void TaskStatusTracker::preEraseElement(std::list<TaskStatusDesc>::iterator listEntry)
{}

void TaskStatusTracker::eraseStaleElements()
{
	if (erasingStaleElements)
	{
		repeatEraseStaleElements = true;
		return;
	}
	erasingStaleElements = true;
	auto staleListEndIt = staleTaskRefs.end();
	do {
		repeatEraseStaleElements = false;
		for (auto staleListIt = staleTaskRefs.begin(); staleListIt != staleTaskRefs.end();)
		{
			auto curStaleListIt = staleListIt;
			++staleListIt;

			typename decltype(taskList)::iterator taskListIt = *curStaleListIt;
			if (taskListIt->eraseIfStale)
			{
				preEraseElement(taskListIt);
				staleTaskRefs.erase(curStaleListIt);
				taskList.erase(taskListIt);
			}
		}
	} while (repeatEraseStaleElements);
	erasingStaleElements = false;
}

void TaskStatusTracker::mainOnTaskAdd(std::list<TaskStatusDesc>::iterator listEntry)
{}
void TaskStatusTracker::mainOnTaskProgressUpdate(std::list<TaskStatusDesc>::iterator listEntry)
{}
void TaskStatusTracker::mainOnTaskProgressDescUpdate(std::list<TaskStatusDesc>::iterator listEntry)
{}
void TaskStatusTracker::mainOnTaskAddLogMessage(std::list<TaskStatusDesc>::iterator listEntry)
{}
void TaskStatusTracker::mainOnTaskCompletion(std::list<TaskStatusDesc>::iterator listEntry)
{
	eraseStaleElements();
}
void TaskStatusTracker::mainOnTaskCancelableChange(std::list<TaskStatusDesc>::iterator listEntry)
{}

void TaskStatusTracker::mainOnTotalProgressUpdate()
{}
void TaskStatusTracker::mainOnProgressMessageUpdate()
{}
