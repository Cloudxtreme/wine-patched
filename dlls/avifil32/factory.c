/*
 * Copyright 2002 Michael Günnewig
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

#include <stdarg.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winerror.h"
#include "ole2.h"
#include "rpcproxy.h"

#include "initguid.h"
#include "vfw.h"
#include "avifile_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(avifile);

HMODULE AVIFILE_hModule   = NULL;

static BOOL    AVIFILE_bLocked;
static UINT    AVIFILE_uUseCount;

static HRESULT WINAPI IClassFactory_fnQueryInterface(LPCLASSFACTORY iface,REFIID riid,LPVOID *ppobj);
static ULONG   WINAPI IClassFactory_fnAddRef(LPCLASSFACTORY iface);
static ULONG   WINAPI IClassFactory_fnRelease(LPCLASSFACTORY iface);
static HRESULT WINAPI IClassFactory_fnCreateInstance(LPCLASSFACTORY iface,LPUNKNOWN pOuter,REFIID riid,LPVOID *ppobj);
static HRESULT WINAPI IClassFactory_fnLockServer(LPCLASSFACTORY iface,BOOL dolock);

static const IClassFactoryVtbl iclassfact = {
  IClassFactory_fnQueryInterface,
  IClassFactory_fnAddRef,
  IClassFactory_fnRelease,
  IClassFactory_fnCreateInstance,
  IClassFactory_fnLockServer
};

typedef struct
{
  /* IUnknown fields */
  IClassFactory IClassFactory_iface;
  DWORD	 dwRef;

  CLSID  clsid;
} IClassFactoryImpl;

static inline IClassFactoryImpl *impl_from_IClassFactory(IClassFactory *iface)
{
  return CONTAINING_RECORD(iface, IClassFactoryImpl, IClassFactory_iface);
}

static HRESULT AVIFILE_CreateClassFactory(const CLSID *pclsid, const IID *riid,
					  LPVOID *ppv)
{
  IClassFactoryImpl *pClassFactory = NULL;
  HRESULT            hr;

  *ppv = NULL;

  pClassFactory = HeapAlloc(GetProcessHeap(), 0, sizeof(*pClassFactory));
  if (pClassFactory == NULL)
    return E_OUTOFMEMORY;

  pClassFactory->IClassFactory_iface.lpVtbl = &iclassfact;
  pClassFactory->dwRef     = 0;
  pClassFactory->clsid     = *pclsid;

  hr = IClassFactory_QueryInterface(&pClassFactory->IClassFactory_iface, riid, ppv);
  if (FAILED(hr)) {
    HeapFree(GetProcessHeap(), 0, pClassFactory);
    *ppv = NULL;
  }

  return hr;
}

static HRESULT WINAPI IClassFactory_fnQueryInterface(LPCLASSFACTORY iface,
						     REFIID riid,LPVOID *ppobj)
{
  TRACE("(%p,%p,%p)\n", iface, riid, ppobj);

  if ((IsEqualGUID(&IID_IUnknown, riid)) ||
      (IsEqualGUID(&IID_IClassFactory, riid))) {
    *ppobj = iface;
    IClassFactory_AddRef(iface);
    return S_OK;
  }

  return E_NOINTERFACE;
}

static ULONG WINAPI IClassFactory_fnAddRef(LPCLASSFACTORY iface)
{
  IClassFactoryImpl *This = impl_from_IClassFactory(iface);

  TRACE("(%p)\n", iface);

  return ++(This->dwRef);
}

static ULONG WINAPI IClassFactory_fnRelease(LPCLASSFACTORY iface)
{
  IClassFactoryImpl *This = impl_from_IClassFactory(iface);

  TRACE("(%p)\n", iface);
  if ((--(This->dwRef)) > 0)
    return This->dwRef;

  HeapFree(GetProcessHeap(), 0, This);

  return 0;
}

static HRESULT WINAPI IClassFactory_fnCreateInstance(LPCLASSFACTORY iface,
						     LPUNKNOWN pOuter,
						     REFIID riid,LPVOID *ppobj)
{
  IClassFactoryImpl *This = impl_from_IClassFactory(iface);

  TRACE("(%p,%p,%s,%p)\n", iface, pOuter, debugstr_guid(riid),
	ppobj);

  if (!ppobj)
    return E_INVALIDARG;
  *ppobj = NULL;

  if (pOuter && !IsEqualGUID(&IID_IUnknown, riid))
    return E_INVALIDARG;

  if (IsEqualGUID(&CLSID_AVIFile, &This->clsid))
    return AVIFILE_CreateAVIFile(pOuter, riid, ppobj);
  if (IsEqualGUID(&CLSID_WAVFile, &This->clsid))
    return AVIFILE_CreateWAVFile(pOuter, riid, ppobj);

  if (pOuter)
    return CLASS_E_NOAGGREGATION;

  if (IsEqualGUID(&CLSID_ICMStream, &This->clsid))
    return AVIFILE_CreateICMStream(riid,ppobj);
  if (IsEqualGUID(&CLSID_ACMStream, &This->clsid))
    return AVIFILE_CreateACMStream(riid,ppobj);

  return E_NOINTERFACE;
}

static HRESULT WINAPI IClassFactory_fnLockServer(LPCLASSFACTORY iface,BOOL dolock)
{
  TRACE("(%p,%d)\n",iface,dolock);

  AVIFILE_bLocked = dolock;

  return S_OK;
}

LPCWSTR AVIFILE_BasenameW(LPCWSTR szPath)
{
#define SLASH(w) ((w) == '/' || (w) == '\\')

  LPCWSTR szCur;

  for (szCur = szPath + lstrlenW(szPath);
       szCur > szPath && !SLASH(*szCur) && *szCur != ':';)
    szCur--;

  if (szCur == szPath)
    return szCur;
  else
    return szCur + 1;

#undef SLASH
}

/***********************************************************************
 *		DllGetClassObject (AVIFIL32.@)
 */
HRESULT WINAPI DllGetClassObject(REFCLSID pclsid, REFIID piid, LPVOID *ppv)
{
  HRESULT hr;

  TRACE("(%s,%s,%p)\n", debugstr_guid(pclsid), debugstr_guid(piid), ppv);

  if (pclsid == NULL || piid == NULL || ppv == NULL)
    return E_FAIL;

  hr = AVIFILE_CreateClassFactory(pclsid,piid,ppv);
  if (SUCCEEDED(hr))
    return hr;

  return avifil32_DllGetClassObject(pclsid,piid,ppv);
}

/*****************************************************************************
 *		DllCanUnloadNow		(AVIFIL32.@)
 */
HRESULT WINAPI DllCanUnloadNow(void)
{
  return ((AVIFILE_bLocked || AVIFILE_uUseCount) ? S_FALSE : S_OK);
}

/*****************************************************************************
 *		DllMain		[AVIFIL32.init]
 */
BOOL WINAPI DllMain(HINSTANCE hInstDll, DWORD fdwReason, LPVOID lpvReserved)
{
  TRACE("(%p,%d,%p)\n", hInstDll, fdwReason, lpvReserved);

  switch (fdwReason) {
  case DLL_PROCESS_ATTACH:
    DisableThreadLibraryCalls(hInstDll);
    AVIFILE_hModule = hInstDll;
    break;
  };

  return TRUE;
}

/***********************************************************************
 *		DllRegisterServer (AVIFIL32.@)
 */
HRESULT WINAPI DllRegisterServer(void)
{
    return __wine_register_resources( AVIFILE_hModule );
}

/***********************************************************************
 *		DllUnregisterServer (AVIFIL32.@)
 */
HRESULT WINAPI DllUnregisterServer(void)
{
    return __wine_unregister_resources( AVIFILE_hModule );
}
