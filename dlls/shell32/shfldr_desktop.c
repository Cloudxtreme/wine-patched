
/*
 *    Virtual Desktop Folder
 *
 *    Copyright 1997            Marcus Meissner
 *    Copyright 1998, 1999, 2002    Juergen Schmied
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define COBJMACROS
#define NONAMELESSUNION

#include "winerror.h"
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "wingdi.h"
#include "winuser.h"

#include "ole2.h"
#include "shlguid.h"

#include "pidl.h"
#include "undocshell.h"
#include "shell32_main.h"
#include "shresdef.h"
#include "shlwapi.h"
#include "shellfolder.h"
#include "wine/debug.h"
#include "debughlp.h"
#include "shfldr.h"

WINE_DEFAULT_DEBUG_CHANNEL (shell);

/* Undocumented functions from shdocvw */
extern HRESULT WINAPI IEParseDisplayNameWithBCW(DWORD codepage, LPCWSTR lpszDisplayName, LPBC pbc, LPITEMIDLIST *ppidl);

/***********************************************************************
*     Desktopfolder implementation
*/

typedef struct {
    IShellFolder2 IShellFolder2_iface;
    IPersistFolder2 IPersistFolder2_iface;
    LONG ref;

    /* both paths are parsible from the desktop */
    LPWSTR sPathTarget;     /* complete path to target used for enumeration and ChangeNotify */
    LPITEMIDLIST pidlRoot;  /* absolute pidl */

    UINT cfShellIDList;        /* clipboardformat for IDropTarget */
    BOOL fAcceptFmt;        /* flag for pending Drop */
} IDesktopFolderImpl;

static IDesktopFolderImpl *cached_sf;

static inline IDesktopFolderImpl *impl_from_IShellFolder2(IShellFolder2 *iface)
{
    return CONTAINING_RECORD(iface, IDesktopFolderImpl, IShellFolder2_iface);
}

static inline IDesktopFolderImpl *impl_from_IPersistFolder2( IPersistFolder2 *iface )
{
    return CONTAINING_RECORD(iface, IDesktopFolderImpl, IPersistFolder2_iface);
}

static const shvheader desktop_header[] = {
    {IDS_SHV_COLUMN1, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_RIGHT, 15},
    {IDS_SHV_COLUMN2, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_RIGHT, 10},
    {IDS_SHV_COLUMN3, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_RIGHT, 10},
    {IDS_SHV_COLUMN4, SHCOLSTATE_TYPE_DATE | SHCOLSTATE_ONBYDEFAULT, LVCFMT_RIGHT, 12},
    {IDS_SHV_COLUMN5, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_RIGHT, 5}
};

#define DESKTOPSHELLVIEWCOLUMNS sizeof(desktop_header)/sizeof(shvheader)

/**************************************************************************
 *    ISF_Desktop_fnQueryInterface
 *
 */
static HRESULT WINAPI ISF_Desktop_fnQueryInterface(
                IShellFolder2 * iface, REFIID riid, LPVOID * ppvObj)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);

    TRACE ("(%p)->(%s,%p)\n", This, shdebugstr_guid (riid), ppvObj);

    if (!ppvObj) return E_POINTER;

    *ppvObj = NULL;

    if (IsEqualIID (riid, &IID_IUnknown) ||
        IsEqualIID (riid, &IID_IShellFolder) ||
        IsEqualIID (riid, &IID_IShellFolder2))
    {
        *ppvObj = &This->IShellFolder2_iface;
    }
    else if (IsEqualIID (riid, &IID_IPersist) ||
             IsEqualIID (riid, &IID_IPersistFolder) ||
             IsEqualIID (riid, &IID_IPersistFolder2))
    {
        *ppvObj = &This->IPersistFolder2_iface;
    }

    if (*ppvObj)
    {
        IUnknown_AddRef ((IUnknown *) (*ppvObj));
        TRACE ("-- Interface: (%p)->(%p)\n", ppvObj, *ppvObj);
        return S_OK;
    }
    TRACE ("-- Interface: E_NOINTERFACE\n");
    return E_NOINTERFACE;
}

static ULONG WINAPI ISF_Desktop_fnAddRef (IShellFolder2 * iface)
{
    return 2; /* non-heap based object */
}

static ULONG WINAPI ISF_Desktop_fnRelease (IShellFolder2 * iface)
{
    return 1; /* non-heap based object */
}

/**************************************************************************
 *    ISF_Desktop_fnParseDisplayName
 *
 * NOTES
 *    "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}" and "" binds
 *    to MyComputer
 */
static HRESULT WINAPI ISF_Desktop_fnParseDisplayName (IShellFolder2 * iface,
                HWND hwndOwner, LPBC pbc, LPOLESTR lpszDisplayName,
                DWORD * pchEaten, LPITEMIDLIST * ppidl, DWORD * pdwAttributes)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);
    IShellFolder *shell_folder = (IShellFolder*)iface;
    WCHAR szElement[MAX_PATH];
    LPCWSTR szNext = NULL;
    LPITEMIDLIST pidlTemp = NULL;
    PARSEDURLW urldata;
    HRESULT hr = S_OK;
    CLSID clsid;

    TRACE ("(%p)->(HWND=%p,%p,%p=%s,%p,pidl=%p,%p)\n",
           This, hwndOwner, pbc, lpszDisplayName, debugstr_w(lpszDisplayName),
           pchEaten, ppidl, pdwAttributes);

    if (!ppidl) return E_INVALIDARG;
    *ppidl = 0;

    if (!lpszDisplayName) return E_INVALIDARG;

    if (pchEaten)
        *pchEaten = 0;        /* strange but like the original */

    urldata.cbSize = sizeof(urldata);

    if (lpszDisplayName[0] == ':' && lpszDisplayName[1] == ':')
    {
        szNext = GetNextElementW (lpszDisplayName, szElement, MAX_PATH);
        TRACE ("-- element: %s\n", debugstr_w (szElement));
        SHCLSIDFromStringW (szElement + 2, &clsid);
        pidlTemp = _ILCreateGuid (PT_GUID, &clsid);
    }
    else if (PathGetDriveNumberW (lpszDisplayName) >= 0)
    {
        /*
         * UNIXFS can't handle drives without a mount point yet. We fall back
         * to use the MyComputer interface if we can't get the file attributes
         * on the device.
         */
        char drivePath[] = "A:\\";
        drivePath[0] = 'A' + PathGetDriveNumberW(lpszDisplayName);

        /* it's a filesystem path with a drive. Let MyComputer/UnixDosFolder parse it */
        if (UNIXFS_is_rooted_at_desktop() &&
            GetFileAttributesA(drivePath) != INVALID_FILE_ATTRIBUTES)
        {
            pidlTemp = _ILCreateGuid(PT_GUID, &CLSID_UnixDosFolder);
            TRACE("Using unixfs for %s\n", debugstr_w(lpszDisplayName));
        }
        else
        {
            pidlTemp = _ILCreateMyComputer ();
            TRACE("Using MyComputer for %s\n", debugstr_w(lpszDisplayName));
        }
        szNext = lpszDisplayName;
    }
    else if (PathIsUNCW(lpszDisplayName))
    {
        pidlTemp = _ILCreateNetwork();
        szNext = lpszDisplayName;
    }
    else if( (pidlTemp = SHELL32_CreatePidlFromBindCtx(pbc, lpszDisplayName)) )
    {
        *ppidl = pidlTemp;
        return S_OK;
    }
    else if (SUCCEEDED(ParseURLW(lpszDisplayName, &urldata)))
    {
        if (urldata.nScheme == URL_SCHEME_SHELL) /* handle shell: urls */
        {
            TRACE ("-- shell url: %s\n", debugstr_w(urldata.pszSuffix));
            SHCLSIDFromStringW (urldata.pszSuffix+2, &clsid);
            pidlTemp = _ILCreateGuid (PT_GUID, &clsid);
        }
        else
            return IEParseDisplayNameWithBCW(CP_ACP,lpszDisplayName,pbc,ppidl);
    }
    else
    {
        /* it's a filesystem path on the desktop. Let a FSFolder parse it */

        if (*lpszDisplayName)
        {
            if (*lpszDisplayName == '/')
            {
                /* UNIX paths should be parsed by unixfs */
                IShellFolder *unixFS;
                hr = UnixFolder_Constructor(NULL, &IID_IShellFolder, (LPVOID*)&unixFS);
                if (SUCCEEDED(hr))
                {
                    hr = IShellFolder_ParseDisplayName(unixFS, NULL, NULL,
                            lpszDisplayName, NULL, &pidlTemp, NULL);
                    IShellFolder_Release(unixFS);
                }
            }
            else
            {
                /* build a complete path to create a simple pidl */
                WCHAR szPath[MAX_PATH];
                LPWSTR pathPtr;

                lstrcpynW(szPath, This->sPathTarget, MAX_PATH);
                pathPtr = PathAddBackslashW(szPath);
                if (pathPtr)
                {
                    lstrcpynW(pathPtr, lpszDisplayName, MAX_PATH - (pathPtr - szPath));
                    hr = _ILCreateFromPathW(szPath, &pidlTemp);
                }
                else
                {
                    /* should never reach here, but for completeness */
                    hr = E_NOT_SUFFICIENT_BUFFER;
                }
            }
        }
        else
            pidlTemp = _ILCreateMyComputer();

        szNext = NULL;
    }

    if (SUCCEEDED(hr) && pidlTemp)
    {
        if (szNext && *szNext)
        {
            hr = SHELL32_ParseNextElement(iface, hwndOwner, pbc,
                    &pidlTemp, (LPOLESTR) szNext, pchEaten, pdwAttributes);
        }
        else
        {
            if (pdwAttributes && *pdwAttributes)
                hr = SHELL32_GetItemAttributes(shell_folder, pidlTemp, pdwAttributes);
        }
    }

    *ppidl = pidlTemp;

    TRACE ("(%p)->(-- ret=0x%08x)\n", This, hr);

    return hr;
}

/**************************************************************************
 *  CreateDesktopEnumList()
 */
static const WCHAR Desktop_NameSpaceW[] = { 'S','O','F','T','W','A','R','E',
 '\\','M','i','c','r','o','s','o','f','t','\\','W','i','n','d','o','w','s','\\',
 'C','u','r','r','e','n','t','V','e','r','s','i','o','n','\\','E','x','p','l',
 'o','r','e','r','\\','D','e','s','k','t','o','p','\\','N','a','m','e','s','p',
 'a','c','e','\0' };

static BOOL CreateDesktopEnumList(IEnumIDListImpl *list, DWORD dwFlags)
{
    BOOL ret = TRUE;
    WCHAR szPath[MAX_PATH];

    TRACE("(%p)->(flags=0x%08x)\n", list, dwFlags);

    /* enumerate the root folders */
    if (dwFlags & SHCONTF_FOLDERS)
    {
        HKEY hkey;
        UINT i;

        /* create the pidl for This item */
        ret = AddToEnumList(list, _ILCreateMyComputer());

        for (i=0; i<2; i++) {
            if (ret && !RegOpenKeyExW(i == 0 ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                                      Desktop_NameSpaceW, 0, KEY_READ, &hkey))
            {
                WCHAR iid[50];
                int i=0;

                while (ret)
                {
                    DWORD size;
                    LONG r;

                    size = sizeof (iid) / sizeof (iid[0]);
                    r = RegEnumKeyExW(hkey, i, iid, &size, 0, NULL, NULL, NULL);
                    if (ERROR_SUCCESS == r)
                    {
                        ret = AddToEnumList(list, _ILCreateGuidFromStrW(iid));
                        i++;
                    }
                    else if (ERROR_NO_MORE_ITEMS == r)
                        break;
                    else
                        ret = FALSE;
                }
                RegCloseKey(hkey);
            }
        }
    }

    /* enumerate the elements in %windir%\desktop */
    ret = ret && SHGetSpecialFolderPathW(0, szPath, CSIDL_DESKTOPDIRECTORY, FALSE);
    ret = ret && CreateFolderEnumList(list, szPath, dwFlags);

    return ret;
}

/**************************************************************************
 *        ISF_Desktop_fnEnumObjects
 */
static HRESULT WINAPI ISF_Desktop_fnEnumObjects (IShellFolder2 * iface,
                HWND hwndOwner, DWORD dwFlags, LPENUMIDLIST * ppEnumIDList)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);
    IEnumIDListImpl *list;

    TRACE ("(%p)->(HWND=%p flags=0x%08x pplist=%p)\n",
           This, hwndOwner, dwFlags, ppEnumIDList);

    if (!(list = IEnumIDList_Constructor()))
        return E_OUTOFMEMORY;
    CreateDesktopEnumList(list, dwFlags);
    *ppEnumIDList = &list->IEnumIDList_iface;

    TRACE ("-- (%p)->(new ID List: %p)\n", This, *ppEnumIDList);

    return S_OK;
}

/**************************************************************************
 *        ISF_Desktop_fnBindToObject
 */
static HRESULT WINAPI ISF_Desktop_fnBindToObject (IShellFolder2 * iface,
                LPCITEMIDLIST pidl, LPBC pbcReserved, REFIID riid, LPVOID * ppvOut)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);

    TRACE ("(%p)->(pidl=%p,%p,%s,%p)\n",
           This, pidl, pbcReserved, shdebugstr_guid (riid), ppvOut);

    return SHELL32_BindToChild( This->pidlRoot, This->sPathTarget, pidl, riid, ppvOut );
}

/**************************************************************************
 *    ISF_Desktop_fnBindToStorage
 */
static HRESULT WINAPI ISF_Desktop_fnBindToStorage (IShellFolder2 * iface,
                LPCITEMIDLIST pidl, LPBC pbcReserved, REFIID riid, LPVOID * ppvOut)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);

    FIXME ("(%p)->(pidl=%p,%p,%s,%p) stub\n",
           This, pidl, pbcReserved, shdebugstr_guid (riid), ppvOut);

    *ppvOut = NULL;
    return E_NOTIMPL;
}

/**************************************************************************
 *     ISF_Desktop_fnCompareIDs
 */
static HRESULT WINAPI ISF_Desktop_fnCompareIDs (IShellFolder2 *iface,
                        LPARAM lParam, LPCITEMIDLIST pidl1, LPCITEMIDLIST pidl2)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);
    HRESULT hr;

    TRACE ("(%p)->(0x%08lx,pidl1=%p,pidl2=%p)\n", This, lParam, pidl1, pidl2);
    hr = SHELL32_CompareIDs(iface, lParam, pidl1, pidl2);
    TRACE ("-- 0x%08x\n", hr);
    return hr;
}

/**************************************************************************
 *    ISF_Desktop_fnCreateViewObject
 */
static HRESULT WINAPI ISF_Desktop_fnCreateViewObject (IShellFolder2 * iface,
                              HWND hwndOwner, REFIID riid, LPVOID * ppvOut)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);
    LPSHELLVIEW pShellView;
    HRESULT hr = E_INVALIDARG;

    TRACE ("(%p)->(hwnd=%p,%s,%p)\n",
           This, hwndOwner, shdebugstr_guid (riid), ppvOut);

    if (!ppvOut)
        return E_INVALIDARG;

    *ppvOut = NULL;

    if (IsEqualIID (riid, &IID_IDropTarget))
    {
        WARN ("IDropTarget not implemented\n");
        hr = E_NOTIMPL;
    }
    else if (IsEqualIID (riid, &IID_IContextMenu))
    {
        WARN ("IContextMenu not implemented\n");
        hr = E_NOTIMPL;
    }
    else if (IsEqualIID (riid, &IID_IShellView))
    {
        pShellView = IShellView_Constructor ((IShellFolder *) iface);
        if (pShellView)
        {
            hr = IShellView_QueryInterface (pShellView, riid, ppvOut);
            IShellView_Release (pShellView);
        }
    }
    TRACE ("-- (%p)->(interface=%p)\n", This, ppvOut);
    return hr;
}

/**************************************************************************
 *  ISF_Desktop_fnGetAttributesOf
 */
static HRESULT WINAPI ISF_Desktop_fnGetAttributesOf (IShellFolder2 * iface,
                UINT cidl, LPCITEMIDLIST * apidl, DWORD * rgfInOut)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);
    IShellFolder *shell_folder = (IShellFolder*)iface;

    static const DWORD dwDesktopAttributes = 
        SFGAO_STORAGE | SFGAO_HASPROPSHEET | SFGAO_STORAGEANCESTOR |
        SFGAO_FILESYSANCESTOR | SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_HASSUBFOLDER;
    static const DWORD dwMyComputerAttributes = 
        SFGAO_CANRENAME | SFGAO_CANDELETE | SFGAO_HASPROPSHEET |
        SFGAO_DROPTARGET | SFGAO_FILESYSANCESTOR | SFGAO_FOLDER | SFGAO_HASSUBFOLDER;

    TRACE ("(%p)->(cidl=%d apidl=%p mask=%p (0x%08x))\n",
           This, cidl, apidl, rgfInOut, rgfInOut ? *rgfInOut : 0);

    if (!rgfInOut)
        return E_INVALIDARG;
    if (cidl && !apidl)
        return E_INVALIDARG;

    if (*rgfInOut == 0)
        *rgfInOut = ~0;
    
    if(cidl == 0) {
        *rgfInOut &= dwDesktopAttributes; 
    } else {
        while (cidl > 0 && *apidl) {
            pdump (*apidl);
            if (_ILIsDesktop(*apidl)) { 
                *rgfInOut &= dwDesktopAttributes;
            } else if (_ILIsMyComputer(*apidl)) {
                *rgfInOut &= dwMyComputerAttributes;
            } else {
                SHELL32_GetItemAttributes ( shell_folder, *apidl, rgfInOut);
            }
            apidl++;
            cidl--;
        }
    }
    /* make sure SFGAO_VALIDATE is cleared, some apps depend on that */
    *rgfInOut &= ~SFGAO_VALIDATE;

    TRACE ("-- result=0x%08x\n", *rgfInOut);

    return S_OK;
}

/**************************************************************************
 *    ISF_Desktop_fnGetUIObjectOf
 *
 * PARAMETERS
 *  HWND           hwndOwner, //[in ] Parent window for any output
 *  UINT           cidl,      //[in ] array size
 *  LPCITEMIDLIST* apidl,     //[in ] simple pidl array
 *  REFIID         riid,      //[in ] Requested Interface
 *  UINT*          prgfInOut, //[   ] reserved
 *  LPVOID*        ppvObject) //[out] Resulting Interface
 *
 */
static HRESULT WINAPI ISF_Desktop_fnGetUIObjectOf (IShellFolder2 * iface,
                HWND hwndOwner, UINT cidl, LPCITEMIDLIST * apidl,
                REFIID riid, UINT * prgfInOut, LPVOID * ppvOut)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);

    LPITEMIDLIST pidl;
    IUnknown *pObj = NULL;
    HRESULT hr = E_INVALIDARG;

    TRACE ("(%p)->(%p,%u,apidl=%p,%s,%p,%p)\n",
       This, hwndOwner, cidl, apidl, shdebugstr_guid (riid), prgfInOut, ppvOut);

    if (!ppvOut)
        return E_INVALIDARG;

    *ppvOut = NULL;

    if (IsEqualIID (riid, &IID_IContextMenu))
    {
        if (cidl > 0)
            return ItemMenu_Constructor((IShellFolder*)iface, This->pidlRoot, apidl, cidl, riid, ppvOut);
        else
            return BackgroundMenu_Constructor((IShellFolder*)iface, TRUE, riid, ppvOut);
    }
    else if (IsEqualIID (riid, &IID_IDataObject) && (cidl >= 1))
    {
        pObj = (LPUNKNOWN) IDataObject_Constructor( hwndOwner,
                                                  This->pidlRoot, apidl, cidl);
        hr = S_OK;
    }
    else if (IsEqualIID (riid, &IID_IExtractIconA) && (cidl == 1))
    {
        pidl = ILCombine (This->pidlRoot, apidl[0]);
        pObj = (LPUNKNOWN) IExtractIconA_Constructor (pidl);
        SHFree (pidl);
        hr = S_OK;
    }
    else if (IsEqualIID (riid, &IID_IExtractIconW) && (cidl == 1))
    {
        pidl = ILCombine (This->pidlRoot, apidl[0]);
        pObj = (LPUNKNOWN) IExtractIconW_Constructor (pidl);
        SHFree (pidl);
        hr = S_OK;
    }
    else if (IsEqualIID (riid, &IID_IDropTarget) && (cidl >= 1))
    {
        hr = IShellFolder2_QueryInterface (iface,
                                          &IID_IDropTarget, (LPVOID *) & pObj);
    }
    else if ((IsEqualIID(riid,&IID_IShellLinkW) ||
              IsEqualIID(riid,&IID_IShellLinkA)) && (cidl == 1))
    {
        pidl = ILCombine (This->pidlRoot, apidl[0]);
        hr = IShellLink_ConstructFromFile(NULL, riid, pidl, &pObj);
        SHFree (pidl);
    }
    else
        hr = E_NOINTERFACE;

    if (SUCCEEDED(hr) && !pObj)
        hr = E_OUTOFMEMORY;

    *ppvOut = pObj;
    TRACE ("(%p)->hr=0x%08x\n", This, hr);
    return hr;
}

/**************************************************************************
 *    ISF_Desktop_fnGetDisplayNameOf
 *
 * NOTES
 *    special case: pidl = null gives desktop-name back
 */
static HRESULT WINAPI ISF_Desktop_fnGetDisplayNameOf (IShellFolder2 * iface,
                LPCITEMIDLIST pidl, DWORD dwFlags, LPSTRRET strRet)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);
    HRESULT hr = S_OK;
    LPWSTR pszPath;

    TRACE ("(%p)->(pidl=%p,0x%08x,%p)\n", This, pidl, dwFlags, strRet);
    pdump (pidl);

    if (!strRet)
        return E_INVALIDARG;

    pszPath = CoTaskMemAlloc((MAX_PATH +1) * sizeof(WCHAR));
    if (!pszPath)
        return E_OUTOFMEMORY;

    if (_ILIsDesktop (pidl))
    {
        if ((GET_SHGDN_RELATION (dwFlags) == SHGDN_NORMAL) &&
            (GET_SHGDN_FOR (dwFlags) & SHGDN_FORPARSING))
            strcpyW(pszPath, This->sPathTarget);
        else
            HCR_GetClassNameW(&CLSID_ShellDesktop, pszPath, MAX_PATH);
    }
    else if (_ILIsPidlSimple (pidl))
    {
        GUID const *clsid;

        if ((clsid = _ILGetGUIDPointer (pidl)))
        {
            if (GET_SHGDN_FOR (dwFlags) & SHGDN_FORPARSING)
            {
                BOOL bWantsForParsing;

                /*
                 * We can only get a filesystem path from a shellfolder if the
                 *  value WantsFORPARSING in CLSID\\{...}\\shellfolder exists.
                 *
                 * Exception: The MyComputer folder doesn't have this key,
                 *   but any other filesystem backed folder it needs it.
                 */
                if (IsEqualIID (clsid, &CLSID_MyComputer))
                {
                    bWantsForParsing = TRUE;
                }
                else
                {
                    /* get the "WantsFORPARSING" flag from the registry */
                    static const WCHAR clsidW[] =
                     { 'C','L','S','I','D','\\',0 };
                    static const WCHAR shellfolderW[] =
                     { '\\','s','h','e','l','l','f','o','l','d','e','r',0 };
                    static const WCHAR wantsForParsingW[] =
                     { 'W','a','n','t','s','F','o','r','P','a','r','s','i','n',
                     'g',0 };
                    WCHAR szRegPath[100];
                    LONG r;

                    lstrcpyW (szRegPath, clsidW);
                    SHELL32_GUIDToStringW (clsid, &szRegPath[6]);
                    lstrcatW (szRegPath, shellfolderW);
                    r = SHGetValueW(HKEY_CLASSES_ROOT, szRegPath,
                                    wantsForParsingW, NULL, NULL, NULL);
                    if (r == ERROR_SUCCESS)
                        bWantsForParsing = TRUE;
                    else
                        bWantsForParsing = FALSE;
                }

                if ((GET_SHGDN_RELATION (dwFlags) == SHGDN_NORMAL) &&
                     bWantsForParsing)
                {
                    /*
                     * we need the filesystem path to the destination folder.
                     * Only the folder itself can know it
                     */
                    hr = SHELL32_GetDisplayNameOfChild (iface, pidl, dwFlags,
                                                        pszPath,
                                                        MAX_PATH);
                }
                else
                {
                    /* parsing name like ::{...} */
                    pszPath[0] = ':';
                    pszPath[1] = ':';
                    SHELL32_GUIDToStringW (clsid, &pszPath[2]);
                }
            }
            else
            {
                /* user friendly name */
                HCR_GetClassNameW (clsid, pszPath, MAX_PATH);
            }
        }
        else
        {
            int cLen = 0;

            /* file system folder or file rooted at the desktop */
            if ((GET_SHGDN_FOR(dwFlags) == SHGDN_FORPARSING) &&
                (GET_SHGDN_RELATION(dwFlags) != SHGDN_INFOLDER))
            {
                lstrcpynW(pszPath, This->sPathTarget, MAX_PATH - 1);
                PathAddBackslashW(pszPath);
                cLen = lstrlenW(pszPath);
            }

            _ILSimpleGetTextW(pidl, pszPath + cLen, MAX_PATH - cLen);

            if (!_ILIsFolder(pidl))
                SHELL_FS_ProcessDisplayFilename(pszPath, dwFlags);
        }
    }
    else
    {
        /* a complex pidl, let the subfolder do the work */
        hr = SHELL32_GetDisplayNameOfChild (iface, pidl, dwFlags,
                                            pszPath, MAX_PATH);
    }

    if (SUCCEEDED(hr))
    {
        /* Win9x always returns ANSI strings, NT always returns Unicode strings */
        if (GetVersion() & 0x80000000)
        {
            strRet->uType = STRRET_CSTR;
            if (!WideCharToMultiByte(CP_ACP, 0, pszPath, -1, strRet->u.cStr, MAX_PATH,
                                     NULL, NULL))
                strRet->u.cStr[0] = '\0';
            CoTaskMemFree(pszPath);
        }
        else
        {
            strRet->uType = STRRET_WSTR;
            strRet->u.pOleStr = pszPath;
        }
    }
    else
        CoTaskMemFree(pszPath);

    TRACE ("-- (%p)->(%s,0x%08x)\n", This,
    strRet->uType == STRRET_CSTR ? strRet->u.cStr :
    debugstr_w(strRet->u.pOleStr), hr);
    return hr;
}

/**************************************************************************
 *  ISF_Desktop_fnSetNameOf
 *  Changes the name of a file object or subfolder, possibly changing its item
 *  identifier in the process.
 *
 * PARAMETERS
 *  HWND          hwndOwner,  //[in ] Owner window for output
 *  LPCITEMIDLIST pidl,       //[in ] simple pidl of item to change
 *  LPCOLESTR     lpszName,   //[in ] the items new display name
 *  DWORD         dwFlags,    //[in ] SHGNO formatting flags
 *  LPITEMIDLIST* ppidlOut)   //[out] simple pidl returned
 */
static HRESULT WINAPI ISF_Desktop_fnSetNameOf (IShellFolder2 * iface,
                HWND hwndOwner, LPCITEMIDLIST pidl,    /* simple pidl */
                LPCOLESTR lpName, DWORD dwFlags, LPITEMIDLIST * pPidlOut)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);

    FIXME ("(%p)->(%p,pidl=%p,%s,%u,%p) stub\n", This, hwndOwner, pidl,
           debugstr_w (lpName), dwFlags, pPidlOut);

    return E_FAIL;
}

static HRESULT WINAPI ISF_Desktop_fnGetDefaultSearchGUID(IShellFolder2 *iface,
                GUID * pguid)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);
    FIXME ("(%p)->(%p) stub\n", This, pguid);
    return E_NOTIMPL;
}

static HRESULT WINAPI ISF_Desktop_fnEnumSearches (IShellFolder2 *iface,
                IEnumExtraSearch ** ppenum)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);
    FIXME ("(%p)->(%p) stub\n", This, ppenum);
    return E_NOTIMPL;
}

static HRESULT WINAPI ISF_Desktop_fnGetDefaultColumn (IShellFolder2 * iface,
                DWORD reserved, ULONG * pSort, ULONG * pDisplay)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);

    TRACE ("(%p)->(%d %p %p)\n", This, reserved, pSort, pDisplay);

    if (pSort)
        *pSort = 0;
    if (pDisplay)
        *pDisplay = 0;

    return S_OK;
}
static HRESULT WINAPI ISF_Desktop_fnGetDefaultColumnState (
                IShellFolder2 * iface, UINT iColumn, DWORD * pcsFlags)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);

    TRACE ("(%p)->(%d %p)\n", This, iColumn, pcsFlags);

    if (!pcsFlags || iColumn >= DESKTOPSHELLVIEWCOLUMNS)
    return E_INVALIDARG;

    *pcsFlags = desktop_header[iColumn].pcsFlags;

    return S_OK;
}

static HRESULT WINAPI ISF_Desktop_fnGetDetailsEx (IShellFolder2 * iface,
                LPCITEMIDLIST pidl, const SHCOLUMNID * pscid, VARIANT * pv)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);
    FIXME ("(%p)->(%p %p %p) stub\n", This, pidl, pscid, pv);
    return E_NOTIMPL;
}

static HRESULT WINAPI ISF_Desktop_fnGetDetailsOf (IShellFolder2 * iface,
                LPCITEMIDLIST pidl, UINT iColumn, SHELLDETAILS * psd)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);

    HRESULT hr = S_OK;

    TRACE ("(%p)->(%p %i %p)\n", This, pidl, iColumn, psd);

    if (!psd || iColumn >= DESKTOPSHELLVIEWCOLUMNS)
        return E_INVALIDARG;

    if (!pidl)
        return SHELL32_GetColumnDetails(desktop_header, iColumn, psd);

    /* the data from the pidl */
    psd->str.uType = STRRET_CSTR;
    switch (iColumn)
    {
    case 0:        /* name */
        hr = IShellFolder2_GetDisplayNameOf(iface, pidl,
                   SHGDN_NORMAL | SHGDN_INFOLDER, &psd->str);
        break;
    case 1:        /* size */
        _ILGetFileSize (pidl, psd->str.u.cStr, MAX_PATH);
        break;
    case 2:        /* type */
        _ILGetFileType (pidl, psd->str.u.cStr, MAX_PATH);
        break;
    case 3:        /* date */
        _ILGetFileDate (pidl, psd->str.u.cStr, MAX_PATH);
        break;
    case 4:        /* attributes */
        _ILGetFileAttributes (pidl, psd->str.u.cStr, MAX_PATH);
        break;
    }

    return hr;
}

static HRESULT WINAPI ISF_Desktop_fnMapColumnToSCID (
                IShellFolder2 * iface, UINT column, SHCOLUMNID * pscid)
{
    IDesktopFolderImpl *This = impl_from_IShellFolder2(iface);
    FIXME ("(%p)->(%d %p) stub\n", This, column, pscid);
    return E_NOTIMPL;
}

static const IShellFolder2Vtbl vt_MCFldr_ShellFolder2 =
{
    ISF_Desktop_fnQueryInterface,
    ISF_Desktop_fnAddRef,
    ISF_Desktop_fnRelease,
    ISF_Desktop_fnParseDisplayName,
    ISF_Desktop_fnEnumObjects,
    ISF_Desktop_fnBindToObject,
    ISF_Desktop_fnBindToStorage,
    ISF_Desktop_fnCompareIDs,
    ISF_Desktop_fnCreateViewObject,
    ISF_Desktop_fnGetAttributesOf,
    ISF_Desktop_fnGetUIObjectOf,
    ISF_Desktop_fnGetDisplayNameOf,
    ISF_Desktop_fnSetNameOf,
    /* ShellFolder2 */
    ISF_Desktop_fnGetDefaultSearchGUID,
    ISF_Desktop_fnEnumSearches,
    ISF_Desktop_fnGetDefaultColumn,
    ISF_Desktop_fnGetDefaultColumnState,
    ISF_Desktop_fnGetDetailsEx,
    ISF_Desktop_fnGetDetailsOf,
    ISF_Desktop_fnMapColumnToSCID
};

/**************************************************************************
 *    IPersist
 */
static HRESULT WINAPI ISF_Desktop_IPersistFolder2_fnQueryInterface(
    IPersistFolder2 *iface, REFIID riid, LPVOID *ppvObj)
{
    IDesktopFolderImpl *This = impl_from_IPersistFolder2( iface );
    return IShellFolder2_QueryInterface(&This->IShellFolder2_iface, riid, ppvObj);
}

static ULONG WINAPI ISF_Desktop_IPersistFolder2_fnAddRef(
    IPersistFolder2 *iface)
{
    IDesktopFolderImpl *This = impl_from_IPersistFolder2( iface );
    return IShellFolder2_AddRef(&This->IShellFolder2_iface);
}

static ULONG WINAPI ISF_Desktop_IPersistFolder2_fnRelease(
    IPersistFolder2 *iface)
{
    IDesktopFolderImpl *This = impl_from_IPersistFolder2( iface );
    return IShellFolder2_Release(&This->IShellFolder2_iface);
}

static HRESULT WINAPI ISF_Desktop_IPersistFolder2_fnGetClassID(
    IPersistFolder2 *iface, CLSID *clsid)
{
    *clsid = CLSID_ShellDesktop;
    return S_OK;
}
static HRESULT WINAPI ISF_Desktop_IPersistFolder2_fnInitialize(
    IPersistFolder2 *iface, LPCITEMIDLIST pidl)
{
    IDesktopFolderImpl *This = impl_from_IPersistFolder2( iface );
    FIXME ("(%p)->(%p) stub\n", This, pidl);
    return E_NOTIMPL;
}
static HRESULT WINAPI ISF_Desktop_IPersistFolder2_fnGetCurFolder(
    IPersistFolder2 *iface, LPITEMIDLIST *ppidl)
{
    IDesktopFolderImpl *This = impl_from_IPersistFolder2( iface );
    *ppidl = ILClone(This->pidlRoot);
    return S_OK;
}

static const IPersistFolder2Vtbl vt_IPersistFolder2 =
{
    ISF_Desktop_IPersistFolder2_fnQueryInterface,
    ISF_Desktop_IPersistFolder2_fnAddRef,
    ISF_Desktop_IPersistFolder2_fnRelease,
    ISF_Desktop_IPersistFolder2_fnGetClassID,
    ISF_Desktop_IPersistFolder2_fnInitialize,
    ISF_Desktop_IPersistFolder2_fnGetCurFolder
};

void release_desktop_folder(void)
{
    if (!cached_sf) return;
    SHFree(cached_sf->pidlRoot);
    SHFree(cached_sf->sPathTarget);
    LocalFree(cached_sf);
}

/**************************************************************************
 *    ISF_Desktop_Constructor
 */
HRESULT WINAPI ISF_Desktop_Constructor (
                IUnknown * pUnkOuter, REFIID riid, LPVOID * ppv)
{
    WCHAR szMyPath[MAX_PATH];

    TRACE ("unkOut=%p %s\n", pUnkOuter, shdebugstr_guid (riid));

    if (!ppv)
        return E_POINTER;
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    if (!cached_sf)
    {
        IDesktopFolderImpl *sf;

        if (!SHGetSpecialFolderPathW( 0, szMyPath, CSIDL_DESKTOPDIRECTORY, TRUE ))
            return E_UNEXPECTED;

        sf = LocalAlloc( LMEM_ZEROINIT, sizeof (IDesktopFolderImpl) );
        if (!sf)
            return E_OUTOFMEMORY;

        sf->ref = 1;
        sf->IShellFolder2_iface.lpVtbl = &vt_MCFldr_ShellFolder2;
        sf->IPersistFolder2_iface.lpVtbl = &vt_IPersistFolder2;
        sf->pidlRoot = _ILCreateDesktop();    /* my qualified pidl */
        sf->sPathTarget = SHAlloc( (lstrlenW(szMyPath) + 1)*sizeof(WCHAR) );
        lstrcpyW( sf->sPathTarget, szMyPath );

        if (InterlockedCompareExchangePointer((void *)&cached_sf, sf, NULL) != NULL)
        {
            /* some other thread already been here */
            SHFree( sf->pidlRoot );
            SHFree( sf->sPathTarget );
            LocalFree( sf );
        }
    }

    return IShellFolder2_QueryInterface( &cached_sf->IShellFolder2_iface, riid, ppv );
}
