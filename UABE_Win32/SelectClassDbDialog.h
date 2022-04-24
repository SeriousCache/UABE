#pragma once
#include <Windows.h>
#include <memory>
#include "../AssetsTools/ClassDatabaseFile.h"

class SelectClassDbDialog
{
	HINSTANCE hInstance;
	HWND hParentWnd;
	HWND hDialog;

	std::string version;
	std::string fileName;
	bool dialogReason_DbNotFound;

	UINT doneParentMessage;

	std::unique_ptr<ClassDatabaseFile, void(*)(ClassDatabaseFile*)> pClassDatabaseResult;
	bool rememberForVersion;
	bool rememberForAll;
	
	ClassDatabasePackage &classPackage;

	static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

public:
	SelectClassDbDialog(HINSTANCE hInstance, HWND hParentWnd, ClassDatabasePackage &classPackage);
	inline void setEngineVersion(std::string version)
	{
		this->version = std::move(version);
	}
	inline void setAffectedFileName(std::string fileName)
	{
		this->fileName = std::move(fileName);
	}
	//Specifies the first description line. 
	//databaseNotFound == true  => "No type database matches the player version %s."
	//databaseNotFound == false => "The selected file has player version %s."
	inline void setDialogReason(bool databaseNotFound)
	{
		this->dialogReason_DbNotFound = databaseNotFound;
	}
	//Returns when the dialog is closed. Returns false if the dialog could not be opened
	bool ShowModal();
	//Returns once the dialog has been created. The parent window receives a message <doneParentMessage> when the dialog closes.
	HWND ShowModeless(UINT doneParentMessage);
	void ForceCancel(bool rememberForVersion = false, bool rememberForAll = false);
	
	inline std::unique_ptr<ClassDatabaseFile, void(*)(ClassDatabaseFile*)> getClassDatabaseResult_Move()
	{
		std::unique_ptr<ClassDatabaseFile, void(*)(ClassDatabaseFile*)> ret = std::move(pClassDatabaseResult);
		pClassDatabaseResult.release(); //Should already be done by the copy assignment above.
		return ret;
	}
	inline bool isRememberForVersion()
	{
		return rememberForVersion;
	}
	inline bool isRememberForAll()
	{
		return rememberForAll;
	}
	inline const std::string &getEngineVersion()
	{
		return this->version;
	}
};