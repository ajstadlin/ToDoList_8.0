// Preferences.cpp: implementation of the CPreferences class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Preferences.h"

#include "..\shared\misc.h"
#include "..\shared\filemisc.h"
#include "..\shared\driveinfo.h"
#include "..\shared\regkey.h"
#include "..\shared\autoflag.h"

#include "..\3rdparty\stdiofileex.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////

INIENTRY::INIENTRY(LPCTSTR szName, LPCTSTR szValue, BOOL bQuote) 
	: sName(szName), sValue(szValue), bQuoted(bQuote) 
{
}

CString INIENTRY::Format() const
{
	CString sEntry;

	if (bQuoted)
		sEntry.Format(_T("%s=\"%s\""), sName, sValue);
	else
		sEntry.Format(_T("%s=%s"), sName, sValue);

	return sEntry;
}

BOOL INIENTRY::Parse(const CString& sEntry)
{
	int nEquals = sEntry.Find('=');

	if (nEquals != -1)
	{
		sName = sEntry.Left(nEquals);
		sName.TrimRight();

		sValue = sEntry.Mid(nEquals + 1);
		sValue.TrimLeft();

		// remove quotes
		bQuoted = sValue.Replace(_T("\""), _T(""));

		return !sName.IsEmpty();
	}

	return FALSE;
}

BOOL INIENTRY::operator==(const INIENTRY& ie) const
{
	return ((sName == ie.sName) && 
		(sValue == ie.sValue) && 
		(bQuoted == ie.bQuoted));
}

/////////////////////////////////////////////////////////////////////////////////

INISECTION::INISECTION(LPCTSTR szName) : sSection(szName) 
{
	aEntries.InitHashTable(199); // prime number closest to 200
}

INISECTION::INISECTION(const INISECTION& other) : sSection(other.sSection)
{
	Misc::CopyStrT<INIENTRY>(other.aEntries, aEntries);
}

//////////////////////////////////////////////////////////////////////

static LPCTSTR ENDL = _T("\r\n");
static CString NULLSTR;

//////////////////////////////////////////////////////////////////////

CIniSectionArray	CPreferences::s_aIni;
BOOL				CPreferences::s_bDirty = FALSE;
BOOL				CPreferences::s_bIni = FALSE;
CString				CPreferences::s_sPrefsPath;
int					CPreferences::s_nRef = 0;
CCriticalSection	CPreferences::s_cs;
BOOL				CPreferences::s_bLocked = FALSE;

//////////////////////////////////////////////////////////////////////

#define LOCKPREFS()                           \
	ASSERT(!s_bLocked);                       \
	if (s_bLocked) return;                    \
	CSingleLock lock(&s_cs, TRUE);            \
	CAutoFlag af(s_bLocked, lock.IsLocked());

#define LOCKPREFSRET(ret)                     \
	ASSERT(!s_bLocked);                       \
	if (s_bLocked) return ret;                \
	CSingleLock lock(&s_cs, TRUE);            \
	CAutoFlag af(s_bLocked, lock.IsLocked());

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CPreferences::CPreferences() 
	: 
#pragma warning (disable: 4355)
	m_iPrefs(*this)
#pragma warning (default: 4355)
{
	// prevent anyone else changing the shared resources
	// for the duration of this function
	LOCKPREFS();

	if (!IsInitialised())
		return;

	// if no other object is active we need to reload the preferences
	if (s_nRef == 0)
	{
		if (s_bIni)
		{
			// check for existing backup file first
			CString sBackupPath = CFileBackup::BuildBackupPath(s_sPrefsPath, FBS_OVERWRITE);
			
			if (FileMisc::FileExists(sBackupPath))
				FileMisc::MoveFile(sBackupPath, s_sPrefsPath, TRUE, TRUE);

			CIniSectionArray aItems;

			if (Load(s_sPrefsPath, aItems))
			{
				Release(s_aIni);
				Copy(aItems, s_aIni);
				Release(aItems);
			}
		}

		s_bDirty = FALSE;
	}
				
	s_nRef++; // increment reference count
}

CPreferences::~CPreferences()
{
	// prevent anyone else changing the shared resources
	// for the duration of this function
	LOCKPREFS();
			
	s_nRef--; // decrement reference count
	
	// save ini?
	if ((s_nRef == 0) && s_bIni)
	{
		// Note: We no longer release the current data
		// until the next load
		if (!SaveInternal())
			s_nRef++;
	}
}

void CPreferences::Release()
{
	// prevent anyone else changing the shared resources
	// for the duration of this function
	LOCKPREFS();
			
	if (s_bIni)
		Release(s_aIni);
}

void CPreferences::Release(CIniSectionArray& aSections)
{
	ASSERT(s_nRef == 0);
	ASSERT(s_bLocked);

	int nSection = aSections.GetSize();

	while (nSection--)
		delete aSections[nSection];

	aSections.RemoveAll();
}

BOOL CPreferences::Initialise(LPCTSTR szPrefsPath, BOOL bIni)
{
	if (Misc::IsEmpty(szPrefsPath))
	{
		ASSERT(0);
		return FALSE;
	}

	LOCKPREFSRET(FALSE);

	// Must be able to save existing ini file
	if (s_bIni)
	{
		if (!s_sPrefsPath.IsEmpty() && !Save())
		{
			ASSERT(0);
			return FALSE;
		}

		if (!bIni)
			Release(s_aIni);
	}

	// Must be able to load the 'new' prefs
	if (bIni && FileMisc::FileExists(szPrefsPath))
	{
		CIniSectionArray aItems;

		if (!Load(szPrefsPath, aItems))
		{
			ASSERT(0);
			return FALSE;
		}
		else
		{
			// Take a copy in case next time we can't load the file
			Copy(aItems, s_aIni);
			Release(aItems);
		}
	}

	s_sPrefsPath = szPrefsPath;
	s_bIni = bIni;
	s_nRef = 0;

	return TRUE;
}

BOOL CPreferences::IsInitialised()
{
	return !s_sPrefsPath.IsEmpty();
}

BOOL CPreferences::IsEmpty()
{
	return (s_aIni.GetSize() == 0);
}

CString CPreferences::GetPath(BOOL bFriendly)
{
	if (UsesIni() || !bFriendly)
		return s_sPrefsPath;

	// else friendly registry path
	return (_T("HKEY_CURRENT_USER\\") + s_sPrefsPath);
}

BOOL CPreferences::Save()
{
	if (!s_bIni)
	{
		ASSERT(0);
		return FALSE;
	}

	if (!s_bDirty)
		return TRUE; // nothing to do

	// prevent anyone else changing the shared resources
	// for the duration of this function
	LOCKPREFSRET(FALSE);

	return SaveInternal();
}

// THIS IS AN INTERNAL METHOD THAT ASSUMES CALLERS HAVE INITIALISED A LOCK ALREADY
BOOL CPreferences::SaveInternal()
{
	ASSERT(s_bLocked);
	ASSERT(s_bIni);

	if (!s_bLocked)
		return FALSE;

	if (!s_bDirty)
		return TRUE; // nothing to do

	// insert application version
	WriteIniString(_T("AppVer"), _T("Version"), FileMisc::GetAppVersion(), FALSE);

	// Build output as a single formatted string so that the 
	// time the prefs file is open is as short as possible
	CString sPrefsContents;
	
	for (int nSection = 0; nSection < s_aIni.GetSize(); nSection++)
	{
		// write section line
		INISECTION* pSection = s_aIni[nSection];
		
		CString sLine;
		sLine.Format(_T("[%s]%s"), pSection->sSection, ENDL);
		
		sPrefsContents += sLine;
		
		// write entries to a CStringArray, then sort it and write it to file
		CStringArray aEntries;
		aEntries.SetSize(pSection->aEntries.GetCount(), 10);
		
		// save map to array
		POSITION pos = pSection->aEntries.GetStartPosition();
		int nEntry = 0;
		
		while (pos)
		{
			CString sDummy;
			INIENTRY ie;
			
			pSection->aEntries.GetNextAssoc(pos, sDummy, ie);

			aEntries.SetAt(nEntry++, ie.Format());
		}
		
		// sort array
		Misc::SortArray(aEntries);
		
		// format by newlines
		sPrefsContents += Misc::FormatArray(aEntries, ENDL);
		sPrefsContents += ENDL;
		sPrefsContents += ENDL;
	}
	
	// backup file first
	CTempFileBackup backup(s_sPrefsPath);
	
	// write prefs
	{
		CStdioFileEx file;
	
		if (!file.Open(s_sPrefsPath, CFile::modeWrite | CFile::modeCreate, SFEF_UTF16))
			return FALSE;

		file.WriteString(sPrefsContents);
	}
	
	s_bDirty = FALSE;

	return TRUE;
}

UINT CPreferences::GetProfileInt(LPCTSTR lpszSection, LPCTSTR lpszEntry, int nDefault) const
{
	if (s_bIni)
	{
		CString sValue = GetProfileString(lpszSection, lpszEntry);
		
		if (!sValue.IsEmpty()) 
			return _ttol(sValue);
	}
	else
	{
		CString sKeyName;
		sKeyName.Format(_T("%s\\%s"), s_sPrefsPath, lpszSection);

		CRegKey2 reg;

		if (reg.Open(HKEY_CURRENT_USER, sKeyName, TRUE) == ERROR_SUCCESS)
		{
			DWORD dwValue = 0;

			if (reg.Read(lpszEntry, dwValue) == ERROR_SUCCESS)
				return (UINT)dwValue;
		}
	}
	
	// all else
	return nDefault;
}

BOOL CPreferences::WriteProfileInt(LPCTSTR lpszSection, LPCTSTR lpszEntry, int nValue)
{
	if (s_bIni)
	{
		// FALSE -> We don't quote numbers
		return WritePreferenceString(lpszSection, lpszEntry, Misc::Format(nValue), FALSE);
	}
	else
	{
		CString sKeyName;
		sKeyName.Format(_T("%s\\%s"), s_sPrefsPath, lpszSection);

		CRegKey2 reg;

		if (reg.Open(HKEY_CURRENT_USER, sKeyName, FALSE) == ERROR_SUCCESS)
		{
			return (reg.Write(lpszEntry, (DWORD)nValue) == ERROR_SUCCESS);
		}
	}

	return FALSE;
}

CString CPreferences::GetProfileString(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPCTSTR lpszDefault) const
{
	if (s_bIni)
	{
		// prevent anyone else changing the shared resources
		// for the duration of this function
		LOCKPREFSRET(NULLSTR);

		return GetIniString(lpszSection, lpszEntry, lpszDefault);
	}

	// else
	CString sKeyName;
	sKeyName.Format(_T("%s\\%s"), s_sPrefsPath, lpszSection);

	CRegKey2 reg;

	if (reg.Open(HKEY_CURRENT_USER, sKeyName, TRUE) == ERROR_SUCCESS)
	{
		CString sValue;

		if (reg.Read(lpszEntry, sValue) == ERROR_SUCCESS)
			return sValue;
	}

	// else
	return lpszDefault;
}

BOOL CPreferences::WriteProfileString(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPCTSTR lpszValue)
{
	// quote string values passed in from outside
	return WritePreferenceString(lpszSection, lpszEntry, lpszValue, TRUE);
}

BOOL CPreferences::WritePreferenceString(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPCTSTR lpszValue, BOOL bQuoted)
{
	if (s_bIni)
	{
		// prevent anyone else changing the shared resources
		// for the duration of this function
		LOCKPREFSRET(FALSE);

		return WriteIniString(lpszSection, lpszEntry, lpszValue, bQuoted);
	}

	// else registry
	CString sKeyName;
	sKeyName.Format(_T("%s\\%s"), s_sPrefsPath, lpszSection);

	CRegKey2 reg;

	if (reg.Open(HKEY_CURRENT_USER, sKeyName, FALSE) == ERROR_SUCCESS)
		return (reg.Write(lpszEntry, lpszValue) == ERROR_SUCCESS);

	return FALSE;
}

// THIS IS AN INTERNAL METHOD THAT ASSUMES CALLERS HAVE INITIALISED A LOCK ALREADY
CString CPreferences::GetIniString(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPCTSTR lpszDefault)
{
	ASSERT(s_bIni);
	ASSERT(s_bLocked);

	if (s_bIni && s_bLocked)
	{
		INISECTION* pSection = GetSection(lpszSection, FALSE);
		
		if (pSection)
		{
			CString sValue;
			
			if (GetEntryValue(*pSection, lpszEntry, sValue))
				return sValue;
		}
	
		// else
		if (lpszDefault && *lpszDefault) // non-empty
			return lpszDefault;
	}

	// else
	return NULLSTR;
}

// THIS IS AN INTERNAL METHOD THAT ASSUMES CALLERS HAVE INITIALISED A LOCK ALREADY
BOOL CPreferences::WriteIniString(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPCTSTR lpszValue, BOOL bQuoted)
{
	ASSERT(s_bIni);
	ASSERT(s_bLocked);

	if (s_bIni && s_bLocked)
	{
		INISECTION* pSection = GetSection(lpszSection, TRUE);
		ASSERT(pSection);
		
		if (pSection)
		{
			SetEntryValue(*pSection, lpszEntry, lpszValue, bQuoted);
			return TRUE;
		}
	}
	
	// else
	return FALSE;
}

double CPreferences::GetProfileDouble(LPCTSTR lpszSection, LPCTSTR lpszEntry, double dDefault) const
{
	CString sValue = GetProfileString(lpszSection, lpszEntry, Misc::Format(dDefault, 6));
	
	if (sValue.IsEmpty())
		return dDefault;
	else
		return Misc::Atof(sValue);
}

bool CPreferences::WriteProfileDouble(LPCTSTR lpszSection, LPCTSTR lpszEntry, double dValue)
{
	// FALSE -> We don't quote numbers
	return (WritePreferenceString(lpszSection, lpszEntry, Misc::Format(dValue, 6), FALSE) != FALSE);
}

int CPreferences::GetProfileArray(LPCTSTR lpszSection, CStringArray& aItems, BOOL bAllowEmpty) const
{
	return GetProfileArray(&m_iPrefs, lpszSection, aItems, bAllowEmpty);
}

int CPreferences::GetProfileArray(const IPreferences* pPrefs, LPCTSTR lpszSection, CStringArray& aItems, BOOL bAllowEmpty)
{
	aItems.RemoveAll();
	int nCount = pPrefs->GetProfileInt(lpszSection, _T("ItemCount"), 0);
	
	// items
	for (int nItem = 0; nItem < nCount; nItem++)
	{
		CString sItemKey, sItem;
		sItemKey.Format(_T("Item%d"), nItem);
		sItem = pPrefs->GetProfileString(lpszSection, sItemKey);
		
		if (bAllowEmpty || !sItem.IsEmpty())
			aItems.Add(sItem);
	}
	
	return aItems.GetSize();
}

void CPreferences::WriteProfileArray(LPCTSTR lpszSection, const CStringArray& aItems, BOOL bDelSection)
{
	WriteProfileArray(&m_iPrefs, lpszSection, aItems, bDelSection);
}

void CPreferences::WriteProfileArray(IPreferences* pPrefs, LPCTSTR lpszSection, const CStringArray& aItems, BOOL bDelSection)
{
	// pre-delete?
	if (bDelSection)
		pPrefs->DeleteProfileSection(lpszSection);

	int nCount = aItems.GetSize();
	
	// items
	for (int nItem = 0; nItem < nCount; nItem++)
	{
		CString sItemKey;
		sItemKey.Format(_T("Item%d"), nItem);
		pPrefs->WriteProfileString(lpszSection, sItemKey, aItems[nItem]);
	}
	
	// item count
	pPrefs->WriteProfileInt(lpszSection, _T("ItemCount"), nCount);
}

int CPreferences::GetProfileArray(LPCTSTR lpszSection, CDWordArray& aItems) const
{
	return GetProfileArray(&m_iPrefs, lpszSection, aItems);
}

int CPreferences::GetProfileArray(const IPreferences* pPrefs, LPCTSTR lpszSection, CDWordArray& aItems)
{
	aItems.RemoveAll();
	int nCount = pPrefs->GetProfileInt(lpszSection, _T("ItemCount"), 0);
	
	// items
	for (int nItem = 0; nItem < nCount; nItem++)
	{
		CString sItemKey, sItem;

		sItemKey.Format(_T("Item%d"), nItem);
		aItems.Add(pPrefs->GetProfileInt(lpszSection, sItemKey));
	}
	
	return aItems.GetSize();
}

void CPreferences::WriteProfileArray(LPCTSTR lpszSection, const CDWordArray& aItems, BOOL bDelSection)
{
	WriteProfileArray(&m_iPrefs, lpszSection, aItems, bDelSection);
}

void CPreferences::WriteProfileArray(IPreferences* pPrefs, LPCTSTR lpszSection, const CDWordArray& aItems, BOOL bDelSection)
{
	// pre-delete?
	if (bDelSection)
		pPrefs->DeleteProfileSection(lpszSection);

	int nCount = aItems.GetSize();
	
	// items
	for (int nItem = 0; nItem < nCount; nItem++)
	{
		CString sItemKey;
		sItemKey.Format(_T("Item%d"), nItem);
		pPrefs->WriteProfileInt(lpszSection, sItemKey, (int)aItems[nItem]);
	}
	
	// item count
	pPrefs->WriteProfileInt(lpszSection, _T("ItemCount"), nCount);
}

// THIS IS AN INTERNAL METHOD THAT ASSUMES CALLERS HAVE INITIALISED A LOCK ALREADY
BOOL CPreferences::GetEntryValue(const INISECTION& section, LPCTSTR lpszEntry, CString& sValue)
{
	ASSERT(s_bIni);
	ASSERT(s_bLocked);
	
	if (s_bIni && s_bLocked)
	{
		CString sKey(lpszEntry);
		sKey.MakeUpper();
		
		INIENTRY ie;
		
		if (section.aEntries.Lookup(sKey, ie))
		{
			sValue = ie.sValue;
			return TRUE;
		}
	}

	// else
	return FALSE;
}

void CPreferences::SetEntryValue(INISECTION& section, LPCTSTR lpszEntry, LPCTSTR szValue, BOOL bQuoted)
{
	SetEntryValue(section, INIENTRY(lpszEntry, szValue, bQuoted));
}

// THIS IS AN INTERNAL METHOD THAT ASSUMES CALLERS HAVE INITIALISED A LOCK ALREADY
void CPreferences::SetEntryValue(INISECTION& section, const INIENTRY& ie)
{
	ASSERT(s_bIni);
	ASSERT(s_bLocked);

	if (s_bIni && s_bLocked)
	{
		INIENTRY ieExist;
		CString sKey(ie.sName);
		sKey.MakeUpper();

		if (!section.aEntries.Lookup(sKey, ieExist) || !(ie == ieExist))
		{
			section.aEntries[sKey] = ie;
			s_bDirty = TRUE;
		}
	}
}

// THIS IS AN INTERNAL METHOD THAT ASSUMES CALLERS HAVE INITIALISED A LOCK ALREADY
INISECTION* CPreferences::GetSection(LPCTSTR lpszSection, BOOL bCreateNotExist)
{
	ASSERT(s_bIni);
	ASSERT(s_bLocked);

	if (s_bIni && s_bLocked)
	{
		int nSection = FindSection(lpszSection);

		if (nSection != -1)
			return s_aIni[nSection];
		
		// add a new section
		if (bCreateNotExist)
		{
			INISECTION* pSection = new INISECTION(lpszSection);
			
			s_aIni.Add(pSection);
			return pSection;
		}
	}
	
	// else
	return NULL;
}

// THIS IS AN INTERNAL METHOD THAT ASSUMES CALLERS HAVE INITIALISED A LOCK ALREADY
int CPreferences::FindSection(LPCTSTR lpszSection, BOOL bIncSubSections)
{
	ASSERT(s_bIni);
	ASSERT(s_bLocked);

	if (s_bIni && s_bLocked)
	{
		int nLenSection = lstrlen(lpszSection);
		int nSection = s_aIni.GetSize();
		
		while (nSection--)
		{
			const CString& sSection = s_aIni[nSection]->sSection;

			if (sSection.GetLength() < nLenSection)
			{
				continue;
			}
			else if (sSection.GetLength() == nLenSection)
			{
				if (sSection.CompareNoCase(lpszSection) == 0)
					return nSection;
			}
			else // sSection.GetLength() > nLenSection
			{
				if (bIncSubSections)
				{
					// look for parent section at head of subsection
					CString sTest = sSection.Left(nLenSection);

					if (sTest.CompareNoCase(lpszSection) == 0)
						return nSection;
				}
			}
		}
	}

	// not found
	return -1;
}

BOOL CPreferences::DeleteProfileSection(LPCTSTR lpszSection, BOOL bIncSubSections)
{
	ASSERT(IsInitialised());
	ASSERT(lpszSection && *lpszSection);

	if (s_bIni)
	{
		// prevent anyone else changing the shared resources
		// for the duration of this function
		LOCKPREFSRET(FALSE);
		
		int nSection = FindSection(lpszSection);
		 
		if (nSection != -1)
		{
			delete s_aIni[nSection];
			s_aIni.RemoveAt(nSection);
			s_bDirty = TRUE;
			
			if (bIncSubSections)
			{
				nSection = FindSection(lpszSection, TRUE);

				while (nSection != -1)
				{
					delete s_aIni[nSection];
					s_aIni.RemoveAt(nSection);
					nSection = FindSection(lpszSection, TRUE);
				} 
			}

			return TRUE;
		}

		// not found
		return FALSE;
	}

	// else registry
	CString sFullKey;
	sFullKey.Format(_T("%s\\%s"), s_sPrefsPath, lpszSection);

	return CRegKey2::DeleteKey(HKEY_CURRENT_USER, sFullKey);
}

BOOL CPreferences::DeleteProfileEntry(LPCTSTR lpszSection, LPCTSTR lpszEntry)
{
	ASSERT(IsInitialised());
	ASSERT(lpszSection && *lpszSection);
	ASSERT(lpszEntry && *lpszEntry);

	if (s_bIni)
	{
		// prevent anyone else changing the shared resources
		// for the duration of this function
		LOCKPREFSRET(FALSE);
		
		INISECTION* pSection = GetSection(lpszSection, FALSE);

		if (pSection)
		{
			CString sKey(lpszEntry);
			sKey.MakeUpper();

			pSection->aEntries.RemoveKey(sKey);
			s_bDirty = TRUE;

			return TRUE;
		}

		// not found
		return FALSE;
	}

	// else registry
	CString sFullKey;
	sFullKey.Format(_T("%s\\%s"), s_sPrefsPath, lpszSection);

	CRegKey2 reg;
	
	if (reg.Open(HKEY_CURRENT_USER, sFullKey, FALSE) == ERROR_SUCCESS)
		return (reg.DeleteValue(lpszEntry) == ERROR_SUCCESS);

	// else
	return FALSE;
}

BOOL CPreferences::HasProfileSection(LPCTSTR lpszSection) const
{
	if (s_bIni)
	{
		// prevent anyone else changing the shared resources
		// for the duration of this function
		LOCKPREFSRET(FALSE);
		
		return (GetSection(lpszSection, FALSE) != NULL);
	}

	// else registry
	CString sFullKey;
	sFullKey.Format(_T("%s\\%s"), s_sPrefsPath, lpszSection);

	return CRegKey2::KeyExists(HKEY_CURRENT_USER, sFullKey);
}

int CPreferences::GetProfileSectionNames(CStringArray& aSections)
{
	// prevent anyone else changing the shared resources
	// for the duration of this function
	LOCKPREFSRET(0);
	
	aSections.RemoveAll();

	if (s_bIni)
	{
		int nSection = s_aIni.GetSize();
		aSections.SetSize(nSection);
		
		while (nSection--)
			aSections[nSection] = s_aIni[nSection]->sSection;
	}

	return aSections.GetSize();
}

CString CPreferences::KeyFromFile(LPCTSTR szFilePath, BOOL bFileNameOnly)
{
	CString sKey = (bFileNameOnly ? FileMisc::GetFileNameFromPath(szFilePath) : szFilePath);
	sKey.Replace('\\', '_');

	// if the filepath is on a removable drive then we strip off the drive letter
	if (!bFileNameOnly)
	{
		int nType = CDriveInfo::GetPathType(szFilePath);

		if (nType == DRIVE_REMOVABLE)
			sKey = sKey.Mid(1);
	}

	return sKey;
}

void CPreferences::Copy(const CIniSectionArray& aSrc, CIniSectionArray& aDest)
{
	ASSERT(aDest.GetSize() == 0);

	int nSection = aSrc.GetSize();

	while (nSection--)
		aDest.Add(new INISECTION(*aSrc[nSection]));
}

BOOL CPreferences::Load(LPCTSTR szFilePath, CIniSectionArray& aSections)
{
	// read the ini file
	CStdioFileEx file;

	if (!file.Open(szFilePath, CFile::modeRead))
		return FALSE;

	CString sLine;
	INISECTION* pCurSection = NULL;

	while (file.ReadString(sLine))
	{
		if (!sLine.IsEmpty())
		{
			// is it a section ?
			if (sLine[0] == '[')
			{
				CString sSection = sLine.Mid(1, sLine.GetLength() - 2);

				// assume (for speed) that the section is already unique
				pCurSection = new INISECTION(sSection);
				aSections.Add(pCurSection);

				ASSERT (pCurSection != NULL);
			}
			// else an entry
			else if (pCurSection)
			{
				INIENTRY ie;

				if (ie.Parse(sLine))
					pCurSection->aEntries[Misc::ToUpper(ie.sName)] = ie;
			}
		}
	}

	return TRUE;
}

//////////////////////////////////////////////////////////////////////
	
UINT CPreferences::CIPreferencesImpl::GetProfileInt(LPCWSTR lpszSection, LPCWSTR lpszEntry, int nDefault) const
{
	return m_prefs.GetProfileInt(lpszSection, lpszEntry, nDefault);
}

bool CPreferences::CIPreferencesImpl::WriteProfileInt(LPCWSTR lpszSection, LPCWSTR lpszEntry, int nValue)
{
	return (m_prefs.WriteProfileInt(lpszSection, lpszEntry, nValue) != FALSE);
}

LPCWSTR CPreferences::CIPreferencesImpl::GetProfileString(LPCWSTR lpszSection, LPCWSTR lpszEntry, LPCWSTR lpszDefault) const
{
	static CString sValue;
	sValue = m_prefs.GetProfileString(lpszSection, lpszEntry, lpszDefault);

	return sValue;
}

bool CPreferences::CIPreferencesImpl::WriteProfileString(LPCWSTR lpszSection, LPCWSTR lpszEntry, LPCWSTR lpszValue)
{
	return (m_prefs.WriteProfileString(lpszSection, lpszEntry, lpszValue) != FALSE);
}
	
double CPreferences::CIPreferencesImpl::GetProfileDouble(LPCWSTR lpszSection, LPCWSTR lpszEntry, double dDefault) const
{
	return m_prefs.GetProfileDouble(lpszSection, lpszEntry, dDefault);
}

bool CPreferences::CIPreferencesImpl::WriteProfileDouble(LPCWSTR lpszSection, LPCWSTR lpszEntry, double dValue)
{
	return (m_prefs.WriteProfileDouble(lpszSection, lpszEntry, dValue) != FALSE);
}
	
bool CPreferences::CIPreferencesImpl::DeleteProfileEntry(LPCWSTR lpszSection, LPCWSTR lpszEntry)
{
	return (m_prefs.DeleteProfileEntry(lpszSection, lpszEntry) != FALSE);
}

bool CPreferences::CIPreferencesImpl::DeleteProfileSection(LPCWSTR lpszSection)
{
	return (m_prefs.DeleteProfileSection(lpszSection) != FALSE);
}

//////////////////////////////////////////////////////////////////////
	
