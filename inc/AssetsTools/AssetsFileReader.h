#pragma once

#include "defines.h"

ASSETSTOOLS_API void AssetsVerifyLoggerToMessageBox(const char *message);
ASSETSTOOLS_API void AssetsVerifyLoggerToConsole(const char *message);

//Abstract class for resources that can be closed and restored when needed.
//This is used to keep "opened" FILEs beyond standard library limits without using OS-specific functions.
//Used by all AssetsReader and AssetsWriter classes; if they aren't restorable, Close() will fail.
class IAssetsReopenable
{
public:
	ASSETSTOOLS_API virtual ~IAssetsReopenable()=0;

	//Reopen a closed instance. Returns false on failure, true on success or if the instance already was opened.
	virtual bool Reopen()=0;

	//Returns whether the instance is open.
	virtual bool IsOpen()=0;

	//Close an opened instance. Returns false on failure, true on success or if the instance already was closed.
	virtual bool Close()=0;
};

//Enum to distinguish readers and writers.
enum AssetsRWTypes
{
	AssetsRWType_Unknown,
	AssetsRWType_Reader,
	AssetsRWType_Writer,
};
//Enum to get the reader or writer type.
enum AssetsRWClasses
{
	AssetsRWClass_Unknown, //Use for custom types.
	AssetsRWClass_ReaderFromFile,
	AssetsRWClass_ReaderFromSplitFile,
	AssetsRWClass_ReaderFromMemory,
	AssetsRWClass_ReaderFromReaderRange,
	AssetsRWClass_WriterToFile,
	AssetsRWClass_WriterToMemory,
	AssetsRWClass_WriterToWriterOffset,
};

//Seek origin types.
enum AssetsSeekTypes
{
	//Use the beginning of the stream as the origin.
	AssetsSeek_Begin,
	//Use the current position as the origin.
	AssetsSeek_Cur,
	//Use the end of the stream as the origin.
	AssetsSeek_End,
};

class IAssetsRW : public IAssetsReopenable
{
public:
	//Get the instance's type (reader or writer).
	virtual AssetsRWTypes GetType()=0;
	//Get the instance's class.
	virtual AssetsRWClasses GetClass()=0;

	//Get the stream's current position. 
	virtual bool Tell(QWORD &pos)=0;
	//Move the stream's current position relative to an origin.
	virtual bool Seek(AssetsSeekTypes origin, long long offset)=0;
	//Set the stream's position.
	virtual bool SetPosition(QWORD pos)=0;
};

class IAssetsReader : public IAssetsRW
{
public:
	//Read [size] bytes of data from the stream to outBuffer, starting at [pos] (or the current position if -1).
	//Returns the amount of bytes read and copied to outBuffer. If the return value is smaller than size, a read error or EOF occured.
	//On failure, if nullUnread is set, the rest of outBuffer will be filled with zeros.
	virtual QWORD Read(QWORD pos, QWORD size, void *outBuffer, bool nullUnread = true)=0;
	//Read [size] bytes of data from the stream to outBuffer, starting at the current position.
	inline QWORD Read(QWORD size, void *outBuffer) { return Read((QWORD)-1, size, outBuffer); }
};

class IAssetsWriter : public IAssetsRW
{
public:
	//Write [size] bytes of data from inBuffer to the stream, starting at [pos] (or the current position if -1) and filling skipped bytes with null.
	//Returns the amount of bytes written. If the return value is smaller than size, a write error occured.
	virtual QWORD Write(QWORD pos, QWORD size, const void *inBuffer)=0;
	//Write [size] bytes of data from inBuffer to the stream, starting at the current position.
	inline QWORD Write(QWORD size, const void *inBuffer) { return Write((QWORD)-1, size, inBuffer); }

	//Write all remaining data from internal buffers to the file, if applicable.
	virtual bool Flush()=0;
};

class IAssetsReaderFromReaderRange : public IAssetsReader
{
public:
	//Returns this writer's current memory buffer and the amount of commited bytes. 
	virtual bool GetChild(IAssetsReader *&pReader)=0;
};

class IAssetsWriterToMemory : public IAssetsWriter
{
public:
	//Returns this writer's current memory buffer and the amount of commited bytes. 
	virtual bool GetBuffer(void *&buffer, size_t &dataSize)=0;
	//Determine whether this writer uses a dynamic or a fixed-length buffer.
	virtual bool IsDynamicBuffer()=0;
	//Specify whether the writer should be allowed to resize the dynamic buffer if required or free it with the writer.
	virtual bool SetFreeBuffer(bool doFree)=0;
	//Resize the internal buffer. If targetLen is smaller than the current buffer size, some data will get discarded.
	virtual bool Resize(size_t targetLen)=0;
};

class IAssetsWriterToWriterOffset : public IAssetsWriter
{
public:
	//Returns this writer's current memory buffer and the amount of commited bytes. 
	virtual bool GetChild(IAssetsWriter *&pWriter)=0;
};

//Open flags for garbage collectable file readers and writers. Combinable with logical OR, except RWOpenFlags_None.
typedef unsigned int AssetsRWOpenFlags;
#define RWOpenFlags_None 0U //Default : Open the underlying file handles on the first use and temporarily close them if the handle limit is reached.
#define RWOpenFlags_Immediately 1U //Immediately open all file handles, before returning from the Create function. If that fails, Create returns NULL.
#define RWOpenFlags_Unclosable 2U //Never temporarily close the handles.

//Close and free an IAssetsReader or IAssetsWriter. 
ASSETSTOOLS_API void Free_AssetsReopenable(IAssetsReopenable *pObject);
inline void Free_AssetsReader(IAssetsReader *pReader) { Free_AssetsReopenable(pReader); }
inline void Free_AssetsWriter(IAssetsWriter *pWriter) { Free_AssetsReopenable(pWriter); }

//Create a reader from a file stored in [filePath] (UTF-8) in text or binary mode.
ASSETSTOOLS_API IAssetsReader *Create_AssetsReaderFromFile(const char *filePath, bool binary, AssetsRWOpenFlags openFlags);
//Create a reader from a file stored in [filePath] (UTF-16) in text or binary mode.
ASSETSTOOLS_API IAssetsReader *Create_AssetsReaderFromFile(const wchar_t *filePath, bool binary, AssetsRWOpenFlags openFlags);
//Create a reader using a FILE* opened by the caller. Use this if the caller requires the FILE* otherwise hidden to allow garbage collection.
ASSETSTOOLS_API IAssetsReader *Create_AssetsReaderFromFile(FILE *pFile);

//Create a reader from .splitX files, with .split0 stored in [filePath] (UTF-8) in text or binary mode.
ASSETSTOOLS_API IAssetsReader *Create_AssetsReaderFromSplitFile(const char *filePath, bool binary, bool allowUpdate, AssetsRWOpenFlags openFlags);
//Create a reader from .splitX files, with .split0 stored in [filePath] (UTF-16) in text or binary mode.
ASSETSTOOLS_API IAssetsReader *Create_AssetsReaderFromSplitFile(const wchar_t *filePath, bool binary, bool allowUpdate, AssetsRWOpenFlags openFlags);

//Create a reader from [bufLen] bytes of data stored in [buf], optionally creating a local copy for the reader's lifetime if [copyBuf] == true.
ASSETSTOOLS_API IAssetsReader *Create_AssetsReaderFromMemory(const void *buf, size_t bufLen, bool copyBuf);

//Create a reader using a [rangeSize] bytes long range starting at [rangeStart] in the child reader [pChild], optionally setting the child reader position for each Read call.
ASSETSTOOLS_API IAssetsReaderFromReaderRange *Create_AssetsReaderFromReaderRange(IAssetsReader *pChild, QWORD rangeStart, QWORD rangeSize, bool alwaysSeek = true);

//Create a writer to a file stored in [filePath] (UTF-8) in text or binary mode, replacing existing data in that file if discard is set.
ASSETSTOOLS_API IAssetsWriter *Create_AssetsWriterToFile(const char *filePath, bool discard, bool binary, AssetsRWOpenFlags openFlags);
//Create a writer to a file stored in [filePath] (UTF-16) in text or binary mode, replacing existing data in that file if discard is set.
ASSETSTOOLS_API IAssetsWriter *Create_AssetsWriterToFile(const wchar_t *filePath, bool discard, bool binary, AssetsRWOpenFlags openFlags);
//Create a writer using a FILE* opened by the caller. Use this if the caller requires the FILE* otherwise hidden to allow garbage collection.
ASSETSTOOLS_API IAssetsWriter *Create_AssetsWriterToFile(FILE *pFile);

//Create a writer to a [bufLen] bytes long buffer [buf] with [initialDataLen] precommited bytes.
ASSETSTOOLS_API IAssetsWriterToMemory *Create_AssetsWriterToMemory(void *buf, size_t bufLen, size_t initialDataLen = 0);
//Create a writer to a dynamic buffer with a maximum length of [maximumLen] bytes and a initial buffer size of [initialLen] bytes.
ASSETSTOOLS_API IAssetsWriterToMemory *Create_AssetsWriterToMemory(size_t initialLen = 0, size_t maximumLen = ~(size_t)0);
//Free the dynamic buffer retrieved through IAssetsWriterToMemory::GetBuffer.
ASSETSTOOLS_API void Free_AssetsWriterToMemory_DynBuf(void *buffer);

//Create a writer using a child writer [pChild] with a data offset of [offset] bytes, optionally setting the child writer position for each Write call.
ASSETSTOOLS_API IAssetsWriterToWriterOffset *Create_AssetsWriterToWriterOffset(IAssetsWriter *pChild, QWORD offset, bool alwaysSeek = true);