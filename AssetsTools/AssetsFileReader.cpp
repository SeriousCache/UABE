#include "stdafx.h"
#include "AssetsFileReader.h"
#include "../libStringConverter/convert.h"
#include <vector>
#include <tchar.h>
#include <mutex>

void AssetsVerifyLoggerToConsole(const char *message)
{
	printf("%s\n", message);
}

std::vector<IAssetsReopenable*> reopenableInstances;
std::mutex reopenableInstancesMutex;
void AddReopenable(IAssetsReopenable *pReopenable)
{
	std::scoped_lock<std::mutex> reopenableInstancesLock(reopenableInstancesMutex);
	reopenableInstances.push_back(pReopenable);
}
void RemoveReopenable(IAssetsReopenable *pReopenable)
{
	std::scoped_lock<std::mutex> reopenableInstancesLock(reopenableInstancesMutex);
	for (size_t i = 1; i <= reopenableInstances.size(); i++)
	{
		if (reopenableInstances[i-1] == pReopenable)
		{
			reopenableInstances.erase(reopenableInstances.begin() + (i-1));
			i--;
		}
	}
}
bool GarbageCollectReopenables(int nMax = 16)
{
	bool collected = false;
	std::scoped_lock<std::mutex> reopenableInstancesLock(reopenableInstancesMutex);
	for (size_t i = 0; i < reopenableInstances.size(); i++)
	{
		if (!nMax)
			break;
		if (reopenableInstances[i]->IsOpen() && reopenableInstances[i]->Close())
		{
			collected = true;
			nMax--;
		}
	}
	return collected;
}

void Free_AssetsReopenable(IAssetsReopenable *pObject)
{
	delete pObject;
}

IAssetsReopenable::~IAssetsReopenable() {}

class AssetsReaderFromFile : public IAssetsReader
{
	AssetsRWOpenFlags flags;
protected:
	bool binary;
	TCHAR *filePath;

	bool wasOpened;
	QWORD lastFilePos;

	FILE *pFile;
	std::recursive_mutex fileOperationMutex;
public:
	AssetsReaderFromFile(const TCHAR *filePath, bool binary, AssetsRWOpenFlags flags)
	{
		this->flags = flags;
		this->binary = binary;

		size_t pathLen = wcslen(filePath) + 1;
		this->filePath = new TCHAR[pathLen];
		memcpy(this->filePath, filePath, pathLen * sizeof(TCHAR));

		this->wasOpened = false;
		this->pFile = NULL;
		this->lastFilePos = 0;
	}
	AssetsReaderFromFile(FILE *pFile)
	{
		this->pFile = pFile;

		this->flags = RWOpenFlags_Immediately | RWOpenFlags_Unclosable;
		this->binary = true;

		this->filePath = NULL;
		
		this->wasOpened = true;
		this->lastFilePos = 0;
	}
	~AssetsReaderFromFile()
	{
		RemoveReopenable(this);
		if (filePath)
		{
			free(filePath);
			filePath = NULL;
			flags = RWOpenFlags_None; //RWOpenFlags_Unclosable could otherwise prevent closing the file.
			Close();
		}
	}
	bool Reopen()
	{
		bool ret = true;
		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		if (IsOpen())
			ret = true;
		else if (filePath == NULL)
			ret = false;
		else
		{
			FILE *pTempFile = NULL;
			TCHAR mode[3] = {_T('r'), binary ? _T('b') : 0, 0};
			errno_t err = _tfopen_s(&pTempFile, filePath, mode);
			if (err != 0)
			{
				if (err == ENFILE || err == EMFILE)
				{
					GarbageCollectReopenables();
					err = _tfopen_s(&pTempFile, filePath, mode);
					if (err != 0)
						ret = false;
				}
				else
					ret = false;
			}
			if (ret)
			{
				pFile = pTempFile;
				if (wasOpened && !SetPosition(lastFilePos))
				{
					fclose(pTempFile);
					ret = false;
				}
				else
					wasOpened = true;
			}
		}
		return ret;
	}
	bool IsOpen()
	{
		return pFile != NULL;
	}
	bool Close()
	{
		bool ret;
		std::unique_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex, std::defer_lock);
		if (filePath != NULL)
			fileOperationLock.lock();
		if (IsOpen())
		{
			if (flags & RWOpenFlags_Unclosable)
				ret = false;
			else if (!Tell(lastFilePos))
				ret = false;
			else
			{
				fclose(pFile);
				pFile = NULL;
				ret = true;
			}
		}
		else
			ret = true;
		return ret;
	}

	AssetsRWTypes GetType() { return AssetsRWType_Reader; }
	AssetsRWClasses GetClass() { return AssetsRWClass_ReaderFromFile; }
	bool IsView() { return false; }

	bool Tell(QWORD &pos)
	{
		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		bool ret;
		if (IsOpen())
		{
			long long posv = _ftelli64(pFile);
			if (posv < 0)
			{
				pos = 0;
				ret = false;
			}
			else
			{
				pos = (QWORD)posv;
				ret = true;
			}
		}
		else
		{
			pos = lastFilePos;
			ret = true;
		}
		return ret;
	}
	bool Seek(AssetsSeekTypes origin, long long offset)
	{
		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		bool ret;
		if (!IsOpen() && !Reopen())
			ret = false;
		else
			ret = _fseeki64(pFile, offset, (int)origin) == 0;
		return ret;
	}
	bool SetPosition(QWORD pos)
	{
		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		bool ret;
		if (((long long)pos) < 0) //fpos_t is signed
			ret = false;
		else if (!IsOpen() && !Reopen())
			ret = false;
		else
			ret = _fseeki64(pFile, (long long)pos, SEEK_SET) == 0;
		return ret;
	}

	QWORD Read(QWORD pos, QWORD size, void *outBuffer, bool nullUnread)
	{
		std::unique_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		QWORD ret;
		if (!IsOpen() && !Reopen())
			ret = 0;
		else if ((pos != (QWORD)-1) && !SetPosition(pos))
			ret = 0;
		else
			ret = fread(outBuffer, 1, size, pFile);
		fileOperationLock.unlock();
		if (nullUnread && (ret < size))
			memset(&((uint8_t*)outBuffer)[ret], 0, size - ret);
		return ret;
	}
	
	IAssetsReader *CreateView();
	friend class AssetsReaderFromFile_View;
};
class AssetsReaderFromFile_View : public IAssetsReader
{
	AssetsReaderFromFile *pBaseReader;
	QWORD filePos;
public:
	AssetsReaderFromFile_View(AssetsReaderFromFile *pBaseReader)
		: pBaseReader(pBaseReader), filePos(0)
	{}
	
	~AssetsReaderFromFile_View(){}

	bool Reopen() { return pBaseReader->Reopen(); }
	bool IsOpen() { return pBaseReader->IsOpen();}
	bool Close() { return false; }

	AssetsRWTypes GetType() { return AssetsRWType_Reader; }
	AssetsRWClasses GetClass() { return AssetsRWClass_ReaderFromFile; }
	bool IsView() { return true; }

	bool Tell(QWORD &pos) { pos = filePos; return true; }
	bool Seek(AssetsSeekTypes origin, long long offset)
	{
		bool ret = false;
		QWORD newPos = filePos;
		switch (origin)
		{
		case AssetsSeek_Begin:
			if (offset < 0) return false;
			newPos = offset;
			//Seek the base reader to determine whether the operation is valid.
			ret = pBaseReader->Seek(AssetsSeek_Begin, offset);
			break;
		case AssetsSeek_Cur:
			if (offset < 0)
			{
				offset = -offset;
				if (offset < 0) return false; //INT64_MIN
				if ((unsigned long long)offset > newPos) return false;
				newPos -= (unsigned long long)offset;
			}
			else
				newPos += offset;
			//Seek the base reader to determine whether the operation is valid.
			ret = pBaseReader->Seek(AssetsSeek_Begin, newPos);
			break;
		case AssetsSeek_End:
			if (offset > 0) return false;
			std::scoped_lock<std::recursive_mutex> fileOperationLock(pBaseReader->fileOperationMutex);
			ret = pBaseReader->Seek(AssetsSeek_End, offset)
				&& pBaseReader->Tell(newPos);
			break;
		}
		if (ret)
			filePos = newPos;
		return ret;
	}
	bool SetPosition(QWORD pos)
	{
		//Seek the base reader to determine whether the operation is valid.
		if (pBaseReader->SetPosition(pos))
		{
			filePos = pos;
			return true;
		}
		return false;
	}
	
	QWORD Read(QWORD pos, QWORD size, void *outBuffer, bool nullUnread)
	{
		if (pos == (QWORD)-1)
			pos = filePos;
		else
			filePos = pos;
		QWORD result = pBaseReader->Read(pos, size, outBuffer, nullUnread);
		filePos += result;
		return result;
	}
	
	IAssetsReader *CreateView() { return pBaseReader->CreateView(); }
};
IAssetsReader *AssetsReaderFromFile::CreateView()
{
	return new AssetsReaderFromFile_View(this);
}

IAssetsReader *Create_AssetsReaderFromFile(const wchar_t *filePath, bool binary, AssetsRWOpenFlags openFlags)
{
	if (filePath == NULL) return NULL;

	AssetsReaderFromFile *pReader = new AssetsReaderFromFile(filePath, binary, openFlags);

	if (openFlags & RWOpenFlags_Immediately)
	{
		if (!pReader->Reopen())
		{
			delete pReader;
			return NULL;
		}
	}

	AddReopenable(pReader);
	return pReader;
}
IAssetsReader *Create_AssetsReaderFromFile(const char *filePath, bool binary, AssetsRWOpenFlags openFlags)
{
	if (filePath == NULL) return NULL;

	size_t tcLen = 0;
	TCHAR *tcPath = _MultiByteToWide(filePath, tcLen);

	IAssetsReader *pReader = Create_AssetsReaderFromFile(tcPath, binary, openFlags);

	_FreeWCHAR(tcPath);

	return pReader;
}
IAssetsReader *Create_AssetsReaderFromFile(FILE *pFile)
{
	if (pFile == NULL) return NULL;

	IAssetsReader *pReader = new AssetsReaderFromFile(pFile);

	return pReader;
}


class AssetsReaderFromSplitFile : public AssetsReaderFromFile
{
	AssetsRWOpenFlags flags;
protected:
	struct FileSizeEntry
	{
		QWORD absolutePos;
		QWORD size;
		FILE *pFile;
	};
	std::vector<FileSizeEntry> splitSizes;
	QWORD totalFileSize;

	//Used for fast seek while the file is opened.
	size_t curSplitIndex; QWORD curSplitPos;
	//Used to find the absolute file position when reopening.
	QWORD absoluteSplitFilePos;

	bool allowUpdate;

	bool findSplitFile(QWORD absolutePos, size_t &splitFileIndex, QWORD &splitFileOffset)
	{
		if (curSplitIndex < splitSizes.size()
			&& splitSizes[curSplitIndex].absolutePos <= absolutePos
			&& splitSizes[curSplitIndex].absolutePos + splitSizes[curSplitIndex].size > absolutePos)
		{
			splitFileIndex = curSplitIndex;
			splitFileOffset = absolutePos - splitSizes[curSplitIndex].absolutePos;
			return true;
		}
		splitFileIndex = splitSizes.size() / 2;
		//Binary search.
		size_t left = 0, right = splitSizes.size();
		while (true)
		{
			QWORD startPos = splitSizes[splitFileIndex].absolutePos;
			QWORD endPos = startPos + splitSizes[splitFileIndex].size;
			if (endPos < startPos) //Overflow
			{
				return false;
			}
			if (startPos > absolutePos)
				right = splitFileIndex;
			else if (endPos <= absolutePos)
				left = splitFileIndex + 1;
			else
				break;
			if (left == right) //Did not find the correct split
			{
				return false;
			}
			splitFileIndex = left + (right - left) / 2;
		}
		splitFileOffset = absolutePos - splitSizes[splitFileIndex].absolutePos;
		return true;
	}
public:
	AssetsReaderFromSplitFile(const TCHAR *filePath, bool binary, bool allowUpdate, AssetsRWOpenFlags flags)
		: AssetsReaderFromFile(filePath, binary, flags & (~RWOpenFlags_Unclosable))
	{
		this->allowUpdate = allowUpdate;
		this->flags = flags;
		this->totalFileSize = 0;
		this->curSplitIndex = 0;
		this->curSplitPos = 0;
		this->absoluteSplitFilePos = 0;
	}
	~AssetsReaderFromSplitFile()
	{
		Close();
	}
	bool Reopen()
	{
		bool ret = true;
		bool wasOpened = this->wasOpened;
		size_t filePathLen = 0;

		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);

		if (IsOpen())
			ret = true;
		else if (filePath == NULL || (filePathLen = _tcslen(filePath)) == 0)
			ret = false;
		else if (filePathLen < 5 || _tcscmp(&filePath[filePathLen - 4], _T("0000")))
			ret = false;
		else
		{
			filePath[filePathLen - 3] = 0; //the actual first name is .split0, the four additional nulls are just reserved space.
			if (!AssetsReaderFromFile::Reopen())
				ret = false;
			else if (!wasOpened || allowUpdate)
			{
				splitSizes.clear(); totalFileSize = 0;
				QWORD oldBasePos = 0;
				AssetsReaderFromFile::Tell(oldBasePos);
				if (!AssetsReaderFromFile::Seek(AssetsSeek_End, 0) || !AssetsReaderFromFile::Tell(totalFileSize))
				{
					ret = false;
					AssetsReaderFromFile::Close();
				}
				else
				{
					AssetsReaderFromFile::SetPosition(oldBasePos);
					FileSizeEntry entry = {0, totalFileSize, this->pFile};
					splitSizes.push_back(entry);
				}
			}
			if (ret && splitSizes.size() > 0)
			{
				int curOpenSplitIndex = 0;
				while ((++curOpenSplitIndex) < 10000)
				{
					if ((wasOpened && !allowUpdate) && curOpenSplitIndex >= splitSizes.size())
						break;
					FILE *pTempFile = NULL;

					_stprintf_p(&filePath[filePathLen - 4], 5, _T("%d"), curOpenSplitIndex);
					filePath[filePathLen] = 0;

					TCHAR mode[3] = {_T('r'), binary ? _T('b') : 0, 0};

					errno_t err = _tfopen_s(&pTempFile, filePath, mode);
					if (err != 0)
					{
						pTempFile = NULL;
						if (err == ENFILE || err == EMFILE)
						{
							GarbageCollectReopenables(128);
							err = _tfopen_s(&pTempFile, filePath, mode);
							if (err != 0)
								pTempFile = NULL;
						}
					}
					if (pTempFile != NULL)
					{
						if (fseek(pTempFile, 0, SEEK_END) == 0)
						{
							long long tempSize = _ftelli64(pTempFile);
							if (tempSize > 0)
							{
								fseek(pTempFile, 0, SEEK_SET);
								if (!wasOpened || allowUpdate)
								{
									size_t lastIndex = splitSizes.size() - 1;
									FileSizeEntry entry = {splitSizes[lastIndex].absolutePos + splitSizes[lastIndex].size, (QWORD)tempSize, pTempFile};
									totalFileSize = entry.absolutePos + entry.size;
									splitSizes.push_back(entry);
								}
								else if ((QWORD)tempSize != splitSizes[curOpenSplitIndex].size)
								{
									fclose(pTempFile);
									pTempFile = NULL;
								}
							}
							else
							{
								fclose(pTempFile);
								pTempFile = NULL;
							}
						}
						else
						{
							fclose(pTempFile);
							pTempFile = NULL;
						}
					}
					if (pTempFile == NULL)
					{
						if (wasOpened && !allowUpdate)
						{
							//Split file not fully openable anymore; failed reopening
							AssetsReaderFromFile::Close();
							splitSizes[0].pFile = NULL;
							for (size_t i = 1; i < splitSizes.size(); i++)
							{
								if (splitSizes[i].pFile != NULL)
								{
									fclose(splitSizes[i].pFile);
									splitSizes[i].pFile = NULL;
								}
							}
							ret = false;
						}
						break;
					}
					else
						splitSizes[curOpenSplitIndex].pFile = pTempFile;
				}
			}
			_tcscpy(&filePath[filePathLen - 4], _T("0000"));
			if (wasOpened)
			{
				if (splitSizes.size() > 0)
				{
					//Go to the previous position.
					if (!findSplitFile(absoluteSplitFilePos, curSplitIndex, curSplitPos)
						|| (_fseeki64(splitSizes[curSplitIndex].pFile, (long long)curSplitPos, SEEK_SET) != 0))
					{
						Close();
						ret = false;
					}
				}
				else
				{
					Close();
					ret = false;
				}
			}
		}
		return ret;
	}
	bool IsOpen()
	{
		return AssetsReaderFromFile::IsOpen();
	}
	bool Close()
	{
		bool ret;
		std::unique_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex, std::defer_lock);
		if (filePath != NULL)
			fileOperationLock.lock();
		if (IsOpen())
		{
			if (flags & RWOpenFlags_Unclosable)
				ret = false;
			else if (!Tell(absoluteSplitFilePos))
				ret = false;
			else if (splitSizes.size() == 0)
				ret = false;
			else if (!AssetsReaderFromFile::Close())
				ret = false;
			else
			{
				this->lastFilePos = 0;
				splitSizes[0].pFile = NULL;
				for (size_t i = 1; i < splitSizes.size(); i++)
				{
					if (splitSizes[i].pFile != NULL)
					{
						fclose(splitSizes[i].pFile);
						splitSizes[i].pFile = NULL;
					}
				}
				ret = true;
			}
		}
		else
			ret = true;
		return ret;
	}

	AssetsRWTypes GetType() { return AssetsRWType_Reader; }
	AssetsRWClasses GetClass() { return AssetsRWClass_ReaderFromSplitFile; }
	bool IsView() { return false; }

	bool Tell(QWORD &pos)
	{
		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		bool ret;
		if (IsOpen())
		{
			pos = 0;
			if (splitSizes.size() <= curSplitIndex)
			{
				ret = false;
			}
			else
			{
				pos = splitSizes[curSplitIndex].absolutePos + curSplitPos;
				ret = true;
			}
		}
		else
		{
			pos = absoluteSplitFilePos;
			ret = true;
		}
		return ret;
	}
	bool Seek(AssetsSeekTypes origin, long long offset)
	{
		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		bool ret;
		if (offset < 0)
			ret = false;
		else if (!IsOpen() && !Reopen())
			ret = false;
		else
		{
			switch (origin)
			{
				case AssetsSeek_Begin:
					{
						if (offset < 0)
							ret = false;
						else
							ret = SetPosition((QWORD)offset);
						break;
					}
				case AssetsSeek_End:
					{
						if (offset > 0)
							ret = false;
						else
						{
							long long tempAbsOffset = -offset;
							if (tempAbsOffset < 0) //if offset is the minimum signed long long
								ret = false;
							else
							{
								if (splitSizes.size() > 0)
								{
									QWORD endPos = splitSizes[splitSizes.size() - 1].absolutePos + splitSizes[splitSizes.size() - 1].size;
									if ((QWORD)tempAbsOffset > endPos)
										ret = false;
									else
									{
										ret = SetPosition(endPos - (QWORD)tempAbsOffset);
									}
								}
								else if (tempAbsOffset > 0)
									ret = false;
							}
						}
						break;
					}
				case AssetsSeek_Cur:
					{
						if (offset == 0)
						{
							ret = true;
						}
						else if (curSplitIndex >= splitSizes.size())
						{
							ret = false;
						}
						else if (offset > 0)
						{
							QWORD curAbsolutePos = splitSizes[curSplitIndex].absolutePos + curSplitPos;
							QWORD targetPos = curAbsolutePos + offset;
							if (targetPos < curAbsolutePos) //Overflow
								ret = false;
							else
							{
								ret = SetPosition(targetPos);
							}
						}
						else //if (offset < 0)
						{
							long long tempRemainingOffset = -offset;
							if (tempRemainingOffset < 0) //if offset is the minimum signed long long
								ret = false;
							else
							{
								QWORD curAbsolutePos = splitSizes[curSplitIndex].absolutePos + curSplitPos;
								if ((QWORD)tempRemainingOffset > curAbsolutePos)
									ret = false;
								else
								{
									ret = SetPosition(curAbsolutePos - (QWORD)tempRemainingOffset);
								}
							}
						}
						break;
					}
			}
		}
		return ret;
	}
	bool SetPosition(QWORD pos)
	{
		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		bool ret;
		if (((long long)pos) < 0) //_fseeki64 uses a signed offset
			ret = false;
		else if (!IsOpen() && !Reopen())
			ret = false;
		else
		{
			if (findSplitFile(pos, curSplitIndex, curSplitPos)
				&& (_fseeki64(splitSizes[curSplitIndex].pFile, (long long)curSplitPos, SEEK_SET) == 0))
				ret = true;
			else
				ret = false;
		}
		return ret;
	}

	QWORD Read(QWORD pos, QWORD size, void *outBuffer, bool nullUnread)
	{
		std::unique_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		QWORD ret;
		if (!IsOpen() && !Reopen())
			ret = 0;
		else if ((pos != (QWORD)-1) && !SetPosition(pos))
			ret = 0;
		else if ((curSplitIndex >= splitSizes.size()) || (curSplitPos > splitSizes[curSplitIndex].size))
			ret = 0;
		else
		{
			QWORD remainingSize = size;
			while (remainingSize > 0)
			{
				QWORD curReadSize = splitSizes[curSplitIndex].size - curSplitPos;
				if (curReadSize > remainingSize) curReadSize = remainingSize;
				if (curReadSize > 0)
				{
					if (splitSizes[curSplitIndex].pFile == NULL)
						break;
					QWORD read = fread(&((uint8_t*)outBuffer)[size - remainingSize], 1, curReadSize, splitSizes[curSplitIndex].pFile);
					curSplitPos += read;
					remainingSize -= read;
					if (read < curReadSize) //curSplitPos is also allowed to be equal to the current split size.
						break;
				}
				if (curSplitPos >= splitSizes[curSplitIndex].size)
				{
					if (splitSizes.size() > (curSplitIndex + 1))
					{
						curSplitIndex++;
						curSplitPos = 0;
						if (splitSizes[curSplitIndex].pFile) fseek(splitSizes[curSplitIndex].pFile, 0, SEEK_SET);
					}
					else
						break;
				}
			}
			ret = size - remainingSize;
		}
		fileOperationLock.unlock();
		if (nullUnread && (ret < size))
			memset(&((uint8_t*)outBuffer)[ret], 0, size - ret);
		return ret;
	}
	IAssetsReader *CreateView();
	friend class AssetsReaderFromSplitFile_View;
};
class AssetsReaderFromSplitFile_View : public IAssetsReader
{
	AssetsReaderFromSplitFile *pBaseReader;
	QWORD filePos;
	size_t splitFileIndex;
	QWORD splitFilePos;
public:
	AssetsReaderFromSplitFile_View(AssetsReaderFromSplitFile *pBaseReader)
		: pBaseReader(pBaseReader), filePos(0), splitFileIndex(0), splitFilePos(0)
	{}
	
	~AssetsReaderFromSplitFile_View(){}

	bool Reopen() { return pBaseReader->SetPosition(filePos); }
	bool IsOpen() { return pBaseReader->IsOpen(); }
	bool Close() { return false; }

	AssetsRWTypes GetType() { return AssetsRWType_Reader; }
	AssetsRWClasses GetClass() { return AssetsRWClass_ReaderFromSplitFile; }
	bool IsView() { return true; }

	bool Tell(QWORD &pos) { pos = filePos; return true; }
	bool Seek(AssetsSeekTypes origin, long long offset)
	{
		bool ret = false;
		QWORD newPos = filePos;
		switch (origin)
		{
		case AssetsSeek_Begin:
			if (offset < 0) return false;
			newPos = offset;
			break;
		case AssetsSeek_Cur:
			if (offset < 0)
			{
				offset = -offset;
				if (offset < 0) return false; //INT64_MIN
				if ((unsigned long long)offset > newPos) return false;
				newPos -= (unsigned long long)offset;
			}
			else
				newPos += (unsigned long long)offset;
			break;
		case AssetsSeek_End:
			if (offset > 0) return false;
			offset = -offset;
			if (offset < 0) return false; //INT64_MIN
			if ((unsigned long long)offset > pBaseReader->totalFileSize) return false;
			newPos = pBaseReader->totalFileSize - (unsigned long long)offset;
			break;
		}
		std::scoped_lock<std::recursive_mutex> fileOperationLock(pBaseReader->fileOperationMutex);
		ret = pBaseReader->Seek(AssetsSeek_Begin, newPos);
		size_t newSplitIndex = pBaseReader->curSplitIndex;
		QWORD newSplitPos = pBaseReader->curSplitPos;
		if (ret)
		{
			filePos = newPos;
			splitFileIndex = newSplitIndex;
			splitFilePos = newSplitPos;
		}
		return ret;
	}
	bool SetPosition(QWORD pos)
	{
		std::scoped_lock<std::recursive_mutex> fileOperationLock(pBaseReader->fileOperationMutex);
		bool ret = pBaseReader->SetPosition(pos);
		if (ret)
		{
			filePos = pos;
			splitFileIndex = pBaseReader->curSplitIndex;
			splitFilePos = pBaseReader->curSplitPos;
		}
		return ret;
	}
	
	QWORD Read(QWORD pos, QWORD size, void *outBuffer, bool nullUnread)
	{
		std::scoped_lock<std::recursive_mutex> fileOperationLock(pBaseReader->fileOperationMutex);
		if (pos == (QWORD)-1)
		{
			pos = filePos;
			//Give the base reader a hint where the data is located.
			pBaseReader->curSplitIndex = splitFileIndex;
			pBaseReader->curSplitPos = splitFilePos;
		}
		else
			filePos = pos;
		QWORD result = pBaseReader->Read(pos, size, outBuffer, nullUnread);
		splitFileIndex = pBaseReader->curSplitIndex;
		splitFilePos = pBaseReader->curSplitPos;
		filePos += result;
		return result;
	}
	
	IAssetsReader *CreateView() { return pBaseReader->CreateView(); }
};
IAssetsReader *AssetsReaderFromSplitFile::CreateView()
{
	return new AssetsReaderFromSplitFile_View(this);
}

IAssetsReader *Create_AssetsReaderFromSplitFile(const wchar_t *filePath, bool binary, bool allowUpdate, AssetsRWOpenFlags openFlags)
{
	if (filePath == NULL) return NULL;
	size_t filePathLen = wcslen(filePath);
	if ((filePathLen < 7) || wcscmp(&filePath[filePathLen - 7], L".split0"))
		return NULL;

	std::wstring tempFilePath = std::wstring(filePath) + L"000";

	AssetsReaderFromSplitFile *pReader = new AssetsReaderFromSplitFile(tempFilePath.c_str(), binary, allowUpdate, openFlags);

	if (openFlags & RWOpenFlags_Immediately)
	{
		if (!pReader->Reopen())
		{
			delete pReader;
			return NULL;
		}
	}

	AddReopenable(pReader);
	return pReader;
}
IAssetsReader *Create_AssetsReaderFromSplitFile(const char *filePath, bool binary, bool allowUpdate, AssetsRWOpenFlags openFlags)
{
	if (filePath == NULL) return NULL;

	size_t tcLen = 0;
	TCHAR *tcPath = _MultiByteToWide(filePath, tcLen);

	IAssetsReader *pReader = Create_AssetsReaderFromSplitFile(tcPath, binary, allowUpdate, openFlags);

	_FreeWCHAR(tcPath);

	return pReader;
}

class AssetsReaderFromMemory : public IAssetsReader
{
protected:
	const void *buf;
	size_t bufLen;
	cbFreeMemoryResource freeBufCallback;

	void *ownBuf; //null if copyBuf was passed as false

	size_t readerPos;
public:
	AssetsReaderFromMemory(const void *buf, size_t bufLen, bool copyBuf, cbFreeMemoryResource freeBufCallback)
		: freeBufCallback(freeBufCallback)
	{
		this->readerPos = 0;
		if (copyBuf)
		{
			this->buf = this->ownBuf = malloc(bufLen);
			if (this->ownBuf == nullptr)
				this->bufLen = 0;
			else
			{
				memcpy(this->ownBuf, buf, bufLen);
				this->bufLen = bufLen;
			}
		}
		else
		{
			ownBuf = nullptr;
			this->buf = buf;
			this->bufLen = bufLen;
		}
	}
	~AssetsReaderFromMemory()
	{
		if (ownBuf != nullptr)
		{
			free(ownBuf);
			ownBuf = nullptr;
		}
		else if (buf != nullptr)
		{
			if (freeBufCallback)
				freeBufCallback(const_cast<void*>(buf));
		}
		buf = nullptr; bufLen = 0; readerPos = 0;
	}
	bool Reopen()
	{
		return true;
	}
	bool IsOpen()
	{
		return true;
	}
	bool Close()
	{
		return false;
	}

	AssetsRWTypes GetType() { return AssetsRWType_Reader; }
	AssetsRWClasses GetClass() { return AssetsRWClass_ReaderFromMemory; }
	bool IsView() { return false; }

	bool Tell(QWORD &pos)
	{
		pos = readerPos;
		return true;
	}
	bool Seek(AssetsSeekTypes origin, long long offset)
	{
		bool ret;
		switch (origin)
		{
			case AssetsSeek_Begin:
			{
				if (offset < 0)
					ret = false;
				else if ((unsigned long long)offset > this->bufLen)
					ret = false;
				else
				{
					this->readerPos = (size_t)offset;
					ret = true;
				}
				break;
			}
			case AssetsSeek_Cur:
			{
				if (offset < 0)
				{
					offset = -offset;
					if (offset < 0) //=> offset is the minimum signed long long
						ret = false;
					else if ((unsigned long long)offset > this->readerPos)
						ret = false;
					else
					{
						this->readerPos -= (unsigned long long)offset;
						ret = true;
					}
				}
				else if ((unsigned long long)offset > (this->bufLen - this->readerPos))
					ret = false;
				else
				{
					this->readerPos += (unsigned long long)offset;
					ret = true;
				}
				break;
			}
			case AssetsSeek_End:
			{
				if (offset > 0)
					ret = false;
				else
				{
					offset = -offset;
					if (offset < 0) //=> offset is the minimum signed long long
						ret = false;
					else if ((unsigned long long)offset > this->bufLen)
						ret = false;
					else
					{
						this->readerPos = this->bufLen - (unsigned long long)offset;
						ret = true;
					}
				}
				break;
			}
			default: ret = false;
		}
		return ret;
	}
	bool SetPosition(QWORD pos)
	{
		bool ret;
		if (pos > this->bufLen)
			ret = false;
		else
		{
			this->readerPos = pos;
			ret = true;
		}
		return ret;
	}

	QWORD Read(QWORD pos, QWORD size, void *outBuffer, bool nullUnread)
	{
		QWORD ret;
		if ((pos != (QWORD)-1) && !SetPosition(pos))
			ret = 0;
		else
		{
			QWORD actualSize = size;
			if (actualSize > (this->bufLen - this->readerPos))
			{
				actualSize = this->bufLen - this->readerPos;
			}
			memcpy(outBuffer, &((uint8_t*)this->buf)[this->readerPos], (size_t)actualSize);
			ret = actualSize;
			this->readerPos += ret;
		}
		if (nullUnread && (ret < size))
			memset(&((uint8_t*)outBuffer)[ret], 0, size - ret);
		return ret;
	}

	bool HasBuffer()
	{
		return buf != NULL || bufLen == 0;
	}

	IAssetsReader *CreateView();
	friend class AssetsReaderFromMemory_View;
};
class AssetsReaderFromMemory_View : public IAssetsReader
{
	AssetsReaderFromMemory *pBaseReader;
	size_t pos;
public:
	AssetsReaderFromMemory_View(AssetsReaderFromMemory *pBaseReader)
		: pBaseReader(pBaseReader), pos(0)
	{}
	
	~AssetsReaderFromMemory_View(){}

	bool Reopen() { return true; }
	bool IsOpen() { return true; }
	bool Close() { return false; }

	AssetsRWTypes GetType() { return AssetsRWType_Reader; }
	AssetsRWClasses GetClass() { return AssetsRWClass_ReaderFromMemory; }
	bool IsView() { return true; }

	bool Tell(QWORD &pos) { pos = this->pos; return true; }
	bool Seek(AssetsSeekTypes origin, long long offset)
	{
		bool ret = false;
		QWORD newPos = pos;
		switch (origin)
		{
		case AssetsSeek_Begin:
			if (offset < 0) return false;
			newPos = offset;
			break;
		case AssetsSeek_Cur:
			if (offset < 0)
			{
				offset = -offset;
				if (offset < 0) return false; //INT64_MIN
				if ((unsigned long long)offset > newPos) return false;
				newPos -= (unsigned long long)offset;
			}
			else
				newPos += offset;
			break;
		case AssetsSeek_End:
			if (offset > 0) return false;
			offset = -offset;
			if (offset < 0) return false; //INT64_MIN
			if ((unsigned long long)offset > pBaseReader->bufLen) return false;
			newPos = pBaseReader->bufLen - (unsigned long long)offset;
			break;
		}
		if (newPos <= (QWORD)pBaseReader->bufLen)
		{
			pos = newPos;
			return true;
		}
		return false;
	}
	bool SetPosition(QWORD pos)
	{
		if (pos <= (QWORD)pBaseReader->bufLen)
		{
			this->pos = pos;
			return true;
		}
		return false;
	}
	
	QWORD Read(QWORD pos, QWORD size, void *outBuffer, bool nullUnread)
	{
		QWORD result = pBaseReader->Read(pos, size, outBuffer, nullUnread);
		pos += result;
		return result;
	}
	
	IAssetsReader *CreateView() { return pBaseReader->CreateView(); }
};
IAssetsReader *AssetsReaderFromMemory::CreateView()
{
	return new AssetsReaderFromMemory_View(this);
}

IAssetsReader *Create_AssetsReaderFromMemory(const void *buf, size_t bufLen, bool copyBuf, cbFreeMemoryResource freeBufCallback)
{
	if (buf == NULL && bufLen > 0) return NULL;
	
	AssetsReaderFromMemory *pReader = new AssetsReaderFromMemory(buf, bufLen, copyBuf, freeBufCallback);
	if (copyBuf && !pReader->HasBuffer())
	{
		delete pReader;
		pReader = NULL;
	}
	//AssetsReaderFromMemory isn't garbage collectable
	//else
	//	AddReopenable(pReader);
	return pReader;
}

class AssetsReaderFromReaderRange : public IAssetsReaderFromReaderRange
{
protected:
	IAssetsReader *pChild;
	QWORD rangeStart; QWORD rangeSize;
	QWORD readerPos;
	bool doSeek; bool alwaysSeek;
public:
	AssetsReaderFromReaderRange(IAssetsReader *pChild, QWORD rangeStart, QWORD rangeSize, bool alwaysSeekChild)
	{
		this->pChild = pChild;
		this->rangeStart = rangeStart;
		this->rangeSize = rangeSize;
		this->alwaysSeek = alwaysSeekChild;
		this->readerPos = 0;
		this->doSeek = false;
	}
	~AssetsReaderFromReaderRange()
	{
		this->pChild = NULL;
		this->rangeStart = 0;
		this->rangeSize = 0;
		this->readerPos = 0;
	}
	bool Reopen()
	{
		bool ret = false;
		if (pChild != NULL)
			ret = pChild->Reopen();
		return ret;
	}
	bool IsOpen()
	{
		bool ret = false;
		if (pChild != NULL)
			ret = pChild->IsOpen();
		return ret;
	}
	bool Close()
	{
		bool ret = false;
		if (pChild != NULL)
		{
			ret = pChild->Close();
			if (ret)
				this->doSeek = true;
		}
		return ret;
	}

	AssetsRWTypes GetType() { return AssetsRWType_Reader; }
	AssetsRWClasses GetClass() { return AssetsRWClass_ReaderFromReaderRange; }
	bool IsView() { return false; }

	bool Tell(QWORD &pos)
	{
		pos = readerPos;
		return true;
	}
	bool Seek(AssetsSeekTypes origin, long long offset, QWORD &newPos)
	{
		bool ret;
		switch (origin)
		{
			case AssetsSeek_Begin:
			{
				if (offset < 0)
					ret = false;
				else if ((unsigned long long)offset > rangeSize)
					ret = false;
				else
				{
					this->readerPos = newPos = (size_t)offset;
					this->doSeek = true;
					ret = true;
				}
				break;
			}
			case AssetsSeek_Cur:
			{
				if (offset < 0)
				{
					offset = -offset;
					if (offset < 0) //=> offset is the minimum signed long long
						ret = false;
					else if ((unsigned long long)offset > this->readerPos)
						ret = false;
					else
					{
						this->readerPos -= (unsigned long long)offset;
						this->doSeek = true;
						ret = true;
					}
				}
				else if ((unsigned long long)offset > (this->rangeSize - this->readerPos))
					ret = false;
				else
				{
					this->readerPos += (unsigned long long)offset;
					this->doSeek = true;
					ret = true;
				}
				break;
			}
			case AssetsSeek_End:
			{
				if (offset > 0)
					ret = false;
				else
				{
					offset = -offset;
					if (offset < 0) //=> offset is the minimum signed long long
						ret = false;
					else if ((unsigned long long)offset > this->rangeSize)
						ret = false;
					else
					{
						this->readerPos = this->rangeSize - (unsigned long long)offset;
						this->doSeek = true;
						ret = true;
					}
				}
				break;
			}
			default: ret = false;
		}
		return ret;
	}
	bool Seek(AssetsSeekTypes origin, long long offset)
	{
		QWORD newPos;
		return Seek(origin, offset, newPos);
	}
	bool SetPosition(QWORD pos)
	{
		bool ret;
		if (pos > this->rangeSize)
			ret = false;
		else
		{
			this->readerPos = pos;
			this->doSeek = true;
			ret = true;
		}
		return ret;
	}

	QWORD Read(QWORD pos, QWORD size, void *outBuffer, bool nullUnread)
	{
		QWORD ret;
		if (pChild == NULL)
			ret = 0;
		else if ((pos != (QWORD)-1) && !SetPosition(pos))
			ret = 0;
		//else if ((doSeek || alwaysSeek) && !pChild->SetPosition(this->rangeStart + this->readerPos))
		//	ret = 0;
		else
		{
			if (pos == (QWORD)-1)
				pos = this->readerPos;
			QWORD actualSize = size;
			if (actualSize > (this->rangeSize - pos))
			{
				actualSize = this->rangeSize - pos;
			}
			QWORD readPos = (doSeek || alwaysSeek) ? (this->rangeStart + pos) : ((QWORD)-1);
			ret = pChild->Read(readPos, actualSize, outBuffer, false);
			this->readerPos = pos + ret;
			doSeek = false;
		}
		if (nullUnread && (ret < size))
			memset(&((uint8_t*)outBuffer)[ret], 0, size - ret);
		return ret;
	}

	bool GetChild(IAssetsReader *&pReader)
	{
		pReader = pChild;
		return (pChild != NULL);
	}
	IAssetsReader *CreateView()
	{
		//This reader type qualifies as a view already.
		alwaysSeek = true;
		return Create_AssetsReaderFromReaderRange(pChild, rangeStart, rangeSize, true);
	}
};

IAssetsReaderFromReaderRange *Create_AssetsReaderFromReaderRange(IAssetsReader *pChild, QWORD rangeStart, QWORD rangeSize, bool alwaysSeek)
{
	if (pChild == NULL) return NULL;
	
	AssetsReaderFromReaderRange *pReader = new AssetsReaderFromReaderRange(pChild, rangeStart, rangeSize, alwaysSeek);

	//AssetsReaderFromReaderRange isn't garbage collectable
	//AddReopenable(pReader);
	return pReader;
}

class AssetsWriterToFile : public IAssetsWriter
{
	AssetsRWOpenFlags flags;
protected:
	bool discard;
	bool binary;
	TCHAR *filePath;

	bool wasOpened;
	QWORD lastFilePos;

	FILE *pFile;
	std::recursive_mutex fileOperationMutex;
public:
	AssetsWriterToFile(const TCHAR *filePath, bool discard, bool binary, AssetsRWOpenFlags flags)
	{
		this->flags = flags;
		
		this->discard = discard;
		this->binary = binary;

		size_t pathLen = wcslen(filePath) + 1;
		this->filePath = new TCHAR[pathLen];
		memcpy(this->filePath, filePath, pathLen * sizeof(TCHAR));

		this->wasOpened = false;
		this->pFile = NULL;
		this->lastFilePos = 0;
	}
	AssetsWriterToFile(FILE *pFile)
	{
		this->pFile = pFile;

		this->flags = RWOpenFlags_Unclosable;
		
		this->discard = this->binary = true;
		this->filePath = NULL;
		this->wasOpened = true;
		this->lastFilePos = 0;
	}
	~AssetsWriterToFile()
	{
		RemoveReopenable(this);
		if (filePath)
		{
			free(filePath);
			filePath = NULL;
			flags = RWOpenFlags_None; //RWOpenFlags_Unclosable could otherwise prevent closing the file.
			Close();
		}
	}
	bool Reopen()
	{
		bool ret = true;
		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		if (IsOpen())
			ret = true;
		else if (filePath == NULL)
			ret = false;
		else
		{
			FILE *pTempFile = NULL;
			TCHAR mode[4] = {0,0,0,0};
			if (discard)
			{
				mode[0] = _T('w');
				mode[1] = binary ? _T('b') : 0;
				//Further reopens must not discard the contents again.
				//Without this, unpredictable behaviour would occur since a Close() call can be issued by garbage collection at any time.
				discard = false;
			}
			else
			{
				mode[0] = _T('r');
				mode[1] = _T('+');
				mode[2] = binary ? _T('b') : 0;
			}
			
			errno_t err = _tfopen_s(&pTempFile, filePath, mode);
			if (err != 0)
			{
				if (err == ENFILE || err == EMFILE)
				{
					GarbageCollectReopenables();
					err = _tfopen_s(&pTempFile, filePath, mode);
					if (err != 0)
						ret = false;
				}
				else
					ret = false;
			}
			if (ret)
			{
				pFile = pTempFile;
				if (wasOpened && !SetPosition(lastFilePos))
				{
					fclose(pTempFile);
					ret = false;
				}
				else
					wasOpened = true;
			}
		}
		return ret;
	}
	bool IsOpen()
	{
		return pFile != NULL;
	}
	bool Close()
	{
		bool ret;
		std::unique_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex, std::defer_lock);
		if (filePath != NULL)
			fileOperationLock.lock();
		if (IsOpen())
		{
			if (flags & RWOpenFlags_Unclosable)
				ret = false;
			else if (!Tell(lastFilePos))
				ret = false;
			else
			{
				fclose(pFile);
				pFile = NULL;
				ret = true;
			}
		}
		else
			ret = true;
		return ret;
	}

	AssetsRWTypes GetType() { return AssetsRWType_Writer; }
	AssetsRWClasses GetClass() { return AssetsRWClass_WriterToFile; }
	bool IsView() { return false; }

	bool Tell(QWORD &pos)
	{
		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		bool ret;
		if (IsOpen())
		{
			long long posv = _ftelli64(pFile);
			if (posv < 0)
			{
				pos = 0;
				ret = false;
			}
			else
			{
				pos = (QWORD)posv;
				ret = true;
			}
		}
		else
		{
			pos = lastFilePos;
			ret = true;
		}
		return ret;
	}
	bool Seek(AssetsSeekTypes origin, long long offset)
	{
		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		bool ret;
		if (!IsOpen() && !Reopen())
			ret = false;
		else
			ret = _fseeki64(pFile, offset, (int)origin) == 0;
		return ret;
	}
	bool SetPosition(QWORD pos)
	{
		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		bool ret;
		if (((long long)pos) < 0) //fpos_t is signed
			ret = false;
		else if (!IsOpen() && !Reopen())
			ret = false;
		else
			ret = _fseeki64(pFile, (long long)pos, SEEK_SET) == 0;
		return ret;
	}

	QWORD Write(QWORD pos, QWORD size, const void *inBuffer)
	{
		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		QWORD ret;
		if (!IsOpen() && !Reopen())
			ret = 0;
		else if ((pos != (QWORD)-1) && !SetPosition(pos))
			ret = 0;
		else
			ret = fwrite(inBuffer, 1, size, pFile);
		return ret;
	}

	bool Flush()
	{
		std::scoped_lock<std::recursive_mutex> fileOperationLock(fileOperationMutex);
		bool ret;
		if (!IsOpen())
			ret = true;
		else
			ret = fflush(pFile) == 0;
		return ret;
	}
};

IAssetsWriter *Create_AssetsWriterToFile(const wchar_t *filePath, bool discard, bool binary, AssetsRWOpenFlags openFlags)
{
	if (filePath == NULL) return NULL;

	AssetsWriterToFile *pWriter = new AssetsWriterToFile(filePath, discard, binary, openFlags);

	if (openFlags & RWOpenFlags_Immediately)
	{
		if (!pWriter->Reopen())
		{
			delete pWriter;
			return NULL;
		}
	}

	AddReopenable(pWriter);
	return pWriter;
}
IAssetsWriter *Create_AssetsWriterToFile(const char *filePath, bool discard, bool binary, AssetsRWOpenFlags openFlags)
{
	if (filePath == NULL) return NULL;

	size_t tcLen = 0;
	TCHAR *tcPath = _MultiByteToWide(filePath, tcLen);

	IAssetsWriter *pWriter = Create_AssetsWriterToFile(tcPath, discard, binary, openFlags);

	_FreeWCHAR(tcPath);

	return pWriter;
}
IAssetsWriter *Create_AssetsWriterToFile(FILE *pFile)
{
	if (pFile == NULL) return NULL;

	AssetsWriterToFile *pWriter = new AssetsWriterToFile(pFile);

	return pWriter;
}


class AssetsWriterToMemory : public IAssetsWriterToMemory
{
protected:
	void *data;
	size_t dataLen;
	size_t dataBufLen;

	void *ownBuf; //NULL if the caller passed a buffer through the constructor
	size_t ownBufMaxLen;

	size_t writerPos;

	bool isDynamic; bool freeOnClose;
public:
	AssetsWriterToMemory(void *buf, size_t bufLen, size_t initialDataLen)
	{
		this->data = buf;
		this->dataLen = initialDataLen;
		this->dataBufLen = bufLen;
		this->ownBuf = NULL;
		this->ownBufMaxLen = 0;
		this->writerPos = 0;
		this->isDynamic = false;
		this->freeOnClose = false;
		memset(&((uint8_t*)buf)[initialDataLen], 0, bufLen - initialDataLen);
	}
	AssetsWriterToMemory(size_t maximumLen, size_t initialLen)
	{
		this->ownBuf = initialLen ? malloc(initialLen) : 0;
		if (this->ownBuf == NULL)
			initialLen = 0;
		else
			memset(this->ownBuf, 0, initialLen);
		this->dataBufLen = initialLen;

		this->data = this->ownBuf;
		this->dataLen = 0;

		this->ownBufMaxLen = maximumLen;
		this->writerPos = 0;

		this->isDynamic = true;
		this->freeOnClose = true;
	}
	~AssetsWriterToMemory()
	{
		if (ownBuf != NULL)
		{
			if (freeOnClose)
				free(ownBuf);
			ownBuf = NULL;
		}
		dataBufLen = 0;
		data = NULL; dataLen = 0;
		writerPos = 0;
	}
	bool Reopen()
	{
		return true;
	}
	bool IsOpen()
	{
		return true;
	}
	bool Close()
	{
		return false;
	}


	AssetsRWTypes GetType() { return AssetsRWType_Writer; }
	AssetsRWClasses GetClass() { return AssetsRWClass_WriterToMemory; }
	bool IsView() { return false; }

	bool Tell(QWORD &pos)
	{
		pos = writerPos;
		return true;
	}
	bool Seek(AssetsSeekTypes origin, long long offset)
	{
		bool ret;
		switch (origin)
		{
			case AssetsSeek_Begin:
			{
				if (offset < 0)
					ret = false;
				else 
				{
					if ((unsigned long long)offset > this->dataBufLen)
						Resize((size_t)std::min<unsigned long long>(offset, SIZE_MAX));
					if ((unsigned long long)offset > this->dataBufLen)
						ret = false;
					else
					{
						if ((unsigned long long)offset > this->dataLen)
							this->dataLen = (size_t)offset;
						this->writerPos = (size_t)offset;
						ret = true;
					}
				}
				break;
			}
			case AssetsSeek_Cur:
			{
				if (offset < 0)
				{
					offset = -offset;
					if (offset < 0) //=> offset is the minimum signed long long
						ret = false;
					else if ((unsigned long long)offset > this->writerPos)
						ret = false;
					else
					{
						this->writerPos -= (unsigned long long)offset;
						ret = true;
					}
				}
				else
				{
					unsigned long long _offset = (unsigned long long)offset + this->writerPos;
					if (_offset > this->dataBufLen)
						Resize(std::min<unsigned long long>(_offset, SIZE_MAX));
					if (_offset > this->dataBufLen)
						ret = false;
					else
					{
						if (_offset > this->dataLen)
							this->dataLen = (size_t)_offset;
						this->writerPos = (size_t)_offset;
						ret = true;
					}
				}
				break;
			}
			case AssetsSeek_End:
			{
				if (offset > 0)
				{
					unsigned long long _offset = offset + this->dataLen;
					if (_offset > this->dataBufLen)
						Resize((size_t)std::min<unsigned long long>(_offset, SIZE_MAX));
					if (_offset > this->dataBufLen)
						ret = false;
					else
					{
						if (_offset > this->dataLen)
							this->dataLen = (size_t)offset;
						this->writerPos = (size_t)offset;
						ret = true;
					}
				}
				else
				{
					offset = -offset;
					if (offset < 0) //=> offset is the minimum signed long long
						ret = false;
					else if ((unsigned long long)offset > this->dataLen)
						ret = false;
					else
					{
						this->writerPos = this->dataLen - (unsigned long long)offset;
						ret = true;
					}
				}
				break;
			}
			default: ret = false;
		}
		return ret;
	}
	bool SetPosition(QWORD pos)
	{
		bool ret;
		if (pos > this->dataBufLen)
			Resize(pos);
		if (pos > this->dataBufLen)
			ret = false;
		else
		{
			if (pos > this->dataLen)
				this->dataLen = (size_t)pos;
			this->writerPos = pos;
			ret = true;
		}
		return ret;
	}

	QWORD Write(QWORD pos, QWORD size, const void *inBuffer)
	{
		QWORD ret;
		if ((pos != (QWORD)-1) && !SetPosition(pos))
			ret = 0;
		else
		{
			if (this->writerPos > this->dataBufLen)
				return 0;
			QWORD actualSize = size;
			if (actualSize > (this->dataBufLen - this->writerPos))
			{
				if (isDynamic && !Resize(this->writerPos + actualSize))
					return 0;
				if (actualSize > (this->dataBufLen - this->writerPos))
					actualSize = this->dataBufLen - this->writerPos;
			}
			memcpy(&((uint8_t*)data)[writerPos], inBuffer, (size_t)actualSize);
			ret = actualSize;
			this->writerPos += ret;
			if (this->writerPos > this->dataLen)
				this->dataLen = this->writerPos;
		}
		return ret;
	}

	bool Flush()
	{
		return true;
	}

	bool GetBuffer(void *&buffer, size_t &dataSize)
	{
		buffer = data;
		dataSize = dataLen;
		return true;
	}

	bool IsDynamicBuffer()
	{
		return isDynamic;
	}

	bool SetFreeBuffer(bool doFree)
	{
		if (!isDynamic && doFree)
			return false;
		this->freeOnClose = doFree;
		return true;
	}

	bool Resize(size_t targetLen)
	{
		if (!isDynamic)
			return false;

		targetLen = (targetLen + 2047) & ~(2047);
		if (targetLen > ownBufMaxLen)
			targetLen = ownBufMaxLen;
		
		void *newBuf = realloc(ownBuf, targetLen);
		if (newBuf == NULL)
			return false;

		ownBuf = newBuf;
		data = newBuf;
		if (targetLen > dataBufLen)
			memset(&((uint8_t*)newBuf)[dataBufLen], 0, targetLen - dataBufLen);
		dataBufLen = targetLen;
		if (dataLen > targetLen)
			dataLen = targetLen;
		return true;
	}
};

IAssetsWriterToMemory *Create_AssetsWriterToMemory(void *buf, size_t bufLen, size_t initialDataLen)
{
	if (buf == NULL && bufLen > 0) return NULL;
	if (initialDataLen > bufLen) return NULL;

	AssetsWriterToMemory *pWriter = new AssetsWriterToMemory(buf, bufLen, initialDataLen);

	//AssetsWriterToMemory isn't garbage collectable
	//AddReopenable(pWriter);
	return pWriter;
}
IAssetsWriterToMemory *Create_AssetsWriterToMemory(size_t initialLen, size_t maximumLen)
{
	if (initialLen > maximumLen) return NULL;

	AssetsWriterToMemory *pWriter = new AssetsWriterToMemory(maximumLen, initialLen);

	//AssetsWriterToMemory isn't garbage collectable
	//AddReopenable(pWriter);
	return pWriter;
}
void Free_AssetsWriterToMemory_DynBuf(void *buffer)
{
	if (buffer)
		free(buffer);
}

class AssetsWriterToWriterOffset : public IAssetsWriterToWriterOffset
{
protected:
	IAssetsWriter *pChild;
	QWORD childOffset;
	QWORD writerPos;
	bool doSeek; bool alwaysSeek;
public:
	AssetsWriterToWriterOffset(IAssetsWriter *pChild, QWORD offset, bool alwaysSeekChild)
	{
		this->pChild = pChild;
		this->childOffset = offset;
		this->alwaysSeek = alwaysSeekChild;
		this->writerPos = 0;
		this->doSeek = false;
	}
	~AssetsWriterToWriterOffset()
	{
		this->pChild = NULL;
		this->childOffset = 0;
		this->writerPos = 0;
	}
	bool Reopen()
	{
		bool ret = false;
		if (pChild != NULL)
			ret = pChild->Reopen();
		return ret;
	}
	bool IsOpen()
	{
		bool ret = false;
		if (pChild != NULL)
			ret = pChild->IsOpen();
		return ret;
	}
	bool Close()
	{
		bool ret = false;
		if (pChild != NULL)
		{
			ret = pChild->Close();
			if (ret)
				this->doSeek = true;
		}
		return ret;
	}

	AssetsRWTypes GetType() { return AssetsRWType_Writer; }
	AssetsRWClasses GetClass() { return AssetsRWClass_WriterToWriterOffset; }
	bool IsView() { return false; }

	bool Tell(QWORD &pos)
	{
		pos = writerPos;
		return true;
	}
	bool Seek(AssetsSeekTypes origin, long long offset)
	{
		bool ret;
		if (pChild == NULL)
			ret = false;
		else
		{
			switch (origin)
			{
				case AssetsSeek_Begin:
				{
					if (offset < 0)
						ret = false;
					else
					{
						if (ret = pChild->Seek(AssetsSeek_Begin, this->childOffset + offset))
						{
							QWORD tempPos = this->childOffset + (size_t)offset;
							if (pChild->Tell(tempPos) && ((tempPos - this->childOffset) != (size_t)offset))
							{
								this->doSeek = true;
								ret = false;
							}
							else
							{
								this->writerPos = (size_t)offset;
								this->doSeek = false;
								ret = true;
							}
						}
						else
							ret = false;
					}
					break;
				}
				case AssetsSeek_Cur:
				{
					if (offset < 0)
					{
						offset = -offset;
						if (offset < 0) //=> offset is the minimum signed long long
							ret = false;
						else if ((unsigned long long)offset > this->writerPos)
							ret = false;
						else
						{
							this->writerPos -= (unsigned long long)offset;
							this->doSeek = true;
							ret = true;
						}
					}
					else
					{
						if (ret = pChild->Seek(AssetsSeek_Cur, offset))
						{
							QWORD tempPos = this->childOffset + (unsigned long long)offset;
							if (pChild->Tell(tempPos) && ((tempPos - this->childOffset) != this->writerPos))
							{
								this->doSeek = true;
								ret = false;
							}
							else
							{
								this->writerPos += (unsigned long long)offset;
								this->doSeek = false;
								ret = true;
							}
						}
						else
							ret = false;
					}
					break;
				}
				case AssetsSeek_End:
				{
					if (offset > 0)
						ret = false;
					else
					{
						offset = -offset;
						if (offset < 0) //=> offset is the minimum signed long long
							ret = false;
						else
						{
							if (ret = pChild->Seek(AssetsSeek_End, -offset))
							{
								QWORD tempPos = 0;
								if (!pChild->Tell(tempPos) || (tempPos < this->childOffset))
								{
									this->doSeek = true;
									ret = false;
								}
								else
								{
									this->writerPos = tempPos - this->childOffset;
									this->doSeek = false;
								}
							}
							else
								ret = false;
						}
					}
					break;
				}
				default: ret = false;
			}
		}
		return ret;
	}
	bool SetPosition(QWORD pos)
	{
		bool ret;
		if (pChild == NULL)
			ret = false;
		else if (pChild->SetPosition(pos + this->childOffset))
		{
			QWORD tempPos = pos + this->childOffset;
			pChild->Tell(tempPos);
			if (tempPos != (pos + this->childOffset))
			{
				this->doSeek = true;
				ret = false;
			}
			else
			{
				this->writerPos = pos;
				this->doSeek = false;
				ret = true;
			}
		}
		else
			ret = false;
		return ret;
	}

	QWORD Write(QWORD pos, QWORD size, const void *inBuffer)
	{
		QWORD ret;
		if (pChild == NULL)
			ret = 0;
		else if ((pos != (QWORD)-1) && !SetPosition(pos))
			ret = 0;
		else
		{
			QWORD writePos = (doSeek || alwaysSeek) ? (this->childOffset + this->writerPos) : ((QWORD)-1);
			ret = pChild->Write(writePos, size, inBuffer);
			this->writerPos += ret;
			doSeek = false;
		}
		return ret;
	}

	bool Flush()
	{
		return (pChild != NULL) && pChild->Flush();
	}

	bool GetChild(IAssetsWriter *&pWriter)
	{
		pWriter = pChild;
		return (pChild != NULL);
	}
};

IAssetsWriterToWriterOffset *Create_AssetsWriterToWriterOffset(IAssetsWriter *pChild, QWORD offset, bool alwaysSeek)
{
	if (pChild == NULL) return NULL;
	
	AssetsWriterToWriterOffset *pWriter = new AssetsWriterToWriterOffset(pChild, offset, alwaysSeek);

	//AssetsWriterToWriterOffset isn't garbage collectable
	//AddReopenable(pReader);
	return pWriter;
}