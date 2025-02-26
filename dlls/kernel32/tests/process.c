/*
 * Unit test suite for process functions
 *
 * Copyright 2002 Eric Pouech
 * Copyright 2006 Dmitry Timoshkov
 * Copyright 2014 Michael Müller
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

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "wincon.h"
#include "winnls.h"
#include "winternl.h"
#include "tlhelp32.h"

#include "wine/test.h"

/* PROCESS_ALL_ACCESS in Vista+ PSDKs is incompatible with older Windows versions */
#define PROCESS_ALL_ACCESS_NT4 (PROCESS_ALL_ACCESS & ~0xf000)
/* THREAD_ALL_ACCESS in Vista+ PSDKs is incompatible with older Windows versions */
#define THREAD_ALL_ACCESS_NT4 (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3ff)

#define expect_eq_d(expected, actual) \
    do { \
      int value = (actual); \
      ok((expected) == value, "Expected " #actual " to be %d (" #expected ") is %d\n", \
          (expected), value); \
    } while (0)
#define expect_eq_s(expected, actual) \
    do { \
      LPCSTR value = (actual); \
      ok(lstrcmpA((expected), value) == 0, "Expected " #actual " to be L\"%s\" (" #expected ") is L\"%s\"\n", \
          expected, value); \
    } while (0)
#define expect_eq_ws_i(expected, actual) \
    do { \
      LPCWSTR value = (actual); \
      ok(lstrcmpiW((expected), value) == 0, "Expected " #actual " to be L\"%s\" (" #expected ") is L\"%s\"\n", \
          wine_dbgstr_w(expected), wine_dbgstr_w(value)); \
    } while (0)

static HINSTANCE hkernel32, hntdll;
static void   (WINAPI *pGetNativeSystemInfo)(LPSYSTEM_INFO);
static BOOL   (WINAPI *pGetSystemRegistryQuota)(PDWORD, PDWORD);
static BOOL   (WINAPI *pIsWow64Process)(HANDLE,PBOOL);
static LPVOID (WINAPI *pVirtualAllocEx)(HANDLE, LPVOID, SIZE_T, DWORD, DWORD);
static BOOL   (WINAPI *pVirtualFreeEx)(HANDLE, LPVOID, SIZE_T, DWORD);
static BOOL   (WINAPI *pQueryFullProcessImageNameA)(HANDLE hProcess, DWORD dwFlags, LPSTR lpExeName, PDWORD lpdwSize);
static BOOL   (WINAPI *pQueryFullProcessImageNameW)(HANDLE hProcess, DWORD dwFlags, LPWSTR lpExeName, PDWORD lpdwSize);
static DWORD  (WINAPI *pK32GetProcessImageFileNameA)(HANDLE,LPSTR,DWORD);
static struct _TEB * (WINAPI *pNtCurrentTeb)(void);
static HANDLE (WINAPI *pCreateJobObjectW)(LPSECURITY_ATTRIBUTES sa, LPCWSTR name);
static BOOL   (WINAPI *pAssignProcessToJobObject)(HANDLE job, HANDLE process);
static BOOL   (WINAPI *pIsProcessInJob)(HANDLE process, HANDLE job, PBOOL result);
static BOOL   (WINAPI *pTerminateJobObject)(HANDLE job, UINT exit_code);
static BOOL   (WINAPI *pQueryInformationJobObject)(HANDLE job, JOBOBJECTINFOCLASS class, LPVOID info, DWORD len, LPDWORD ret_len);
static BOOL   (WINAPI *pSetInformationJobObject)(HANDLE job, JOBOBJECTINFOCLASS class, LPVOID info, DWORD len);
static HANDLE (WINAPI *pCreateIoCompletionPort)(HANDLE file, HANDLE existing_port, ULONG_PTR key, DWORD threads);
static BOOL   (WINAPI *pGetNumaProcessorNode)(UCHAR, PUCHAR);
static NTSTATUS (WINAPI *pNtQueryInformationProcess)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
static BOOL   (WINAPI *pProcessIdToSessionId)(DWORD,DWORD*);
static DWORD  (WINAPI *pWTSGetActiveConsoleSessionId)(void);
static HANDLE (WINAPI *pCreateToolhelp32Snapshot)(DWORD, DWORD);
static BOOL   (WINAPI *pProcess32First)(HANDLE, PROCESSENTRY32*);
static BOOL   (WINAPI *pProcess32Next)(HANDLE, PROCESSENTRY32*);
static BOOL   (WINAPI *pThread32First)(HANDLE, THREADENTRY32*);
static BOOL   (WINAPI *pThread32Next)(HANDLE, THREADENTRY32*);
static BOOL   (WINAPI *pGetLogicalProcessorInformationEx)(LOGICAL_PROCESSOR_RELATIONSHIP,SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*,DWORD*);

/* ############################### */
static char     base[MAX_PATH];
static char     selfname[MAX_PATH];
static char*    exename;
static char     resfile[MAX_PATH];

static int      myARGC;
static char**   myARGV;

/* As some environment variables get very long on Unix, we only test for
 * the first 127 bytes.
 * Note that increasing this value past 256 may exceed the buffer size
 * limitations of the *Profile functions (at least on Wine).
 */
#define MAX_LISTED_ENV_VAR      128

/* ---------------- portable memory allocation thingie */

static char     memory[1024*256];
static char*    memory_index = memory;

static char*    grab_memory(size_t len)
{
    char*       ret = memory_index;
    /* align on dword */
    len = (len + 3) & ~3;
    memory_index += len;
    assert(memory_index <= memory + sizeof(memory));
    return ret;
}

static void     release_memory(void)
{
    memory_index = memory;
}

/* ---------------- simplistic tool to encode/decode strings (to hide \ " ' and such) */

static const char* encodeA(const char* str)
{
    char*       ptr;
    size_t      len,i;

    if (!str) return "";
    len = strlen(str) + 1;
    ptr = grab_memory(len * 2 + 1);
    for (i = 0; i < len; i++)
        sprintf(&ptr[i * 2], "%02x", (unsigned char)str[i]);
    ptr[2 * len] = '\0';
    return ptr;
}

static const char* encodeW(const WCHAR* str)
{
    char*       ptr;
    size_t      len,i;

    if (!str) return "";
    len = lstrlenW(str) + 1;
    ptr = grab_memory(len * 4 + 1);
    assert(ptr);
    for (i = 0; i < len; i++)
        sprintf(&ptr[i * 4], "%04x", (unsigned int)(unsigned short)str[i]);
    ptr[4 * len] = '\0';
    return ptr;
}

static unsigned decode_char(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    assert(c >= 'A' && c <= 'F');
    return c - 'A' + 10;
}

static char*    decodeA(const char* str)
{
    char*       ptr;
    size_t      len,i;

    len = strlen(str) / 2;
    if (!len--) return NULL;
    ptr = grab_memory(len + 1);
    for (i = 0; i < len; i++)
        ptr[i] = (decode_char(str[2 * i]) << 4) | decode_char(str[2 * i + 1]);
    ptr[len] = '\0';
    return ptr;
}

/* This will be needed to decode Unicode strings saved by the child process
 * when we test Unicode functions.
 */
static WCHAR*   decodeW(const char* str)
{
    size_t      len;
    WCHAR*      ptr;
    int         i;

    len = strlen(str) / 4;
    if (!len--) return NULL;
    ptr = (WCHAR*)grab_memory(len * 2 + 1);
    for (i = 0; i < len; i++)
        ptr[i] = (decode_char(str[4 * i]) << 12) |
            (decode_char(str[4 * i + 1]) << 8) |
            (decode_char(str[4 * i + 2]) << 4) |
            (decode_char(str[4 * i + 3]) << 0);
    ptr[len] = '\0';
    return ptr;
}

/******************************************************************
 *		init
 *
 * generates basic information like:
 *      base:           absolute path to curr dir
 *      selfname:       the way to reinvoke ourselves
 *      exename:        executable without the path
 * function-pointers, which are not implemented in all windows versions
 */
static BOOL init(void)
{
    char *p;

    myARGC = winetest_get_mainargs( &myARGV );
    if (!GetCurrentDirectoryA(sizeof(base), base)) return FALSE;
    strcpy(selfname, myARGV[0]);

    /* Strip the path of selfname */
    if ((p = strrchr(selfname, '\\')) != NULL) exename = p + 1;
    else exename = selfname;

    if ((p = strrchr(exename, '/')) != NULL) exename = p + 1;

    hkernel32 = GetModuleHandleA("kernel32");
    hntdll    = GetModuleHandleA("ntdll.dll");

    pNtCurrentTeb = (void *)GetProcAddress(hntdll, "NtCurrentTeb");
    pNtQueryInformationProcess = (void *)GetProcAddress(hntdll, "NtQueryInformationProcess");

    pGetNativeSystemInfo = (void *) GetProcAddress(hkernel32, "GetNativeSystemInfo");
    pGetSystemRegistryQuota = (void *) GetProcAddress(hkernel32, "GetSystemRegistryQuota");
    pIsWow64Process = (void *) GetProcAddress(hkernel32, "IsWow64Process");
    pVirtualAllocEx = (void *) GetProcAddress(hkernel32, "VirtualAllocEx");
    pVirtualFreeEx = (void *) GetProcAddress(hkernel32, "VirtualFreeEx");
    pQueryFullProcessImageNameA = (void *) GetProcAddress(hkernel32, "QueryFullProcessImageNameA");
    pQueryFullProcessImageNameW = (void *) GetProcAddress(hkernel32, "QueryFullProcessImageNameW");
    pK32GetProcessImageFileNameA = (void *) GetProcAddress(hkernel32, "K32GetProcessImageFileNameA");
    pCreateJobObjectW = (void *)GetProcAddress(hkernel32, "CreateJobObjectW");
    pAssignProcessToJobObject = (void *)GetProcAddress(hkernel32, "AssignProcessToJobObject");
    pIsProcessInJob = (void *)GetProcAddress(hkernel32, "IsProcessInJob");
    pTerminateJobObject = (void *)GetProcAddress(hkernel32, "TerminateJobObject");
    pQueryInformationJobObject = (void *)GetProcAddress(hkernel32, "QueryInformationJobObject");
    pSetInformationJobObject = (void *)GetProcAddress(hkernel32, "SetInformationJobObject");
    pCreateIoCompletionPort = (void *)GetProcAddress(hkernel32, "CreateIoCompletionPort");
    pGetNumaProcessorNode = (void *)GetProcAddress(hkernel32, "GetNumaProcessorNode");
    pProcessIdToSessionId = (void *)GetProcAddress(hkernel32, "ProcessIdToSessionId");
    pWTSGetActiveConsoleSessionId = (void *)GetProcAddress(hkernel32, "WTSGetActiveConsoleSessionId");
    pCreateToolhelp32Snapshot = (void *)GetProcAddress(hkernel32, "CreateToolhelp32Snapshot");
    pProcess32First = (void *)GetProcAddress(hkernel32, "Process32First");
    pProcess32Next = (void *)GetProcAddress(hkernel32, "Process32Next");
    pThread32First = (void *)GetProcAddress(hkernel32, "Thread32First");
    pThread32Next = (void *)GetProcAddress(hkernel32, "Thread32Next");
    pGetLogicalProcessorInformationEx = (void *)GetProcAddress(hkernel32, "GetLogicalProcessorInformationEx");

    return TRUE;
}

/******************************************************************
 *		get_file_name
 *
 * generates an absolute file_name for temporary file
 *
 */
static void     get_file_name(char* buf)
{
    char        path[MAX_PATH];

    buf[0] = '\0';
    GetTempPathA(sizeof(path), path);
    GetTempFileNameA(path, "wt", 0, buf);
}

/******************************************************************
 *		static void     childPrintf
 *
 */
static void WINETEST_PRINTF_ATTR(2,3) childPrintf(HANDLE h, const char* fmt, ...)
{
    va_list     valist;
    char        buffer[1024+4*MAX_LISTED_ENV_VAR];
    DWORD       w;

    va_start(valist, fmt);
    vsprintf(buffer, fmt, valist);
    va_end(valist);
    WriteFile(h, buffer, strlen(buffer), &w, NULL);
}


/******************************************************************
 *		doChild
 *
 * output most of the information in the child process
 */
static void     doChild(const char* file, const char* option)
{
    STARTUPINFOA        siA;
    STARTUPINFOW        siW;
    int                 i;
    char                *ptrA, *ptrA_save;
    WCHAR               *ptrW, *ptrW_save;
    char                bufA[MAX_PATH];
    WCHAR               bufW[MAX_PATH];
    HANDLE              hFile = CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, 0);
    HANDLE              snapshot;
    PROCESSENTRY32      pe;
    BOOL ret;

    if (hFile == INVALID_HANDLE_VALUE) return;

    /* output of startup info (Ansi) */
    GetStartupInfoA(&siA);
    childPrintf(hFile,
                "[StartupInfoA]\ncb=%08u\nlpDesktop=%s\nlpTitle=%s\n"
                "dwX=%u\ndwY=%u\ndwXSize=%u\ndwYSize=%u\n"
                "dwXCountChars=%u\ndwYCountChars=%u\ndwFillAttribute=%u\n"
                "dwFlags=%u\nwShowWindow=%u\n"
                "hStdInput=%lu\nhStdOutput=%lu\nhStdError=%lu\n\n",
                siA.cb, encodeA(siA.lpDesktop), encodeA(siA.lpTitle),
                siA.dwX, siA.dwY, siA.dwXSize, siA.dwYSize,
                siA.dwXCountChars, siA.dwYCountChars, siA.dwFillAttribute,
                siA.dwFlags, siA.wShowWindow,
                (DWORD_PTR)siA.hStdInput, (DWORD_PTR)siA.hStdOutput, (DWORD_PTR)siA.hStdError);

    if (pNtCurrentTeb)
    {
        RTL_USER_PROCESS_PARAMETERS *params = pNtCurrentTeb()->Peb->ProcessParameters;

        /* check the console handles in the TEB */
        childPrintf(hFile, "[TEB]\nhStdInput=%lu\nhStdOutput=%lu\nhStdError=%lu\n\n",
                    (DWORD_PTR)params->hStdInput, (DWORD_PTR)params->hStdOutput,
                    (DWORD_PTR)params->hStdError);
    }

    /* since GetStartupInfoW is only implemented in win2k,
     * zero out before calling so we can notice the difference
     */
    memset(&siW, 0, sizeof(siW));
    GetStartupInfoW(&siW);
    childPrintf(hFile,
                "[StartupInfoW]\ncb=%08u\nlpDesktop=%s\nlpTitle=%s\n"
                "dwX=%u\ndwY=%u\ndwXSize=%u\ndwYSize=%u\n"
                "dwXCountChars=%u\ndwYCountChars=%u\ndwFillAttribute=%u\n"
                "dwFlags=%u\nwShowWindow=%u\n"
                "hStdInput=%lu\nhStdOutput=%lu\nhStdError=%lu\n\n",
                siW.cb, encodeW(siW.lpDesktop), encodeW(siW.lpTitle),
                siW.dwX, siW.dwY, siW.dwXSize, siW.dwYSize,
                siW.dwXCountChars, siW.dwYCountChars, siW.dwFillAttribute,
                siW.dwFlags, siW.wShowWindow,
                (DWORD_PTR)siW.hStdInput, (DWORD_PTR)siW.hStdOutput, (DWORD_PTR)siW.hStdError);

    /* Arguments */
    childPrintf(hFile, "[Arguments]\nargcA=%d\n", myARGC);
    for (i = 0; i < myARGC; i++)
    {
        childPrintf(hFile, "argvA%d=%s\n", i, encodeA(myARGV[i]));
    }
    childPrintf(hFile, "CommandLineA=%s\n", encodeA(GetCommandLineA()));
    childPrintf(hFile, "CommandLineW=%s\n\n", encodeW(GetCommandLineW()));

    /* output toolhelp information */
    snapshot = pCreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    ok(snapshot != INVALID_HANDLE_VALUE, "CreateToolhelp32Snapshot failed %u\n", GetLastError());
    memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(pe);
    if (pProcess32First(snapshot, &pe))
    {
        while (pe.th32ProcessID != GetCurrentProcessId())
            if (!pProcess32Next(snapshot, &pe)) break;
    }
    CloseHandle(snapshot);
    ok(pe.th32ProcessID == GetCurrentProcessId(), "failed to find current process in snapshot\n");
    childPrintf(hFile,
                "[Toolhelp]\ncntUsage=%u\nth32DefaultHeapID=%lu\n"
                "th32ModuleID=%u\ncntThreads=%u\nth32ParentProcessID=%u\n"
                "pcPriClassBase=%u\ndwFlags=%u\nszExeFile=%s\n\n",
                pe.cntUsage, pe.th32DefaultHeapID, pe.th32ModuleID,
                pe.cntThreads, pe.th32ParentProcessID, pe.pcPriClassBase,
                pe.dwFlags, encodeA(pe.szExeFile));

    /* output of environment (Ansi) */
    ptrA_save = ptrA = GetEnvironmentStringsA();
    if (ptrA)
    {
        char    env_var[MAX_LISTED_ENV_VAR];

        childPrintf(hFile, "[EnvironmentA]\n");
        i = 0;
        while (*ptrA)
        {
            lstrcpynA(env_var, ptrA, MAX_LISTED_ENV_VAR);
            childPrintf(hFile, "env%d=%s\n", i, encodeA(env_var));
            i++;
            ptrA += strlen(ptrA) + 1;
        }
        childPrintf(hFile, "len=%d\n\n", i);
        FreeEnvironmentStringsA(ptrA_save);
    }

    /* output of environment (Unicode) */
    ptrW_save = ptrW = GetEnvironmentStringsW();
    if (ptrW)
    {
        WCHAR   env_var[MAX_LISTED_ENV_VAR];

        childPrintf(hFile, "[EnvironmentW]\n");
        i = 0;
        while (*ptrW)
        {
            lstrcpynW(env_var, ptrW, MAX_LISTED_ENV_VAR - 1);
            env_var[MAX_LISTED_ENV_VAR - 1] = '\0';
            childPrintf(hFile, "env%d=%s\n", i, encodeW(env_var));
            i++;
            ptrW += lstrlenW(ptrW) + 1;
        }
        childPrintf(hFile, "len=%d\n\n", i);
        FreeEnvironmentStringsW(ptrW_save);
    }

    childPrintf(hFile, "[Misc]\n");
    if (GetCurrentDirectoryA(sizeof(bufA), bufA))
        childPrintf(hFile, "CurrDirA=%s\n", encodeA(bufA));
    if (GetCurrentDirectoryW(sizeof(bufW) / sizeof(bufW[0]), bufW))
        childPrintf(hFile, "CurrDirW=%s\n", encodeW(bufW));
    childPrintf(hFile, "\n");

    if (option && strcmp(option, "console") == 0)
    {
        CONSOLE_SCREEN_BUFFER_INFO	sbi;
        HANDLE hConIn  = GetStdHandle(STD_INPUT_HANDLE);
        HANDLE hConOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD modeIn, modeOut;

        childPrintf(hFile, "[Console]\n");
        if (GetConsoleScreenBufferInfo(hConOut, &sbi))
        {
            childPrintf(hFile, "SizeX=%d\nSizeY=%d\nCursorX=%d\nCursorY=%d\nAttributes=%d\n",
                        sbi.dwSize.X, sbi.dwSize.Y, sbi.dwCursorPosition.X, sbi.dwCursorPosition.Y, sbi.wAttributes);
            childPrintf(hFile, "winLeft=%d\nwinTop=%d\nwinRight=%d\nwinBottom=%d\n",
                        sbi.srWindow.Left, sbi.srWindow.Top, sbi.srWindow.Right, sbi.srWindow.Bottom);
            childPrintf(hFile, "maxWinWidth=%d\nmaxWinHeight=%d\n",
                        sbi.dwMaximumWindowSize.X, sbi.dwMaximumWindowSize.Y);
        }
        childPrintf(hFile, "InputCP=%d\nOutputCP=%d\n",
                    GetConsoleCP(), GetConsoleOutputCP());
        if (GetConsoleMode(hConIn, &modeIn))
            childPrintf(hFile, "InputMode=%u\n", modeIn);
        if (GetConsoleMode(hConOut, &modeOut))
            childPrintf(hFile, "OutputMode=%u\n", modeOut);

        /* now that we have written all relevant information, let's change it */
        SetLastError(0xdeadbeef);
        ret = SetConsoleCP(1252);
        if (!ret && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
        {
            win_skip("Setting the codepage is not implemented\n");
        }
        else
        {
            ok(ret, "Setting CP\n");
            ok(SetConsoleOutputCP(1252), "Setting SB CP\n");
        }

        ret = SetConsoleMode(hConIn, modeIn ^ 1);
        ok( ret, "Setting mode (%d)\n", GetLastError());
        ret = SetConsoleMode(hConOut, modeOut ^ 1);
        ok( ret, "Setting mode (%d)\n", GetLastError());
        sbi.dwCursorPosition.X ^= 1;
        sbi.dwCursorPosition.Y ^= 1;
        ret = SetConsoleCursorPosition(hConOut, sbi.dwCursorPosition);
        ok( ret, "Setting cursor position (%d)\n", GetLastError());
    }
    if (option && strcmp(option, "stdhandle") == 0)
    {
        HANDLE hStdIn  = GetStdHandle(STD_INPUT_HANDLE);
        HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

        if (hStdIn != INVALID_HANDLE_VALUE || hStdOut != INVALID_HANDLE_VALUE)
        {
            char buf[1024];
            DWORD r, w;

            ok(ReadFile(hStdIn, buf, sizeof(buf), &r, NULL) && r > 0, "Reading message from input pipe\n");
            childPrintf(hFile, "[StdHandle]\nmsg=%s\n\n", encodeA(buf));
            ok(WriteFile(hStdOut, buf, r, &w, NULL) && w == r, "Writing message to output pipe\n");
        }
    }

    if (option && strcmp(option, "exit_code") == 0)
    {
        childPrintf(hFile, "[ExitCode]\nvalue=%d\n\n", 123);
        CloseHandle(hFile);
        ExitProcess(123);
    }

    CloseHandle(hFile);
}

static char* getChildString(const char* sect, const char* key)
{
    char        buf[1024+4*MAX_LISTED_ENV_VAR];
    char*       ret;

    GetPrivateProfileStringA(sect, key, "-", buf, sizeof(buf), resfile);
    if (buf[0] == '\0' || (buf[0] == '-' && buf[1] == '\0')) return NULL;
    assert(!(strlen(buf) & 1));
    ret = decodeA(buf);
    return ret;
}

static WCHAR* getChildStringW(const char* sect, const char* key)
{
    char        buf[1024+4*MAX_LISTED_ENV_VAR];
    WCHAR*       ret;

    GetPrivateProfileStringA(sect, key, "-", buf, sizeof(buf), resfile);
    if (buf[0] == '\0' || (buf[0] == '-' && buf[1] == '\0')) return NULL;
    assert(!(strlen(buf) & 1));
    ret = decodeW(buf);
    return ret;
}

/* FIXME: this may be moved to the wtmain.c file, because it may be needed by
 * others... (windows uses stricmp while Un*x uses strcasecmp...)
 */
static int wtstrcasecmp(const char* p1, const char* p2)
{
    char c1, c2;

    c1 = c2 = '@';
    while (c1 == c2 && c1)
    {
        c1 = *p1++; c2 = *p2++;
        if (c1 != c2)
        {
            c1 = toupper(c1); c2 = toupper(c2);
        }
    }
    return c1 - c2;
}

static int strCmp(const char* s1, const char* s2, BOOL sensitive)
{
    if (!s1 && !s2) return 0;
    if (!s2) return -1;
    if (!s1) return 1;
    return (sensitive) ? strcmp(s1, s2) : wtstrcasecmp(s1, s2);
}

static void ok_child_string( int line, const char *sect, const char *key,
                             const char *expect, int sensitive )
{
    char* result = getChildString( sect, key );
    ok_(__FILE__, line)( strCmp(result, expect, sensitive) == 0, "%s:%s expected '%s', got '%s'\n",
                         sect, key, expect ? expect : "(null)", result );
}

static void ok_child_stringWA( int line, const char *sect, const char *key,
                             const char *expect, int sensitive )
{
    WCHAR* expectW;
    CHAR* resultA;
    DWORD len;
    WCHAR* result = getChildStringW( sect, key );

    len = MultiByteToWideChar( CP_ACP, 0, expect, -1, NULL, 0);
    expectW = HeapAlloc(GetProcessHeap(),0,len*sizeof(WCHAR));
    MultiByteToWideChar( CP_ACP, 0, expect, -1, expectW, len);

    len = WideCharToMultiByte( CP_ACP, 0, result, -1, NULL, 0, NULL, NULL);
    resultA = HeapAlloc(GetProcessHeap(),0,len*sizeof(CHAR));
    WideCharToMultiByte( CP_ACP, 0, result, -1, resultA, len, NULL, NULL);

    if (sensitive)
        ok_(__FILE__, line)( lstrcmpW(result, expectW) == 0, "%s:%s expected '%s', got '%s'\n",
                         sect, key, expect ? expect : "(null)", resultA );
    else
        ok_(__FILE__, line)( lstrcmpiW(result, expectW) == 0, "%s:%s expected '%s', got '%s'\n",
                         sect, key, expect ? expect : "(null)", resultA );
    HeapFree(GetProcessHeap(),0,expectW);
    HeapFree(GetProcessHeap(),0,resultA);
}

static void ok_child_int( int line, const char *sect, const char *key, UINT expect )
{
    UINT result = GetPrivateProfileIntA( sect, key, !expect, resfile );
    ok_(__FILE__, line)( result == expect, "%s:%s expected %u, but got %u\n", sect, key, expect, result );
}

#define okChildString(sect, key, expect) ok_child_string(__LINE__, (sect), (key), (expect), 1 )
#define okChildIString(sect, key, expect) ok_child_string(__LINE__, (sect), (key), (expect), 0 )
#define okChildStringWA(sect, key, expect) ok_child_stringWA(__LINE__, (sect), (key), (expect), 1 )
#define okChildInt(sect, key, expect) ok_child_int(__LINE__, (sect), (key), (expect))

static void test_Startup(void)
{
    char                buffer[MAX_PATH];
    PROCESS_INFORMATION	info;
    STARTUPINFOA	startup,si;
    char *result;
    static CHAR title[]   = "I'm the title string",
                desktop[] = "winsta0\\default",
                empty[]   = "";

    /* let's start simplistic */
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info), "CreateProcess\n");
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    GetStartupInfoA(&si);
    okChildInt("StartupInfoA", "cb", startup.cb);
    okChildString("StartupInfoA", "lpDesktop", si.lpDesktop);
    okChildInt("StartupInfoA", "dwX", startup.dwX);
    okChildInt("StartupInfoA", "dwY", startup.dwY);
    okChildInt("StartupInfoA", "dwXSize", startup.dwXSize);
    okChildInt("StartupInfoA", "dwYSize", startup.dwYSize);
    okChildInt("StartupInfoA", "dwXCountChars", startup.dwXCountChars);
    okChildInt("StartupInfoA", "dwYCountChars", startup.dwYCountChars);
    okChildInt("StartupInfoA", "dwFillAttribute", startup.dwFillAttribute);
    okChildInt("StartupInfoA", "dwFlags", startup.dwFlags);
    okChildInt("StartupInfoA", "wShowWindow", startup.wShowWindow);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    /* not so simplistic now */
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    startup.lpTitle = title;
    startup.lpDesktop = desktop;
    startup.dwXCountChars = 0x12121212;
    startup.dwYCountChars = 0x23232323;
    startup.dwX = 0x34343434;
    startup.dwY = 0x45454545;
    startup.dwXSize = 0x56565656;
    startup.dwYSize = 0x67676767;
    startup.dwFillAttribute = 0xA55A;

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info), "CreateProcess\n");
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    okChildInt("StartupInfoA", "cb", startup.cb);
    okChildString("StartupInfoA", "lpDesktop", startup.lpDesktop);
    okChildString("StartupInfoA", "lpTitle", startup.lpTitle);
    okChildInt("StartupInfoA", "dwX", startup.dwX);
    okChildInt("StartupInfoA", "dwY", startup.dwY);
    okChildInt("StartupInfoA", "dwXSize", startup.dwXSize);
    okChildInt("StartupInfoA", "dwYSize", startup.dwYSize);
    okChildInt("StartupInfoA", "dwXCountChars", startup.dwXCountChars);
    okChildInt("StartupInfoA", "dwYCountChars", startup.dwYCountChars);
    okChildInt("StartupInfoA", "dwFillAttribute", startup.dwFillAttribute);
    okChildInt("StartupInfoA", "dwFlags", startup.dwFlags);
    okChildInt("StartupInfoA", "wShowWindow", startup.wShowWindow);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    /* not so simplistic now */
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    startup.lpTitle = title;
    startup.lpDesktop = NULL;
    startup.dwXCountChars = 0x12121212;
    startup.dwYCountChars = 0x23232323;
    startup.dwX = 0x34343434;
    startup.dwY = 0x45454545;
    startup.dwXSize = 0x56565656;
    startup.dwYSize = 0x67676767;
    startup.dwFillAttribute = 0xA55A;

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info), "CreateProcess\n");
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    okChildInt("StartupInfoA", "cb", startup.cb);
    okChildString("StartupInfoA", "lpDesktop", si.lpDesktop);
    okChildString("StartupInfoA", "lpTitle", startup.lpTitle);
    okChildInt("StartupInfoA", "dwX", startup.dwX);
    okChildInt("StartupInfoA", "dwY", startup.dwY);
    okChildInt("StartupInfoA", "dwXSize", startup.dwXSize);
    okChildInt("StartupInfoA", "dwYSize", startup.dwYSize);
    okChildInt("StartupInfoA", "dwXCountChars", startup.dwXCountChars);
    okChildInt("StartupInfoA", "dwYCountChars", startup.dwYCountChars);
    okChildInt("StartupInfoA", "dwFillAttribute", startup.dwFillAttribute);
    okChildInt("StartupInfoA", "dwFlags", startup.dwFlags);
    okChildInt("StartupInfoA", "wShowWindow", startup.wShowWindow);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    /* not so simplistic now */
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    startup.lpTitle = title;
    startup.lpDesktop = empty;
    startup.dwXCountChars = 0x12121212;
    startup.dwYCountChars = 0x23232323;
    startup.dwX = 0x34343434;
    startup.dwY = 0x45454545;
    startup.dwXSize = 0x56565656;
    startup.dwYSize = 0x67676767;
    startup.dwFillAttribute = 0xA55A;

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info), "CreateProcess\n");
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    okChildInt("StartupInfoA", "cb", startup.cb);
    okChildString("StartupInfoA", "lpDesktop", startup.lpDesktop);
    okChildString("StartupInfoA", "lpTitle", startup.lpTitle);
    okChildInt("StartupInfoA", "dwX", startup.dwX);
    okChildInt("StartupInfoA", "dwY", startup.dwY);
    okChildInt("StartupInfoA", "dwXSize", startup.dwXSize);
    okChildInt("StartupInfoA", "dwYSize", startup.dwYSize);
    okChildInt("StartupInfoA", "dwXCountChars", startup.dwXCountChars);
    okChildInt("StartupInfoA", "dwYCountChars", startup.dwYCountChars);
    okChildInt("StartupInfoA", "dwFillAttribute", startup.dwFillAttribute);
    okChildInt("StartupInfoA", "dwFlags", startup.dwFlags);
    okChildInt("StartupInfoA", "wShowWindow", startup.wShowWindow);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    /* not so simplistic now */
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    startup.lpTitle = NULL;
    startup.lpDesktop = desktop;
    startup.dwXCountChars = 0x12121212;
    startup.dwYCountChars = 0x23232323;
    startup.dwX = 0x34343434;
    startup.dwY = 0x45454545;
    startup.dwXSize = 0x56565656;
    startup.dwYSize = 0x67676767;
    startup.dwFillAttribute = 0xA55A;

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info), "CreateProcess\n");
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    okChildInt("StartupInfoA", "cb", startup.cb);
    okChildString("StartupInfoA", "lpDesktop", startup.lpDesktop);
    result = getChildString( "StartupInfoA", "lpTitle" );
    ok( broken(!result) || (result && !strCmp( result, selfname, 0 )),
        "expected '%s' or null, got '%s'\n", selfname, result );
    okChildInt("StartupInfoA", "dwX", startup.dwX);
    okChildInt("StartupInfoA", "dwY", startup.dwY);
    okChildInt("StartupInfoA", "dwXSize", startup.dwXSize);
    okChildInt("StartupInfoA", "dwYSize", startup.dwYSize);
    okChildInt("StartupInfoA", "dwXCountChars", startup.dwXCountChars);
    okChildInt("StartupInfoA", "dwYCountChars", startup.dwYCountChars);
    okChildInt("StartupInfoA", "dwFillAttribute", startup.dwFillAttribute);
    okChildInt("StartupInfoA", "dwFlags", startup.dwFlags);
    okChildInt("StartupInfoA", "wShowWindow", startup.wShowWindow);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    /* not so simplistic now */
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    startup.lpTitle = empty;
    startup.lpDesktop = desktop;
    startup.dwXCountChars = 0x12121212;
    startup.dwYCountChars = 0x23232323;
    startup.dwX = 0x34343434;
    startup.dwY = 0x45454545;
    startup.dwXSize = 0x56565656;
    startup.dwYSize = 0x67676767;
    startup.dwFillAttribute = 0xA55A;

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info), "CreateProcess\n");
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    okChildInt("StartupInfoA", "cb", startup.cb);
    okChildString("StartupInfoA", "lpDesktop", startup.lpDesktop);
    okChildString("StartupInfoA", "lpTitle", startup.lpTitle);
    okChildInt("StartupInfoA", "dwX", startup.dwX);
    okChildInt("StartupInfoA", "dwY", startup.dwY);
    okChildInt("StartupInfoA", "dwXSize", startup.dwXSize);
    okChildInt("StartupInfoA", "dwYSize", startup.dwYSize);
    okChildInt("StartupInfoA", "dwXCountChars", startup.dwXCountChars);
    okChildInt("StartupInfoA", "dwYCountChars", startup.dwYCountChars);
    okChildInt("StartupInfoA", "dwFillAttribute", startup.dwFillAttribute);
    okChildInt("StartupInfoA", "dwFlags", startup.dwFlags);
    okChildInt("StartupInfoA", "wShowWindow", startup.wShowWindow);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    /* not so simplistic now */
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    startup.lpTitle = empty;
    startup.lpDesktop = empty;
    startup.dwXCountChars = 0x12121212;
    startup.dwYCountChars = 0x23232323;
    startup.dwX = 0x34343434;
    startup.dwY = 0x45454545;
    startup.dwXSize = 0x56565656;
    startup.dwYSize = 0x67676767;
    startup.dwFillAttribute = 0xA55A;

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info), "CreateProcess\n");
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    okChildInt("StartupInfoA", "cb", startup.cb);
    okChildString("StartupInfoA", "lpDesktop", startup.lpDesktop);
    okChildString("StartupInfoA", "lpTitle", startup.lpTitle);
    okChildInt("StartupInfoA", "dwX", startup.dwX);
    okChildInt("StartupInfoA", "dwY", startup.dwY);
    okChildInt("StartupInfoA", "dwXSize", startup.dwXSize);
    okChildInt("StartupInfoA", "dwYSize", startup.dwYSize);
    okChildInt("StartupInfoA", "dwXCountChars", startup.dwXCountChars);
    okChildInt("StartupInfoA", "dwYCountChars", startup.dwYCountChars);
    okChildInt("StartupInfoA", "dwFillAttribute", startup.dwFillAttribute);
    okChildInt("StartupInfoA", "dwFlags", startup.dwFlags);
    okChildInt("StartupInfoA", "wShowWindow", startup.wShowWindow);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    /* TODO: test for A/W and W/A and W/W */
}

static void test_CommandLine(void)
{
    char                buffer[MAX_PATH], fullpath[MAX_PATH], *lpFilePart, *p;
    char                buffer2[MAX_PATH];
    PROCESS_INFORMATION	info;
    STARTUPINFOA	startup;
    BOOL                ret;

    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    /* the basics */
    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\" \"C:\\Program Files\\my nice app.exe\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info), "CreateProcess\n");
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    okChildInt("Arguments", "argcA", 5);
    okChildString("Arguments", "argvA4", "C:\\Program Files\\my nice app.exe");
    okChildString("Arguments", "argvA5", NULL);
    okChildString("Arguments", "CommandLineA", buffer);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    /* from François */
    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\" \"a\\\"b\\\\\" c\\\" d", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info), "CreateProcess\n");
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    okChildInt("Arguments", "argcA", 7);
    okChildString("Arguments", "argvA4", "a\"b\\");
    okChildString("Arguments", "argvA5", "c\"");
    okChildString("Arguments", "argvA6", "d");
    okChildString("Arguments", "argvA7", NULL);
    okChildString("Arguments", "CommandLineA", buffer);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    /* Test for Bug1330 to show that XP doesn't change '/' to '\\' in argv[0]*/
    get_file_name(resfile);
    /* Use exename to avoid buffer containing things like 'C:' */
    sprintf(buffer, "./%s tests/process.c dump \"%s\" \"a\\\"b\\\\\" c\\\" d", exename, resfile);
    SetLastError(0xdeadbeef);
    ret = CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info);
    ok(ret, "CreateProcess (%s) failed : %d\n", buffer, GetLastError());
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);
    sprintf(buffer, "./%s", exename);
    okChildString("Arguments", "argvA0", buffer);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    get_file_name(resfile);
    /* Use exename to avoid buffer containing things like 'C:' */
    sprintf(buffer, ".\\%s tests/process.c dump \"%s\" \"a\\\"b\\\\\" c\\\" d", exename, resfile);
    SetLastError(0xdeadbeef);
    ret = CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info);
    ok(ret, "CreateProcess (%s) failed : %d\n", buffer, GetLastError());
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);
    sprintf(buffer, ".\\%s", exename);
    okChildString("Arguments", "argvA0", buffer);
    release_memory();
    assert(DeleteFileA(resfile) != 0);
    
    get_file_name(resfile);
    GetFullPathNameA(selfname, MAX_PATH, fullpath, &lpFilePart);
    assert ( lpFilePart != 0);
    *(lpFilePart -1 ) = 0;
    p = strrchr(fullpath, '\\');
    /* Use exename to avoid buffer containing things like 'C:' */
    if (p) sprintf(buffer, "..%s/%s tests/process.c dump \"%s\" \"a\\\"b\\\\\" c\\\" d", p, exename, resfile);
    else sprintf(buffer, "./%s tests/process.c dump \"%s\" \"a\\\"b\\\\\" c\\\" d", exename, resfile);
    SetLastError(0xdeadbeef);
    ret = CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info);
    ok(ret, "CreateProcess (%s) failed : %d\n", buffer, GetLastError());
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);
    if (p) sprintf(buffer, "..%s/%s", p, exename);
    else sprintf(buffer, "./%s", exename);
    okChildString("Arguments", "argvA0", buffer);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    /* Using AppName */
    get_file_name(resfile);
    GetFullPathNameA(selfname, MAX_PATH, fullpath, &lpFilePart);
    assert ( lpFilePart != 0);
    *(lpFilePart -1 ) = 0;
    p = strrchr(fullpath, '\\');
    /* Use exename to avoid buffer containing things like 'C:' */
    if (p) sprintf(buffer, "..%s/%s", p, exename);
    else sprintf(buffer, "./%s", exename);
    sprintf(buffer2, "dummy tests/process.c dump \"%s\" \"a\\\"b\\\\\" c\\\" d", resfile);
    SetLastError(0xdeadbeef);
    ret = CreateProcessA(buffer, buffer2, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info);
    ok(ret, "CreateProcess (%s) failed : %d\n", buffer, GetLastError());
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);
    sprintf(buffer, "tests/process.c dump %s", resfile);
    okChildString("Arguments", "argvA0", "dummy");
    okChildString("Arguments", "CommandLineA", buffer2);
    okChildStringWA("Arguments", "CommandLineW", buffer2);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    if (0) /* Test crashes on NT-based Windows. */
    {
        /* Test NULL application name and command line parameters. */
        SetLastError(0xdeadbeef);
        ret = CreateProcessA(NULL, NULL, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info);
        ok(!ret, "CreateProcessA unexpectedly succeeded\n");
        ok(GetLastError() == ERROR_INVALID_PARAMETER,
           "Expected ERROR_INVALID_PARAMETER, got %d\n", GetLastError());
    }

    buffer[0] = '\0';

    /* Test empty application name parameter. */
    SetLastError(0xdeadbeef);
    ret = CreateProcessA(buffer, NULL, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info);
    ok(!ret, "CreateProcessA unexpectedly succeeded\n");
    ok(GetLastError() == ERROR_PATH_NOT_FOUND ||
       broken(GetLastError() == ERROR_FILE_NOT_FOUND) /* Win9x/WinME */ ||
       broken(GetLastError() == ERROR_ACCESS_DENIED) /* Win98 */,
       "Expected ERROR_PATH_NOT_FOUND, got %d\n", GetLastError());

    buffer2[0] = '\0';

    /* Test empty application name and command line parameters. */
    SetLastError(0xdeadbeef);
    ret = CreateProcessA(buffer, buffer2, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info);
    ok(!ret, "CreateProcessA unexpectedly succeeded\n");
    ok(GetLastError() == ERROR_PATH_NOT_FOUND ||
       broken(GetLastError() == ERROR_FILE_NOT_FOUND) /* Win9x/WinME */ ||
       broken(GetLastError() == ERROR_ACCESS_DENIED) /* Win98 */,
       "Expected ERROR_PATH_NOT_FOUND, got %d\n", GetLastError());

    /* Test empty command line parameter. */
    SetLastError(0xdeadbeef);
    ret = CreateProcessA(NULL, buffer2, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info);
    ok(!ret, "CreateProcessA unexpectedly succeeded\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND ||
       GetLastError() == ERROR_PATH_NOT_FOUND /* NT4 */ ||
       GetLastError() == ERROR_BAD_PATHNAME /* Win98 */ ||
       GetLastError() == ERROR_INVALID_PARAMETER /* Win7 */,
       "Expected ERROR_FILE_NOT_FOUND, got %d\n", GetLastError());

    strcpy(buffer, "doesnotexist.exe");
    strcpy(buffer2, "does not exist.exe");

    /* Test nonexistent application name. */
    SetLastError(0xdeadbeef);
    ret = CreateProcessA(buffer, NULL, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info);
    ok(!ret, "CreateProcessA unexpectedly succeeded\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "Expected ERROR_FILE_NOT_FOUND, got %d\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = CreateProcessA(buffer2, NULL, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info);
    ok(!ret, "CreateProcessA unexpectedly succeeded\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "Expected ERROR_FILE_NOT_FOUND, got %d\n", GetLastError());

    /* Test nonexistent command line parameter. */
    SetLastError(0xdeadbeef);
    ret = CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info);
    ok(!ret, "CreateProcessA unexpectedly succeeded\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "Expected ERROR_FILE_NOT_FOUND, got %d\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = CreateProcessA(NULL, buffer2, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info);
    ok(!ret, "CreateProcessA unexpectedly succeeded\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "Expected ERROR_FILE_NOT_FOUND, got %d\n", GetLastError());
}

static void test_Directory(void)
{
    char                buffer[MAX_PATH];
    PROCESS_INFORMATION	info;
    STARTUPINFOA	startup;
    char windir[MAX_PATH];
    static CHAR cmdline[] = "winver.exe";

    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    /* the basics */
    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);
    GetWindowsDirectoryA( windir, sizeof(windir) );
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, windir, &startup, &info), "CreateProcess\n");
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    okChildIString("Misc", "CurrDirA", windir);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    /* search PATH for the exe if directory is NULL */
    ok(CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info), "CreateProcess\n");
    ok(TerminateProcess(info.hProcess, 0), "Child process termination\n");

    /* if any directory is provided, don't search PATH, error on bad directory */
    SetLastError(0xdeadbeef);
    memset(&info, 0, sizeof(info));
    ok(!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0L,
                       NULL, "non\\existent\\directory", &startup, &info), "CreateProcess\n");
    ok(GetLastError() == ERROR_DIRECTORY, "Expected ERROR_DIRECTORY, got %d\n", GetLastError());
    ok(!TerminateProcess(info.hProcess, 0), "Child process should not exist\n");
}

static void test_Toolhelp(void)
{
    char                buffer[MAX_PATH];
    STARTUPINFOA        startup;
    PROCESS_INFORMATION info;
    HANDLE              process, thread, snapshot;
    PROCESSENTRY32      pe;
    THREADENTRY32       te;
    DWORD               ret;
    int                 i;

    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info), "CreateProcess failed\n");
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);

    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);
    okChildInt("Toolhelp", "cntUsage", 0);
    okChildInt("Toolhelp", "th32DefaultHeapID", 0);
    okChildInt("Toolhelp", "th32ModuleID", 0);
    okChildInt("Toolhelp", "th32ParentProcessID", GetCurrentProcessId());
    /* pcPriClassBase differs between Windows versions (either 6 or 8) */
    okChildInt("Toolhelp", "dwFlags", 0);

    release_memory();
    DeleteFileA(resfile);

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c nested \"%s\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info), "CreateProcess failed\n");
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");

    process = OpenProcess(PROCESS_ALL_ACCESS_NT4, FALSE, info.dwProcessId);
    ok(process != NULL, "OpenProcess failed %u\n", GetLastError());
    CloseHandle(process);

    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);

    for (i = 0; i < 20; i++)
    {
        SetLastError(0xdeadbeef);
        process = OpenProcess(PROCESS_ALL_ACCESS_NT4, FALSE, info.dwProcessId);
        ok(process || GetLastError() == ERROR_INVALID_PARAMETER, "OpenProcess failed %u\n", GetLastError());
        if (!process) break;
        CloseHandle(process);
        Sleep(100);
    }
    /* The following test fails randomly on some Windows versions, but Gothic 2 depends on it */
    ok(i < 20 || broken(i == 20), "process object not released\n");

    snapshot = pCreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    ok(snapshot != INVALID_HANDLE_VALUE, "CreateToolhelp32Snapshot failed %u\n", GetLastError());
    memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(pe);
    if (pProcess32First(snapshot, &pe))
    {
        while (pe.th32ParentProcessID != info.dwProcessId)
            if (!pProcess32Next(snapshot, &pe)) break;
    }
    CloseHandle(snapshot);
    ok(pe.th32ParentProcessID == info.dwProcessId, "failed to find nested child process\n");

    process = OpenProcess(PROCESS_ALL_ACCESS_NT4, FALSE, pe.th32ProcessID);
    ok(process != NULL, "OpenProcess failed %u\n", GetLastError());

    snapshot = pCreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    ok(snapshot != INVALID_HANDLE_VALUE, "CreateToolhelp32Snapshot failed %u\n", GetLastError());
    memset(&te, 0, sizeof(te));
    te.dwSize = sizeof(te);
    if (pThread32First(snapshot, &te))
    {
        while (te.th32OwnerProcessID != pe.th32ProcessID)
            if (!pThread32Next(snapshot, &te)) break;
    }
    CloseHandle(snapshot);
    ok(te.th32OwnerProcessID == pe.th32ProcessID, "failed to find suspended thread\n");

    thread = OpenThread(THREAD_ALL_ACCESS_NT4, FALSE, te.th32ThreadID);
    ok(thread != NULL, "OpenThread failed %u\n", GetLastError());
    ret = ResumeThread(thread);
    ok(ret == 1, "expected 1, got %u\n", ret);
    CloseHandle(thread);

    ok(WaitForSingleObject(process, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    CloseHandle(process);

    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);
    okChildInt("Toolhelp", "cntUsage", 0);
    okChildInt("Toolhelp", "th32DefaultHeapID", 0);
    okChildInt("Toolhelp", "th32ModuleID", 0);
    okChildInt("Toolhelp", "th32ParentProcessID", info.dwProcessId);
    /* pcPriClassBase differs between Windows versions (either 6 or 8) */
    okChildInt("Toolhelp", "dwFlags", 0);

    release_memory();
    DeleteFileA(resfile);
}

static BOOL is_str_env_drive_dir(const char* str)
{
    return str[0] == '=' && str[1] >= 'A' && str[1] <= 'Z' && str[2] == ':' &&
        str[3] == '=' && str[4] == str[1];
}

/* compared expected child's environment (in gesA) from actual
 * environment our child got
 */
static void cmpEnvironment(const char* gesA)
{
    int                 i, clen;
    const char*         ptrA;
    char*               res;
    char                key[32];
    BOOL                found;

    clen = GetPrivateProfileIntA("EnvironmentA", "len", 0, resfile);
    
    /* now look each parent env in child */
    if ((ptrA = gesA) != NULL)
    {
        while (*ptrA)
        {
            for (i = 0; i < clen; i++)
            {
                sprintf(key, "env%d", i);
                res = getChildString("EnvironmentA", key);
                if (strncmp(ptrA, res, MAX_LISTED_ENV_VAR - 1) == 0)
                    break;
            }
            found = i < clen;
            ok(found, "Parent-env string %s isn't in child process\n", ptrA);
            
            ptrA += strlen(ptrA) + 1;
            release_memory();
        }
    }
    /* and each child env in parent */
    for (i = 0; i < clen; i++)
    {
        sprintf(key, "env%d", i);
        res = getChildString("EnvironmentA", key);
        if ((ptrA = gesA) != NULL)
        {
            while (*ptrA)
            {
                if (strncmp(res, ptrA, MAX_LISTED_ENV_VAR - 1) == 0)
                    break;
                ptrA += strlen(ptrA) + 1;
            }
            if (!*ptrA) ptrA = NULL;
        }

        if (!is_str_env_drive_dir(res))
        {
            found = ptrA != NULL;
            ok(found, "Child-env string %s isn't in parent process\n", res);
        }
        /* else => should also test we get the right per drive default directory here... */
    }
}

static void test_Environment(void)
{
    char                buffer[MAX_PATH];
    PROCESS_INFORMATION	info;
    STARTUPINFOA	startup;
    char                *child_env;
    int                 child_env_len;
    char                *ptr;
    char                *ptr2;
    char                *env;
    int                 slen;

    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    /* the basics */
    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, NULL, NULL, &startup, &info), "CreateProcess\n");
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    env = GetEnvironmentStringsA();
    cmpEnvironment(env);
    release_memory();
    assert(DeleteFileA(resfile) != 0);

    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    /* the basics */
    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);

    child_env_len = 0;
    ptr = env;
    while(*ptr)
    {
        slen = strlen(ptr)+1;
        child_env_len += slen;
        ptr += slen;
    }
    /* Add space for additional environment variables */
    child_env_len += 256;
    child_env = HeapAlloc(GetProcessHeap(), 0, child_env_len);

    ptr = child_env;
    sprintf(ptr, "=%c:=%s", 'C', "C:\\FOO\\BAR");
    ptr += strlen(ptr) + 1;
    strcpy(ptr, "PATH=C:\\WINDOWS;C:\\WINDOWS\\SYSTEM;C:\\MY\\OWN\\DIR");
    ptr += strlen(ptr) + 1;
    strcpy(ptr, "FOO=BAR");
    ptr += strlen(ptr) + 1;
    strcpy(ptr, "BAR=FOOBAR");
    ptr += strlen(ptr) + 1;
    /* copy all existing variables except:
     * - WINELOADER
     * - PATH (already set above)
     * - the directory definitions (=[A-Z]:=)
     */
    for (ptr2 = env; *ptr2; ptr2 += strlen(ptr2) + 1)
    {
        if (strncmp(ptr2, "PATH=", 5) != 0 &&
            strncmp(ptr2, "WINELOADER=", 11) != 0 &&
            !is_str_env_drive_dir(ptr2))
        {
            strcpy(ptr, ptr2);
            ptr += strlen(ptr) + 1;
        }
    }
    *ptr = '\0';
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0L, child_env, NULL, &startup, &info), "CreateProcess\n");
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    cmpEnvironment(child_env);

    HeapFree(GetProcessHeap(), 0, child_env);
    FreeEnvironmentStringsA(env);
    release_memory();
    assert(DeleteFileA(resfile) != 0);
}

static  void    test_SuspendFlag(void)
{
    char                buffer[MAX_PATH];
    PROCESS_INFORMATION	info;
    STARTUPINFOA       startup, us;
    DWORD               exit_status;
    char *result;

    /* let's start simplistic */
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &startup, &info), "CreateProcess\n");

    ok(GetExitCodeThread(info.hThread, &exit_status) && exit_status == STILL_ACTIVE, "thread still running\n");
    Sleep(8000);
    ok(GetExitCodeThread(info.hThread, &exit_status) && exit_status == STILL_ACTIVE, "thread still running\n");
    ok(ResumeThread(info.hThread) == 1, "Resuming thread\n");

    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    GetStartupInfoA(&us);

    okChildInt("StartupInfoA", "cb", startup.cb);
    okChildString("StartupInfoA", "lpDesktop", us.lpDesktop);
    result = getChildString( "StartupInfoA", "lpTitle" );
    ok( broken(!result) || (result && !strCmp( result, selfname, 0 )),
        "expected '%s' or null, got '%s'\n", selfname, result );
    okChildInt("StartupInfoA", "dwX", startup.dwX);
    okChildInt("StartupInfoA", "dwY", startup.dwY);
    okChildInt("StartupInfoA", "dwXSize", startup.dwXSize);
    okChildInt("StartupInfoA", "dwYSize", startup.dwYSize);
    okChildInt("StartupInfoA", "dwXCountChars", startup.dwXCountChars);
    okChildInt("StartupInfoA", "dwYCountChars", startup.dwYCountChars);
    okChildInt("StartupInfoA", "dwFillAttribute", startup.dwFillAttribute);
    okChildInt("StartupInfoA", "dwFlags", startup.dwFlags);
    okChildInt("StartupInfoA", "wShowWindow", startup.wShowWindow);
    release_memory();
    assert(DeleteFileA(resfile) != 0);
}

static  void    test_DebuggingFlag(void)
{
    char                buffer[MAX_PATH];
    void               *processbase = NULL;
    PROCESS_INFORMATION	info;
    STARTUPINFOA       startup, us;
    DEBUG_EVENT         de;
    unsigned            dbg = 0;
    char *result;

    /* let's start simplistic */
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, DEBUG_PROCESS, NULL, NULL, &startup, &info), "CreateProcess\n");

    /* get all startup events up to the entry point break exception */
    do 
    {
        ok(WaitForDebugEvent(&de, INFINITE), "reading debug event\n");
        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
        if (!dbg)
        {
            ok(de.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT,
               "first event: %d\n", de.dwDebugEventCode);
            processbase = de.u.CreateProcessInfo.lpBaseOfImage;
        }
        if (de.dwDebugEventCode != EXCEPTION_DEBUG_EVENT) dbg++;
        ok(de.dwDebugEventCode != LOAD_DLL_DEBUG_EVENT ||
           de.u.LoadDll.lpBaseOfDll != processbase, "got LOAD_DLL for main module\n");
    } while (de.dwDebugEventCode != EXIT_PROCESS_DEBUG_EVENT);

    ok(dbg, "I have seen a debug event\n");
    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    GetStartupInfoA(&us);

    okChildInt("StartupInfoA", "cb", startup.cb);
    okChildString("StartupInfoA", "lpDesktop", us.lpDesktop);
    result = getChildString( "StartupInfoA", "lpTitle" );
    ok( broken(!result) || (result && !strCmp( result, selfname, 0 )),
        "expected '%s' or null, got '%s'\n", selfname, result );
    okChildInt("StartupInfoA", "dwX", startup.dwX);
    okChildInt("StartupInfoA", "dwY", startup.dwY);
    okChildInt("StartupInfoA", "dwXSize", startup.dwXSize);
    okChildInt("StartupInfoA", "dwYSize", startup.dwYSize);
    okChildInt("StartupInfoA", "dwXCountChars", startup.dwXCountChars);
    okChildInt("StartupInfoA", "dwYCountChars", startup.dwYCountChars);
    okChildInt("StartupInfoA", "dwFillAttribute", startup.dwFillAttribute);
    okChildInt("StartupInfoA", "dwFlags", startup.dwFlags);
    okChildInt("StartupInfoA", "wShowWindow", startup.wShowWindow);
    release_memory();
    assert(DeleteFileA(resfile) != 0);
}

static BOOL is_console(HANDLE h)
{
    return h != INVALID_HANDLE_VALUE && ((ULONG_PTR)h & 3) == 3;
}

static void test_Console(void)
{
    char                buffer[MAX_PATH];
    PROCESS_INFORMATION	info;
    STARTUPINFOA       startup, us;
    SECURITY_ATTRIBUTES sa;
    CONSOLE_SCREEN_BUFFER_INFO	sbi, sbiC;
    DWORD               modeIn, modeOut, modeInC, modeOutC;
    DWORD               cpIn, cpOut, cpInC, cpOutC;
    DWORD               w;
    HANDLE              hChildIn, hChildInInh, hChildOut, hChildOutInh, hParentIn, hParentOut;
    const char*         msg = "This is a std-handle inheritance test.";
    unsigned            msg_len;
    BOOL                run_tests = TRUE;
    char *result;

    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;
    startup.wShowWindow = SW_SHOWNORMAL;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    startup.hStdInput = CreateFileA("CONIN$", GENERIC_READ|GENERIC_WRITE, 0, &sa, OPEN_EXISTING, 0, 0);
    startup.hStdOutput = CreateFileA("CONOUT$", GENERIC_READ|GENERIC_WRITE, 0, &sa, OPEN_EXISTING, 0, 0);

    /* first, we need to be sure we're attached to a console */
    if (!is_console(startup.hStdInput) || !is_console(startup.hStdOutput))
    {
        /* we're not attached to a console, let's do it */
        AllocConsole();
        startup.hStdInput = CreateFileA("CONIN$", GENERIC_READ|GENERIC_WRITE, 0, &sa, OPEN_EXISTING, 0, 0);
        startup.hStdOutput = CreateFileA("CONOUT$", GENERIC_READ|GENERIC_WRITE, 0, &sa, OPEN_EXISTING, 0, 0);
    }
    /* now verify everything's ok */
    ok(startup.hStdInput != INVALID_HANDLE_VALUE, "Opening ConIn\n");
    ok(startup.hStdOutput != INVALID_HANDLE_VALUE, "Opening ConOut\n");
    startup.hStdError = startup.hStdOutput;

    ok(GetConsoleScreenBufferInfo(startup.hStdOutput, &sbi), "Getting sb info\n");
    ok(GetConsoleMode(startup.hStdInput, &modeIn) && 
       GetConsoleMode(startup.hStdOutput, &modeOut), "Getting console modes\n");
    cpIn = GetConsoleCP();
    cpOut = GetConsoleOutputCP();

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\" console", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &info), "CreateProcess\n");

    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    /* now get the modification the child has made, and resets parents expected values */
    ok(GetConsoleScreenBufferInfo(startup.hStdOutput, &sbiC), "Getting sb info\n");
    ok(GetConsoleMode(startup.hStdInput, &modeInC) && 
       GetConsoleMode(startup.hStdOutput, &modeOutC), "Getting console modes\n");

    SetConsoleMode(startup.hStdInput, modeIn);
    SetConsoleMode(startup.hStdOutput, modeOut);

    cpInC = GetConsoleCP();
    cpOutC = GetConsoleOutputCP();

    /* Try to set invalid CP */
    SetLastError(0xdeadbeef);
    ok(!SetConsoleCP(0), "Shouldn't succeed\n");
    ok(GetLastError()==ERROR_INVALID_PARAMETER ||
       broken(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED), /* win9x */
       "GetLastError: expecting %u got %u\n",
       ERROR_INVALID_PARAMETER, GetLastError());
    if (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
        run_tests = FALSE;


    SetLastError(0xdeadbeef);
    ok(!SetConsoleOutputCP(0), "Shouldn't succeed\n");
    ok(GetLastError()==ERROR_INVALID_PARAMETER ||
       broken(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED), /* win9x */
       "GetLastError: expecting %u got %u\n",
       ERROR_INVALID_PARAMETER, GetLastError());

    SetConsoleCP(cpIn);
    SetConsoleOutputCP(cpOut);

    GetStartupInfoA(&us);

    okChildInt("StartupInfoA", "cb", startup.cb);
    okChildString("StartupInfoA", "lpDesktop", us.lpDesktop);
    result = getChildString( "StartupInfoA", "lpTitle" );
    ok( broken(!result) || (result && !strCmp( result, selfname, 0 )),
        "expected '%s' or null, got '%s'\n", selfname, result );
    okChildInt("StartupInfoA", "dwX", startup.dwX);
    okChildInt("StartupInfoA", "dwY", startup.dwY);
    okChildInt("StartupInfoA", "dwXSize", startup.dwXSize);
    okChildInt("StartupInfoA", "dwYSize", startup.dwYSize);
    okChildInt("StartupInfoA", "dwXCountChars", startup.dwXCountChars);
    okChildInt("StartupInfoA", "dwYCountChars", startup.dwYCountChars);
    okChildInt("StartupInfoA", "dwFillAttribute", startup.dwFillAttribute);
    okChildInt("StartupInfoA", "dwFlags", startup.dwFlags);
    okChildInt("StartupInfoA", "wShowWindow", startup.wShowWindow);

    /* check child correctly inherited the console */
    okChildInt("StartupInfoA", "hStdInput", (DWORD_PTR)startup.hStdInput);
    okChildInt("StartupInfoA", "hStdOutput", (DWORD_PTR)startup.hStdOutput);
    okChildInt("StartupInfoA", "hStdError", (DWORD_PTR)startup.hStdError);
    okChildInt("Console", "SizeX", (DWORD)sbi.dwSize.X);
    okChildInt("Console", "SizeY", (DWORD)sbi.dwSize.Y);
    okChildInt("Console", "CursorX", (DWORD)sbi.dwCursorPosition.X);
    okChildInt("Console", "CursorY", (DWORD)sbi.dwCursorPosition.Y);
    okChildInt("Console", "Attributes", sbi.wAttributes);
    okChildInt("Console", "winLeft", (DWORD)sbi.srWindow.Left);
    okChildInt("Console", "winTop", (DWORD)sbi.srWindow.Top);
    okChildInt("Console", "winRight", (DWORD)sbi.srWindow.Right);
    okChildInt("Console", "winBottom", (DWORD)sbi.srWindow.Bottom);
    okChildInt("Console", "maxWinWidth", (DWORD)sbi.dwMaximumWindowSize.X);
    okChildInt("Console", "maxWinHeight", (DWORD)sbi.dwMaximumWindowSize.Y);
    okChildInt("Console", "InputCP", cpIn);
    okChildInt("Console", "OutputCP", cpOut);
    okChildInt("Console", "InputMode", modeIn);
    okChildInt("Console", "OutputMode", modeOut);

    if (run_tests)
    {
        ok(cpInC == 1252, "Wrong console CP (expected 1252 got %d/%d)\n", cpInC, cpIn);
        ok(cpOutC == 1252, "Wrong console-SB CP (expected 1252 got %d/%d)\n", cpOutC, cpOut);
    }
    else
        win_skip("Setting the codepage is not implemented\n");

    ok(modeInC == (modeIn ^ 1), "Wrong console mode\n");
    ok(modeOutC == (modeOut ^ 1), "Wrong console-SB mode\n");
    trace("cursor position(X): %d/%d\n",sbi.dwCursorPosition.X, sbiC.dwCursorPosition.X);
    ok(sbiC.dwCursorPosition.Y == (sbi.dwCursorPosition.Y ^ 1), "Wrong cursor position\n");

    release_memory();
    assert(DeleteFileA(resfile) != 0);

    ok(CreatePipe(&hParentIn, &hChildOut, NULL, 0), "Creating parent-input pipe\n");
    ok(DuplicateHandle(GetCurrentProcess(), hChildOut, GetCurrentProcess(), 
                       &hChildOutInh, 0, TRUE, DUPLICATE_SAME_ACCESS),
       "Duplicating as inheritable child-output pipe\n");
    CloseHandle(hChildOut);
 
    ok(CreatePipe(&hChildIn, &hParentOut, NULL, 0), "Creating parent-output pipe\n");
    ok(DuplicateHandle(GetCurrentProcess(), hChildIn, GetCurrentProcess(), 
                       &hChildInInh, 0, TRUE, DUPLICATE_SAME_ACCESS),
       "Duplicating as inheritable child-input pipe\n");
    CloseHandle(hChildIn); 
    
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;
    startup.wShowWindow = SW_SHOWNORMAL;
    startup.hStdInput = hChildInInh;
    startup.hStdOutput = hChildOutInh;
    startup.hStdError = hChildOutInh;

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\" stdhandle", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, TRUE, DETACHED_PROCESS, NULL, NULL, &startup, &info), "CreateProcess\n");
    ok(CloseHandle(hChildInInh), "Closing handle\n");
    ok(CloseHandle(hChildOutInh), "Closing handle\n");

    msg_len = strlen(msg) + 1;
    ok(WriteFile(hParentOut, msg, msg_len, &w, NULL), "Writing to child\n");
    ok(w == msg_len, "Should have written %u bytes, actually wrote %u\n", msg_len, w);
    memset(buffer, 0, sizeof(buffer));
    ok(ReadFile(hParentIn, buffer, sizeof(buffer), &w, NULL), "Reading from child\n");
    ok(strcmp(buffer, msg) == 0, "Should have received '%s'\n", msg);

    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    okChildString("StdHandle", "msg", msg);

    release_memory();
    assert(DeleteFileA(resfile) != 0);
}

static  void    test_ExitCode(void)
{
    char                buffer[MAX_PATH];
    PROCESS_INFORMATION	info;
    STARTUPINFOA	startup;
    DWORD               code;

    /* let's start simplistic */
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\" exit_code", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info), "CreateProcess\n");

    /* wait for child to terminate */
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    /* child process has changed result file, so let profile functions know about it */
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);

    ok(GetExitCodeProcess(info.hProcess, &code), "Getting exit code\n");
    okChildInt("ExitCode", "value", code);

    release_memory();
    assert(DeleteFileA(resfile) != 0);
}

static void test_OpenProcess(void)
{
    HANDLE hproc;
    void *addr1;
    MEMORY_BASIC_INFORMATION info;
    SIZE_T dummy, read_bytes;
    BOOL ret;

    /* not exported in all windows versions */
    if ((!pVirtualAllocEx) || (!pVirtualFreeEx)) {
        win_skip("VirtualAllocEx not found\n");
        return;
    }

    /* without PROCESS_VM_OPERATION */
    hproc = OpenProcess(PROCESS_ALL_ACCESS_NT4 & ~PROCESS_VM_OPERATION, FALSE, GetCurrentProcessId());
    ok(hproc != NULL, "OpenProcess error %d\n", GetLastError());

    SetLastError(0xdeadbeef);
    addr1 = pVirtualAllocEx(hproc, 0, 0xFFFC, MEM_RESERVE, PAGE_NOACCESS);
    ok(!addr1, "VirtualAllocEx should fail\n");
    if (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {   /* Win9x */
        CloseHandle(hproc);
        win_skip("VirtualAllocEx not implemented\n");
        return;
    }
    ok(GetLastError() == ERROR_ACCESS_DENIED, "wrong error %d\n", GetLastError());

    read_bytes = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = ReadProcessMemory(hproc, test_OpenProcess, &dummy, sizeof(dummy), &read_bytes);
    ok(ret, "ReadProcessMemory error %d\n", GetLastError());
    ok(read_bytes == sizeof(dummy), "wrong read bytes %ld\n", read_bytes);

    CloseHandle(hproc);

    hproc = OpenProcess(PROCESS_VM_OPERATION, FALSE, GetCurrentProcessId());
    ok(hproc != NULL, "OpenProcess error %d\n", GetLastError());

    addr1 = pVirtualAllocEx(hproc, 0, 0xFFFC, MEM_RESERVE, PAGE_NOACCESS);
    ok(addr1 != NULL, "VirtualAllocEx error %d\n", GetLastError());

    /* without PROCESS_QUERY_INFORMATION */
    SetLastError(0xdeadbeef);
    ok(!VirtualQueryEx(hproc, addr1, &info, sizeof(info)),
       "VirtualQueryEx without PROCESS_QUERY_INFORMATION rights should fail\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "wrong error %d\n", GetLastError());

    /* without PROCESS_VM_READ */
    read_bytes = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ok(!ReadProcessMemory(hproc, addr1, &dummy, sizeof(dummy), &read_bytes),
       "ReadProcessMemory without PROCESS_VM_READ rights should fail\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "wrong error %d\n", GetLastError());
    ok(read_bytes == 0, "wrong read bytes %ld\n", read_bytes);

    CloseHandle(hproc);

    hproc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());

    memset(&info, 0xcc, sizeof(info));
    read_bytes = VirtualQueryEx(hproc, addr1, &info, sizeof(info));
    ok(read_bytes == sizeof(info), "VirtualQueryEx error %d\n", GetLastError());

    ok(info.BaseAddress == addr1, "%p != %p\n", info.BaseAddress, addr1);
    ok(info.AllocationBase == addr1, "%p != %p\n", info.AllocationBase, addr1);
    ok(info.AllocationProtect == PAGE_NOACCESS, "%x != PAGE_NOACCESS\n", info.AllocationProtect);
    ok(info.RegionSize == 0x10000, "%lx != 0x10000\n", info.RegionSize);
    ok(info.State == MEM_RESERVE, "%x != MEM_RESERVE\n", info.State);
    /* NT reports Protect == 0 for a not committed memory block */
    ok(info.Protect == 0 /* NT */ ||
       info.Protect == PAGE_NOACCESS, /* Win9x */
        "%x != PAGE_NOACCESS\n", info.Protect);
    ok(info.Type == MEM_PRIVATE, "%x != MEM_PRIVATE\n", info.Type);

    SetLastError(0xdeadbeef);
    ok(!pVirtualFreeEx(hproc, addr1, 0, MEM_RELEASE),
       "VirtualFreeEx without PROCESS_VM_OPERATION rights should fail\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "wrong error %d\n", GetLastError());

    CloseHandle(hproc);

    hproc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    if (hproc)
    {
        SetLastError(0xdeadbeef);
        memset(&info, 0xcc, sizeof(info));
        read_bytes = VirtualQueryEx(hproc, addr1, &info, sizeof(info));
        if (read_bytes) /* win8 */
        {
            ok(read_bytes == sizeof(info), "VirtualQueryEx error %d\n", GetLastError());
            ok(info.BaseAddress == addr1, "%p != %p\n", info.BaseAddress, addr1);
            ok(info.AllocationBase == addr1, "%p != %p\n", info.AllocationBase, addr1);
            ok(info.AllocationProtect == PAGE_NOACCESS, "%x != PAGE_NOACCESS\n", info.AllocationProtect);
            ok(info.RegionSize == 0x10000, "%lx != 0x10000\n", info.RegionSize);
            ok(info.State == MEM_RESERVE, "%x != MEM_RESERVE\n", info.State);
            ok(info.Protect == 0, "%x != PAGE_NOACCESS\n", info.Protect);
            ok(info.Type == MEM_PRIVATE, "%x != MEM_PRIVATE\n", info.Type);
        }
        else /* before win8 */
            ok(GetLastError() == ERROR_ACCESS_DENIED, "wrong error %d\n", GetLastError());

        SetLastError(0xdeadbeef);
        ok(!pVirtualFreeEx(hproc, addr1, 0, MEM_RELEASE),
           "VirtualFreeEx without PROCESS_VM_OPERATION rights should fail\n");
        ok(GetLastError() == ERROR_ACCESS_DENIED, "wrong error %d\n", GetLastError());

        CloseHandle(hproc);
    }

    ok(VirtualFree(addr1, 0, MEM_RELEASE), "VirtualFree failed\n");
}

static void test_GetProcessVersion(void)
{
    static char cmdline[] = "winver.exe";
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    DWORD ret;

    SetLastError(0xdeadbeef);
    ret = GetProcessVersion(0);
    ok(ret, "GetProcessVersion error %u\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = GetProcessVersion(GetCurrentProcessId());
    ok(ret, "GetProcessVersion error %u\n", GetLastError());

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    SetLastError(0xdeadbeef);
    ret = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    ok(ret, "CreateProcess error %u\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = GetProcessVersion(pi.dwProcessId);
    ok(ret, "GetProcessVersion error %u\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = TerminateProcess(pi.hProcess, 0);
    ok(ret, "TerminateProcess error %u\n", GetLastError());

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

static void test_GetProcessImageFileNameA(void)
{
    DWORD rc;
    CHAR process[MAX_PATH];
    static const char harddisk[] = "\\Device\\HarddiskVolume";

    if (!pK32GetProcessImageFileNameA)
    {
        win_skip("K32GetProcessImageFileNameA is unavailable\n");
        return;
    }

    /* callers must guess the buffer size */
    SetLastError(0xdeadbeef);
    rc = pK32GetProcessImageFileNameA(GetCurrentProcess(), NULL, 0);
    ok(!rc && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "K32GetProcessImageFileNameA(no buffer): returned %u, le=%u\n", rc, GetLastError());

    *process = '\0';
    rc = pK32GetProcessImageFileNameA(GetCurrentProcess(), process, sizeof(process));
    expect_eq_d(rc, lstrlenA(process));
    if (strncmp(process, harddisk, lstrlenA(harddisk)))
    {
        todo_wine win_skip("%s is probably on a network share, skipping tests\n", process);
        return;
    }

    if (!pQueryFullProcessImageNameA)
        win_skip("QueryFullProcessImageNameA unavailable (added in Windows Vista)\n");
    else
    {
        CHAR image[MAX_PATH];
        DWORD length;

        length = sizeof(image);
        expect_eq_d(TRUE, pQueryFullProcessImageNameA(GetCurrentProcess(), PROCESS_NAME_NATIVE, image, &length));
        expect_eq_d(length, lstrlenA(image));
        ok(lstrcmpiA(process, image) == 0, "expected '%s' to be equal to '%s'\n", process, image);
    }
}

static void test_QueryFullProcessImageNameA(void)
{
#define INIT_STR "Just some words"
    DWORD length, size;
    CHAR buf[MAX_PATH], module[MAX_PATH];

    if (!pQueryFullProcessImageNameA)
    {
        win_skip("QueryFullProcessImageNameA unavailable (added in Windows Vista)\n");
        return;
    }

    *module = '\0';
    SetLastError(0); /* old Windows don't reset it on success */
    size = GetModuleFileNameA(NULL, module, sizeof(module));
    ok(size && GetLastError() != ERROR_INSUFFICIENT_BUFFER, "GetModuleFileName failed: %u le=%u\n", size, GetLastError());

    /* get the buffer length without \0 terminator */
    length = sizeof(buf);
    expect_eq_d(TRUE, pQueryFullProcessImageNameA(GetCurrentProcess(), 0, buf, &length));
    expect_eq_d(length, lstrlenA(buf));
    ok((buf[0] == '\\' && buf[1] == '\\') ||
       lstrcmpiA(buf, module) == 0, "expected %s to match %s\n", buf, module);

    /*  when the buffer is too small
     *  - function fail with error ERROR_INSUFFICIENT_BUFFER
     *  - the size variable is not modified
     * tested with the biggest too small size
     */
    size = length;
    sprintf(buf,INIT_STR);
    expect_eq_d(FALSE, pQueryFullProcessImageNameA(GetCurrentProcess(), 0, buf, &size));
    expect_eq_d(ERROR_INSUFFICIENT_BUFFER, GetLastError());
    expect_eq_d(length, size);
    expect_eq_s(INIT_STR, buf);

    /* retest with smaller buffer size
     */
    size = 4;
    sprintf(buf,INIT_STR);
    expect_eq_d(FALSE, pQueryFullProcessImageNameA(GetCurrentProcess(), 0, buf, &size));
    expect_eq_d(ERROR_INSUFFICIENT_BUFFER, GetLastError());
    expect_eq_d(4, size);
    expect_eq_s(INIT_STR, buf);

    /* this is a difference between the ascii and the unicode version
     * the unicode version crashes when the size is big enough to hold
     * the result while the ascii version throws an error
     */
    size = 1024;
    expect_eq_d(FALSE, pQueryFullProcessImageNameA(GetCurrentProcess(), 0, NULL, &size));
    expect_eq_d(1024, size);
    expect_eq_d(ERROR_INVALID_PARAMETER, GetLastError());
}

static void test_QueryFullProcessImageNameW(void)
{
    HANDLE hSelf;
    WCHAR module_name[1024], device[1024];
    WCHAR deviceW[] = {'\\','D', 'e','v','i','c','e',0};
    WCHAR buf[1024];
    DWORD size, len;

    if (!pQueryFullProcessImageNameW)
    {
        win_skip("QueryFullProcessImageNameW unavailable (added in Windows Vista)\n");
        return;
    }

    ok(GetModuleFileNameW(NULL, module_name, 1024), "GetModuleFileNameW(NULL, ...) failed\n");

    /* GetCurrentProcess pseudo-handle */
    size = sizeof(buf) / sizeof(buf[0]);
    expect_eq_d(TRUE, pQueryFullProcessImageNameW(GetCurrentProcess(), 0, buf, &size));
    expect_eq_d(lstrlenW(buf), size);
    expect_eq_ws_i(buf, module_name);

    hSelf = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());
    /* Real handle */
    size = sizeof(buf) / sizeof(buf[0]);
    expect_eq_d(TRUE, pQueryFullProcessImageNameW(hSelf, 0, buf, &size));
    expect_eq_d(lstrlenW(buf), size);
    expect_eq_ws_i(buf, module_name);

    /* Buffer too small */
    size = lstrlenW(module_name)/2;
    lstrcpyW(buf, deviceW);
    SetLastError(0xdeadbeef);
    expect_eq_d(FALSE, pQueryFullProcessImageNameW(hSelf, 0, buf, &size));
    expect_eq_d(lstrlenW(module_name)/2, size);  /* size not changed(!) */
    expect_eq_d(ERROR_INSUFFICIENT_BUFFER, GetLastError());
    expect_eq_ws_i(deviceW, buf);  /* buffer not changed */

    /* Too small - not space for NUL terminator */
    size = lstrlenW(module_name);
    SetLastError(0xdeadbeef);
    expect_eq_d(FALSE, pQueryFullProcessImageNameW(hSelf, 0, buf, &size));
    expect_eq_d(lstrlenW(module_name), size);  /* size not changed(!) */
    expect_eq_d(ERROR_INSUFFICIENT_BUFFER, GetLastError());

    /* NULL buffer */
    size = 0;
    expect_eq_d(FALSE, pQueryFullProcessImageNameW(hSelf, 0, NULL, &size));
    expect_eq_d(0, size);
    expect_eq_d(ERROR_INSUFFICIENT_BUFFER, GetLastError());

    /* Buffer too small */
    size = lstrlenW(module_name)/2;
    SetLastError(0xdeadbeef);
    lstrcpyW(buf, module_name);
    expect_eq_d(FALSE, pQueryFullProcessImageNameW(hSelf, 0, buf, &size));
    expect_eq_d(lstrlenW(module_name)/2, size);  /* size not changed(!) */
    expect_eq_d(ERROR_INSUFFICIENT_BUFFER, GetLastError());
    expect_eq_ws_i(module_name, buf);  /* buffer not changed */


    /* native path */
    size = sizeof(buf) / sizeof(buf[0]);
    expect_eq_d(TRUE, pQueryFullProcessImageNameW(hSelf, PROCESS_NAME_NATIVE, buf, &size));
    expect_eq_d(lstrlenW(buf), size);
    ok(buf[0] == '\\', "NT path should begin with '\\'\n");
    ok(memcmp(buf, deviceW, sizeof(WCHAR)*lstrlenW(deviceW)) == 0, "NT path should begin with \\Device\n");

    module_name[2] = '\0';
    *device = '\0';
    size = QueryDosDeviceW(module_name, device, sizeof(device)/sizeof(device[0]));
    ok(size, "QueryDosDeviceW failed: le=%u\n", GetLastError());
    len = lstrlenW(device);
    ok(size >= len+2, "expected %d to be greater than %d+2 = strlen(%s)\n", size, len, wine_dbgstr_w(device));

    if (size >= lstrlenW(buf))
    {
        ok(0, "expected %s\\ to match the start of %s\n", wine_dbgstr_w(device), wine_dbgstr_w(buf));
    }
    else
    {
        ok(buf[len] == '\\', "expected '%c' to be a '\\' in %s\n", buf[len], wine_dbgstr_w(module_name));
        buf[len] = '\0';
        ok(lstrcmpiW(device, buf) == 0, "expected %s to match %s\n", wine_dbgstr_w(device), wine_dbgstr_w(buf));
        ok(lstrcmpiW(module_name+3, buf+len+1) == 0, "expected '%s' to match '%s'\n", wine_dbgstr_w(module_name+3), wine_dbgstr_w(buf+len+1));
    }

    CloseHandle(hSelf);
}

static void test_Handles(void)
{
    HANDLE handle = GetCurrentProcess();
    HANDLE h2, h3;
    BOOL ret;
    DWORD code;

    ok( handle == (HANDLE)~(ULONG_PTR)0 ||
        handle == (HANDLE)(ULONG_PTR)0x7fffffff /* win9x */,
        "invalid current process handle %p\n", handle );
    ret = GetExitCodeProcess( handle, &code );
    ok( ret, "GetExitCodeProcess failed err %u\n", GetLastError() );
#ifdef _WIN64
    /* truncated handle */
    SetLastError( 0xdeadbeef );
    handle = (HANDLE)((ULONG_PTR)handle & ~0u);
    ret = GetExitCodeProcess( handle, &code );
    ok( !ret, "GetExitCodeProcess succeeded for %p\n", handle );
    ok( GetLastError() == ERROR_INVALID_HANDLE, "wrong error %u\n", GetLastError() );
    /* sign-extended handle */
    SetLastError( 0xdeadbeef );
    handle = (HANDLE)((LONG_PTR)(int)(ULONG_PTR)handle);
    ret = GetExitCodeProcess( handle, &code );
    ok( ret, "GetExitCodeProcess failed err %u\n", GetLastError() );
    /* invalid high-word */
    SetLastError( 0xdeadbeef );
    handle = (HANDLE)(((ULONG_PTR)handle & ~0u) + ((ULONG_PTR)1 << 32));
    ret = GetExitCodeProcess( handle, &code );
    ok( !ret, "GetExitCodeProcess succeeded for %p\n", handle );
    ok( GetLastError() == ERROR_INVALID_HANDLE, "wrong error %u\n", GetLastError() );
#endif

    handle = GetStdHandle( STD_ERROR_HANDLE );
    ok( handle != 0, "handle %p\n", handle );
    DuplicateHandle( GetCurrentProcess(), handle, GetCurrentProcess(), &h3,
                     0, TRUE, DUPLICATE_SAME_ACCESS );
    SetStdHandle( STD_ERROR_HANDLE, h3 );
    CloseHandle( (HANDLE)STD_ERROR_HANDLE );
    h2 = GetStdHandle( STD_ERROR_HANDLE );
    ok( h2 == 0 ||
        broken( h2 == h3) || /* nt4, w2k */
        broken( h2 == INVALID_HANDLE_VALUE),  /* win9x */
        "wrong handle %p/%p\n", h2, h3 );
    SetStdHandle( STD_ERROR_HANDLE, handle );
}

static void test_IsWow64Process(void)
{
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    DWORD ret;
    BOOL is_wow64;
    static char cmdline[] = "C:\\Program Files\\Internet Explorer\\iexplore.exe";
    static char cmdline_wow64[] = "C:\\Program Files (x86)\\Internet Explorer\\iexplore.exe";

    if (!pIsWow64Process)
    {
        skip("IsWow64Process is not available\n");
        return;
    }

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ret = CreateProcessA(NULL, cmdline_wow64, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (ret)
    {
        trace("Created process %s\n", cmdline_wow64);
        is_wow64 = FALSE;
        ret = pIsWow64Process(pi.hProcess, &is_wow64);
        ok(ret, "IsWow64Process failed.\n");
        ok(is_wow64, "is_wow64 returned FALSE.\n");

        ret = TerminateProcess(pi.hProcess, 0);
        ok(ret, "TerminateProcess error\n");

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ret = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (ret)
    {
        trace("Created process %s\n", cmdline);
        is_wow64 = TRUE;
        ret = pIsWow64Process(pi.hProcess, &is_wow64);
        ok(ret, "IsWow64Process failed.\n");
        ok(!is_wow64, "is_wow64 returned TRUE.\n");

        ret = TerminateProcess(pi.hProcess, 0);
        ok(ret, "TerminateProcess error\n");

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

static void test_SystemInfo(void)
{
    SYSTEM_INFO si, nsi;
    BOOL is_wow64;

    if (!pGetNativeSystemInfo)
    {
        win_skip("GetNativeSystemInfo is not available\n");
        return;
    }

    if (!pIsWow64Process || !pIsWow64Process( GetCurrentProcess(), &is_wow64 )) is_wow64 = FALSE;

    GetSystemInfo(&si);
    pGetNativeSystemInfo(&nsi);
    if (is_wow64)
    {
        if (S(U(si)).wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
        {
            ok(S(U(nsi)).wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64,
               "Expected PROCESSOR_ARCHITECTURE_AMD64, got %d\n",
               S(U(nsi)).wProcessorArchitecture);
            ok(nsi.dwProcessorType == PROCESSOR_AMD_X8664,
               "Expected PROCESSOR_AMD_X8664, got %d\n",
               nsi.dwProcessorType);
        }
    }
    else
    {
        ok(S(U(si)).wProcessorArchitecture == S(U(nsi)).wProcessorArchitecture,
           "Expected no difference for wProcessorArchitecture, got %d and %d\n",
           S(U(si)).wProcessorArchitecture, S(U(nsi)).wProcessorArchitecture);
        ok(si.dwProcessorType == nsi.dwProcessorType,
           "Expected no difference for dwProcessorType, got %d and %d\n",
           si.dwProcessorType, nsi.dwProcessorType);
    }
}

static void test_RegistryQuota(void)
{
    BOOL ret;
    DWORD max_quota, used_quota;

    if (!pGetSystemRegistryQuota)
    {
        win_skip("GetSystemRegistryQuota is not available\n");
        return;
    }

    ret = pGetSystemRegistryQuota(NULL, NULL);
    ok(ret == TRUE,
       "Expected GetSystemRegistryQuota to return TRUE, got %d\n", ret);

    ret = pGetSystemRegistryQuota(&max_quota, NULL);
    ok(ret == TRUE,
       "Expected GetSystemRegistryQuota to return TRUE, got %d\n", ret);

    ret = pGetSystemRegistryQuota(NULL, &used_quota);
    ok(ret == TRUE,
       "Expected GetSystemRegistryQuota to return TRUE, got %d\n", ret);

    ret = pGetSystemRegistryQuota(&max_quota, &used_quota);
    ok(ret == TRUE,
       "Expected GetSystemRegistryQuota to return TRUE, got %d\n", ret);
}

static void test_TerminateProcess(void)
{
    static char cmdline[] = "winver.exe";
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    DWORD ret;
    HANDLE dummy, thread;

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    SetLastError(0xdeadbeef);
    ret = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi);
    ok(ret, "CreateProcess error %u\n", GetLastError());

    SetLastError(0xdeadbeef);
    thread = CreateRemoteThread(pi.hProcess, NULL, 0, (void *)0xdeadbeef, NULL, CREATE_SUSPENDED, &ret);
    ok(thread != 0, "CreateRemoteThread error %d\n", GetLastError());

    /* create a not closed thread handle duplicate in the target process */
    SetLastError(0xdeadbeef);
    ret = DuplicateHandle(GetCurrentProcess(), thread, pi.hProcess, &dummy,
                          0, FALSE, DUPLICATE_SAME_ACCESS);
    ok(ret, "DuplicateHandle error %u\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = TerminateThread(thread, 0);
    ok(ret, "TerminateThread error %u\n", GetLastError());
    CloseHandle(thread);

    SetLastError(0xdeadbeef);
    ret = TerminateProcess(pi.hProcess, 0);
    ok(ret, "TerminateProcess error %u\n", GetLastError());

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

static void test_DuplicateHandle(void)
{
    char path[MAX_PATH], file_name[MAX_PATH];
    HANDLE f, fmin, out;
    DWORD info;
    BOOL r;

    r = DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(),
            GetCurrentProcess(), &out, 0, FALSE,
            DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE);
    ok(r, "DuplicateHandle error %u\n", GetLastError());
    r = GetHandleInformation(out, &info);
    ok(r, "GetHandleInformation error %u\n", GetLastError());
    ok(info == 0, "info = %x\n", info);
    ok(out != GetCurrentProcess(), "out = GetCurrentProcess()\n");
    CloseHandle(out);

    r = DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(),
            GetCurrentProcess(), &out, 0, TRUE,
            DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE);
    ok(r, "DuplicateHandle error %u\n", GetLastError());
    r = GetHandleInformation(out, &info);
    ok(r, "GetHandleInformation error %u\n", GetLastError());
    ok(info == HANDLE_FLAG_INHERIT, "info = %x\n", info);
    ok(out != GetCurrentProcess(), "out = GetCurrentProcess()\n");
    CloseHandle(out);

    GetTempPathA(MAX_PATH, path);
    GetTempFileNameA(path, "wt", 0, file_name);
    f = CreateFileA(file_name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, 0);
    if (f == INVALID_HANDLE_VALUE)
    {
        ok(0, "could not create %s\n", file_name);
        return;
    }

    r = DuplicateHandle(GetCurrentProcess(), f, GetCurrentProcess(), &out,
            0, FALSE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE);
    ok(r, "DuplicateHandle error %u\n", GetLastError());
    ok(f == out, "f != out\n");
    r = GetHandleInformation(out, &info);
    ok(r, "GetHandleInformation error %u\n", GetLastError());
    ok(info == 0, "info = %x\n", info);

    r = DuplicateHandle(GetCurrentProcess(), f, GetCurrentProcess(), &out,
            0, TRUE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE);
    ok(r, "DuplicateHandle error %u\n", GetLastError());
    ok(f == out, "f != out\n");
    r = GetHandleInformation(out, &info);
    ok(r, "GetHandleInformation error %u\n", GetLastError());
    ok(info == HANDLE_FLAG_INHERIT, "info = %x\n", info);

    r = SetHandleInformation(f, HANDLE_FLAG_PROTECT_FROM_CLOSE, HANDLE_FLAG_PROTECT_FROM_CLOSE);
    ok(r, "SetHandleInformation error %u\n", GetLastError());
    r = DuplicateHandle(GetCurrentProcess(), f, GetCurrentProcess(), &out,
                0, TRUE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE);
    ok(r, "DuplicateHandle error %u\n", GetLastError());
    ok(f != out, "f == out\n");
    r = GetHandleInformation(out, &info);
    ok(r, "GetHandleInformation error %u\n", GetLastError());
    ok(info == HANDLE_FLAG_INHERIT, "info = %x\n", info);
    r = SetHandleInformation(f, HANDLE_FLAG_PROTECT_FROM_CLOSE, 0);
    ok(r, "SetHandleInformation error %u\n", GetLastError());

    /* Test if DuplicateHandle allocates first free handle */
    if (f > out)
    {
        fmin = out;
    }
    else
    {
        fmin = f;
        f = out;
    }
    CloseHandle(fmin);
    r = DuplicateHandle(GetCurrentProcess(), f, GetCurrentProcess(), &out,
            0, TRUE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE);
    ok(r, "DuplicateHandle error %u\n", GetLastError());
    ok(f == out, "f != out\n");
    CloseHandle(out);
    DeleteFileA(file_name);

    f = CreateFileA("CONIN$", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0);
    if (!is_console(f))
    {
        skip("DuplicateHandle on console handle\n");
        CloseHandle(f);
        return;
    }

    r = DuplicateHandle(GetCurrentProcess(), f, GetCurrentProcess(), &out,
            0, FALSE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE);
    ok(r, "DuplicateHandle error %u\n", GetLastError());
    todo_wine ok(f != out, "f == out\n");
    CloseHandle(out);
}

#define test_completion(a, b, c, d, e) _test_completion(__LINE__, a, b, c, d, e)
static void _test_completion(int line, HANDLE port, DWORD ekey, ULONG_PTR evalue, ULONG_PTR eoverlapped, DWORD wait)
{
    LPOVERLAPPED overlapped;
    ULONG_PTR value;
    DWORD key;
    BOOL ret;

    ret = GetQueuedCompletionStatus(port, &key, &value, &overlapped, wait);

    ok_(__FILE__, line)(ret, "GetQueuedCompletionStatus: %x\n", GetLastError());
    if (ret)
    {
        ok_(__FILE__, line)(key == ekey, "unexpected key %x\n", key);
        ok_(__FILE__, line)(value == evalue, "unexpected value %p\n", (void *)value);
        ok_(__FILE__, line)(overlapped == (LPOVERLAPPED)eoverlapped, "unexpected overlapped %p\n", overlapped);
    }
}

#define create_process(cmd, pi) _create_process(__LINE__, cmd, pi)
static void _create_process(int line, const char *command, LPPROCESS_INFORMATION pi)
{
    BOOL ret;
    char buffer[MAX_PATH];
    STARTUPINFOA si = {0};

    sprintf(buffer, "\"%s\" tests/process.c %s", selfname, command);

    ret = CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, pi);
    ok_(__FILE__, line)(ret, "CreateProcess error %u\n", GetLastError());
}


static void test_IsProcessInJob(void)
{
    HANDLE job, job2;
    PROCESS_INFORMATION pi;
    BOOL ret, out;
    DWORD dwret;

    if (!pIsProcessInJob)
    {
        win_skip("IsProcessInJob not available.\n");
        return;
    }

    job = pCreateJobObjectW(NULL, NULL);
    ok(job != NULL, "CreateJobObject error %u\n", GetLastError());

    job2 = pCreateJobObjectW(NULL, NULL);
    ok(job2 != NULL, "CreateJobObject error %u\n", GetLastError());

    create_process("wait", &pi);

    out = TRUE;
    ret = pIsProcessInJob(pi.hProcess, job, &out);
    ok(ret, "IsProcessInJob error %u\n", GetLastError());
    ok(!out, "IsProcessInJob returned out=%u\n", out);

    out = TRUE;
    ret = pIsProcessInJob(pi.hProcess, job2, &out);
    ok(ret, "IsProcessInJob error %u\n", GetLastError());
    ok(!out, "IsProcessInJob returned out=%u\n", out);

    out = TRUE;
    ret = pIsProcessInJob(pi.hProcess, NULL, &out);
    ok(ret, "IsProcessInJob error %u\n", GetLastError());
    ok(!out, "IsProcessInJob returned out=%u\n", out);

    ret = pAssignProcessToJobObject(job, pi.hProcess);
    ok(ret, "AssignProcessToJobObject error %u\n", GetLastError());

    out = FALSE;
    ret = pIsProcessInJob(pi.hProcess, job, &out);
    ok(ret, "IsProcessInJob error %u\n", GetLastError());
    ok(out, "IsProcessInJob returned out=%u\n", out);

    out = TRUE;
    ret = pIsProcessInJob(pi.hProcess, job2, &out);
    ok(ret, "IsProcessInJob error %u\n", GetLastError());
    ok(!out, "IsProcessInJob returned out=%u\n", out);

    out = FALSE;
    ret = pIsProcessInJob(pi.hProcess, NULL, &out);
    ok(ret, "IsProcessInJob error %u\n", GetLastError());
    ok(out, "IsProcessInJob returned out=%u\n", out);

    TerminateProcess(pi.hProcess, 0);

    dwret = WaitForSingleObject(pi.hProcess, 1000);
    ok(dwret == WAIT_OBJECT_0, "WaitForSingleObject returned %u\n", dwret);

    out = FALSE;
    ret = pIsProcessInJob(pi.hProcess, job, &out);
    ok(ret, "IsProcessInJob error %u\n", GetLastError());
    ok(out, "IsProcessInJob returned out=%u\n", out);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(job);
    CloseHandle(job2);
}

static void test_TerminateJobObject(void)
{
    HANDLE job;
    PROCESS_INFORMATION pi;
    BOOL ret;
    DWORD dwret;

    job = pCreateJobObjectW(NULL, NULL);
    ok(job != NULL, "CreateJobObject error %u\n", GetLastError());

    create_process("wait", &pi);

    ret = pAssignProcessToJobObject(job, pi.hProcess);
    ok(ret, "AssignProcessToJobObject error %u\n", GetLastError());

    ret = pTerminateJobObject(job, 123);
    ok(ret, "TerminateJobObject error %u\n", GetLastError());

    dwret = WaitForSingleObject(pi.hProcess, 1000);
    ok(dwret == WAIT_OBJECT_0, "WaitForSingleObject returned %u\n", dwret);
    if (dwret == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 0);

    ret = GetExitCodeProcess(pi.hProcess, &dwret);
    ok(ret, "GetExitCodeProcess error %u\n", GetLastError());
    ok(dwret == 123 || broken(dwret == 0) /* randomly fails on Win 2000 / XP */,
       "wrong exitcode %u\n", dwret);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    /* Test adding an already terminated process to a job object */
    create_process("exit", &pi);

    dwret = WaitForSingleObject(pi.hProcess, 1000);
    ok(dwret == WAIT_OBJECT_0, "WaitForSingleObject returned %u\n", dwret);

    SetLastError(0xdeadbeef);
    ret = pAssignProcessToJobObject(job, pi.hProcess);
    ok(!ret, "AssignProcessToJobObject unexpectedly succeeded\n");
    expect_eq_d(ERROR_ACCESS_DENIED, GetLastError());

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    CloseHandle(job);
}

static void test_QueryInformationJobObject(void)
{
    char buf[sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + sizeof(ULONG_PTR) * 4];
    PJOBOBJECT_BASIC_PROCESS_ID_LIST pid_list = (JOBOBJECT_BASIC_PROCESS_ID_LIST *)buf;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION ext_limit_info;
    JOBOBJECT_BASIC_LIMIT_INFORMATION *basic_limit_info = &ext_limit_info.BasicLimitInformation;
    DWORD dwret, ret_len;
    PROCESS_INFORMATION pi[2];
    HANDLE job;
    BOOL ret;

    job = pCreateJobObjectW(NULL, NULL);
    ok(job != NULL, "CreateJobObject error %u\n", GetLastError());

    /* Only active processes are returned */
    create_process("exit", &pi[0]);
    ret = pAssignProcessToJobObject(job, pi[0].hProcess);
    ok(ret, "AssignProcessToJobObject error %u\n", GetLastError());
    dwret = WaitForSingleObject(pi[0].hProcess, 1000);
    ok(dwret == WAIT_OBJECT_0, "WaitForSingleObject returned %u\n", dwret);

    CloseHandle(pi[0].hProcess);
    CloseHandle(pi[0].hThread);

    create_process("wait", &pi[0]);
    ret = pAssignProcessToJobObject(job, pi[0].hProcess);
    ok(ret, "AssignProcessToJobObject error %u\n", GetLastError());

    create_process("wait", &pi[1]);
    ret = pAssignProcessToJobObject(job, pi[1].hProcess);
    ok(ret, "AssignProcessToJobObject error %u\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = QueryInformationJobObject(job, JobObjectBasicProcessIdList, pid_list,
                                    FIELD_OFFSET(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList), &ret_len);
    ok(!ret, "QueryInformationJobObject expected failure\n");
    todo_wine
    expect_eq_d(ERROR_BAD_LENGTH, GetLastError());

    SetLastError(0xdeadbeef);
    memset(buf, 0, sizeof(buf));
    pid_list->NumberOfAssignedProcesses = 42;
    pid_list->NumberOfProcessIdsInList  = 42;
    ret = QueryInformationJobObject(job, JobObjectBasicProcessIdList, pid_list,
                                    FIELD_OFFSET(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList[1]), &ret_len);
    ok(!ret, "QueryInformationJobObject expected failure\n");
    todo_wine
    expect_eq_d(ERROR_MORE_DATA, GetLastError());
    if (ret)
    {
        expect_eq_d(42, pid_list->NumberOfAssignedProcesses);
        expect_eq_d(42, pid_list->NumberOfProcessIdsInList);
    }

    memset(buf, 0, sizeof(buf));
    ret = pQueryInformationJobObject(job, JobObjectBasicProcessIdList, pid_list, sizeof(buf), &ret_len);
    todo_wine
    ok(ret, "QueryInformationJobObject error %u\n", GetLastError());
    if(ret)
    {
        if (pid_list->NumberOfAssignedProcesses == 3) /* Win 8 */
            win_skip("Number of assigned processes broken on Win 8\n");
        else
        {
            ULONG_PTR *list = pid_list->ProcessIdList;

            ok(ret_len == FIELD_OFFSET(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList[2]),
               "QueryInformationJobObject returned ret_len=%u\n", ret_len);

            expect_eq_d(2, pid_list->NumberOfAssignedProcesses);
            expect_eq_d(2, pid_list->NumberOfProcessIdsInList);
            expect_eq_d(pi[0].dwProcessId, list[0]);
            expect_eq_d(pi[1].dwProcessId, list[1]);
        }
    }

    /* test JobObjectBasicLimitInformation */
    ret = pQueryInformationJobObject(job, JobObjectBasicLimitInformation, basic_limit_info,
                                     sizeof(*basic_limit_info) - 1, &ret_len);
    ok(!ret, "QueryInformationJobObject expected failure\n");
    expect_eq_d(ERROR_BAD_LENGTH, GetLastError());

    ret_len = 0xdeadbeef;
    memset(basic_limit_info, 0x11, sizeof(*basic_limit_info));
    ret = pQueryInformationJobObject(job, JobObjectBasicLimitInformation, basic_limit_info,
                                     sizeof(*basic_limit_info), &ret_len);
    ok(ret, "QueryInformationJobObject error %u\n", GetLastError());
    ok(ret_len == sizeof(*basic_limit_info), "QueryInformationJobObject returned ret_len=%u\n", ret_len);
    expect_eq_d(0, basic_limit_info->LimitFlags);

    /* test JobObjectExtendedLimitInformation */
    ret = pQueryInformationJobObject(job, JobObjectExtendedLimitInformation, &ext_limit_info,
                                     sizeof(ext_limit_info) - 1, &ret_len);
    ok(!ret, "QueryInformationJobObject expected failure\n");
    expect_eq_d(ERROR_BAD_LENGTH, GetLastError());

    ret_len = 0xdeadbeef;
    memset(&ext_limit_info, 0x11, sizeof(ext_limit_info));
    ret = pQueryInformationJobObject(job, JobObjectExtendedLimitInformation, &ext_limit_info,
                                     sizeof(ext_limit_info), &ret_len);
    ok(ret, "QueryInformationJobObject error %u\n", GetLastError());
    ok(ret_len == sizeof(ext_limit_info), "QueryInformationJobObject returned ret_len=%u\n", ret_len);
    expect_eq_d(0, basic_limit_info->LimitFlags);

    TerminateProcess(pi[0].hProcess, 0);
    CloseHandle(pi[0].hProcess);
    CloseHandle(pi[0].hThread);

    TerminateProcess(pi[1].hProcess, 0);
    CloseHandle(pi[1].hProcess);
    CloseHandle(pi[1].hThread);

    CloseHandle(job);
}

static void test_CompletionPort(void)
{
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT port_info;
    PROCESS_INFORMATION pi;
    HANDLE job, port;
    DWORD dwret;
    BOOL ret;

    job = pCreateJobObjectW(NULL, NULL);
    ok(job != NULL, "CreateJobObject error %u\n", GetLastError());

    port = pCreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    ok(port != NULL, "CreateIoCompletionPort error %u\n", GetLastError());

    port_info.CompletionKey = job;
    port_info.CompletionPort = port;
    ret = pSetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &port_info, sizeof(port_info));
    ok(ret, "SetInformationJobObject error %u\n", GetLastError());

    create_process("wait", &pi);

    ret = pAssignProcessToJobObject(job, pi.hProcess);
    ok(ret, "AssignProcessToJobObject error %u\n", GetLastError());

    test_completion(port, JOB_OBJECT_MSG_NEW_PROCESS, (DWORD_PTR)job, pi.dwProcessId, 0);

    TerminateProcess(pi.hProcess, 0);
    dwret = WaitForSingleObject(pi.hProcess, 1000);
    ok(dwret == WAIT_OBJECT_0, "WaitForSingleObject returned %u\n", dwret);

    test_completion(port, JOB_OBJECT_MSG_EXIT_PROCESS, (DWORD_PTR)job, pi.dwProcessId, 0);
    test_completion(port, JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO, (DWORD_PTR)job, 0, 100);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(job);
    CloseHandle(port);
}

static void test_KillOnJobClose(void)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info;
    PROCESS_INFORMATION pi;
    DWORD dwret;
    HANDLE job;
    BOOL ret;

    job = pCreateJobObjectW(NULL, NULL);
    ok(job != NULL, "CreateJobObject error %u\n", GetLastError());

    limit_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    ret = pSetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit_info, sizeof(limit_info));
    if (!ret && GetLastError() == ERROR_INVALID_PARAMETER)
    {
        win_skip("Kill on job close limit not available\n");
        return;
    }
    ok(ret, "SetInformationJobObject error %u\n", GetLastError());

    create_process("wait", &pi);

    ret = pAssignProcessToJobObject(job, pi.hProcess);
    ok(ret, "AssignProcessToJobObject error %u\n", GetLastError());

    CloseHandle(job);

    dwret = WaitForSingleObject(pi.hProcess, 1000);
    ok(dwret == WAIT_OBJECT_0, "WaitForSingleObject returned %u\n", dwret);
    if (dwret == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 0);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

static void test_WaitForJobObject(void)
{
    HANDLE job;
    PROCESS_INFORMATION pi;
    BOOL ret;
    DWORD dwret;

    /* test waiting for a job object when the process is killed */
    job = pCreateJobObjectW(NULL, NULL);
    ok(job != NULL, "CreateJobObject error %u\n", GetLastError());

    dwret = WaitForSingleObject(job, 100);
    ok(dwret == WAIT_TIMEOUT, "WaitForSingleObject returned %u\n", dwret);

    create_process("wait", &pi);

    ret = pAssignProcessToJobObject(job, pi.hProcess);
    ok(ret, "AssignProcessToJobObject error %u\n", GetLastError());

    dwret = WaitForSingleObject(job, 100);
    ok(dwret == WAIT_TIMEOUT, "WaitForSingleObject returned %u\n", dwret);

    ret = pTerminateJobObject(job, 123);
    ok(ret, "TerminateJobObject error %u\n", GetLastError());

    dwret = WaitForSingleObject(job, 500);
    ok(dwret == WAIT_OBJECT_0 || broken(dwret == WAIT_TIMEOUT),
       "WaitForSingleObject returned %u\n", dwret);

    if (dwret == WAIT_TIMEOUT) /* Win 2000/XP */
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(job);
        win_skip("TerminateJobObject doesn't signal job, skipping tests\n");
        return;
    }

    /* the object is not reset immediately */
    dwret = WaitForSingleObject(job, 100);
    ok(dwret == WAIT_OBJECT_0, "WaitForSingleObject returned %u\n", dwret);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    /* creating a new process doesn't reset the signalled state */
    create_process("wait", &pi);

    ret = pAssignProcessToJobObject(job, pi.hProcess);
    ok(ret, "AssignProcessToJobObject error %u\n", GetLastError());

    dwret = WaitForSingleObject(job, 100);
    ok(dwret == WAIT_OBJECT_0, "WaitForSingleObject returned %u\n", dwret);

    ret = pTerminateJobObject(job, 123);
    ok(ret, "TerminateJobObject error %u\n", GetLastError());

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    CloseHandle(job);

    /* repeat the test, but this time the process terminates properly */
    job = pCreateJobObjectW(NULL, NULL);
    ok(job != NULL, "CreateJobObject error %u\n", GetLastError());

    dwret = WaitForSingleObject(job, 100);
    ok(dwret == WAIT_TIMEOUT, "WaitForSingleObject returned %u\n", dwret);

    create_process("exit", &pi);

    ret = pAssignProcessToJobObject(job, pi.hProcess);
    ok(ret, "AssignProcessToJobObject error %u\n", GetLastError());

    dwret = WaitForSingleObject(job, 100);
    ok(dwret == WAIT_TIMEOUT, "WaitForSingleObject returned %u\n", dwret);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(job);
}

static HANDLE test_AddSelfToJob(void)
{
    HANDLE job;
    BOOL ret;

    job = pCreateJobObjectW(NULL, NULL);
    ok(job != NULL, "CreateJobObject error %u\n", GetLastError());

    ret = pAssignProcessToJobObject(job, GetCurrentProcess());
    ok(ret, "AssignProcessToJobObject error %u\n", GetLastError());

    return job;
}

static void test_jobInheritance(HANDLE job)
{
    char buffer[MAX_PATH];
    PROCESS_INFORMATION pi;
    STARTUPINFOA si = {0};
    DWORD dwret;
    BOOL ret, out;

    if (!pIsProcessInJob)
    {
        win_skip("IsProcessInJob not available.\n");
        return;
    }

    sprintf(buffer, "\"%s\" tests/process.c %s", selfname, "exit");

    ret = CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    ok(ret, "CreateProcessA error %u\n", GetLastError());

    out = FALSE;
    ret = pIsProcessInJob(pi.hProcess, job, &out);
    ok(ret, "IsProcessInJob error %u\n", GetLastError());
    ok(out, "IsProcessInJob returned out=%u\n", out);

    dwret = WaitForSingleObject(pi.hProcess, 1000);
    ok(dwret == WAIT_OBJECT_0, "WaitForSingleObject returned %u\n", dwret);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

static void test_BreakawayOk(HANDLE job)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info;
    PROCESS_INFORMATION pi;
    STARTUPINFOA si = {0};
    char buffer[MAX_PATH];
    BOOL ret, out;
    DWORD dwret;

    if (!pIsProcessInJob)
    {
        win_skip("IsProcessInJob not available.\n");
        return;
    }

    sprintf(buffer, "\"%s\" tests/process.c %s", selfname, "exit");

    ret = CreateProcessA(NULL, buffer, NULL, NULL, FALSE, CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &pi);
    ok(!ret, "CreateProcessA expected failure\n");
    expect_eq_d(ERROR_ACCESS_DENIED, GetLastError());

    if (ret)
    {
        TerminateProcess(pi.hProcess, 0);

        dwret = WaitForSingleObject(pi.hProcess, 1000);
        ok(dwret == WAIT_OBJECT_0, "WaitForSingleObject returned %u\n", dwret);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    limit_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    ret = pSetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit_info, sizeof(limit_info));
    ok(ret, "SetInformationJobObject error %u\n", GetLastError());

    ret = CreateProcessA(NULL, buffer, NULL, NULL, FALSE, CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &pi);
    ok(ret, "CreateProcessA error %u\n", GetLastError());

    ret = pIsProcessInJob(pi.hProcess, job, &out);
    ok(ret, "IsProcessInJob error %u\n", GetLastError());
    ok(!out, "IsProcessInJob returned out=%u\n", out);

    dwret = WaitForSingleObject(pi.hProcess, 1000);
    ok(dwret == WAIT_OBJECT_0, "WaitForSingleObject returned %u\n", dwret);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    limit_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
    ret = pSetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit_info, sizeof(limit_info));
    ok(ret, "SetInformationJobObject error %u\n", GetLastError());

    ret = CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    ok(ret, "CreateProcess error %u\n", GetLastError());

    ret = pIsProcessInJob(pi.hProcess, job, &out);
    ok(ret, "IsProcessInJob error %u\n", GetLastError());
    ok(!out, "IsProcessInJob returned out=%u\n", out);

    dwret = WaitForSingleObject(pi.hProcess, 1000);
    ok(dwret == WAIT_OBJECT_0, "WaitForSingleObject returned %u\n", dwret);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    /* unset breakaway ok */
    limit_info.BasicLimitInformation.LimitFlags = 0;
    ret = pSetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit_info, sizeof(limit_info));
    ok(ret, "SetInformationJobObject error %u\n", GetLastError());
}

static void test_StartupNoConsole(void)
{
#ifndef _WIN64
    char                buffer[MAX_PATH];
    STARTUPINFOA        startup;
    PROCESS_INFORMATION info;

    if (!pNtCurrentTeb)
    {
        win_skip( "NtCurrentTeb not supported\n" );
        return;
    }

    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    get_file_name(resfile);
    sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, resfile);
    ok(CreateProcessA(NULL, buffer, NULL, NULL, TRUE, DETACHED_PROCESS, NULL, NULL, &startup,
                      &info), "CreateProcess\n");
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");
    WritePrivateProfileStringA(NULL, NULL, NULL, resfile);
    okChildInt("StartupInfoA", "hStdInput", (UINT)INVALID_HANDLE_VALUE);
    okChildInt("StartupInfoA", "hStdOutput", (UINT)INVALID_HANDLE_VALUE);
    okChildInt("StartupInfoA", "hStdError", (UINT)INVALID_HANDLE_VALUE);
    okChildInt("TEB", "hStdInput", 0);
    okChildInt("TEB", "hStdOutput", 0);
    okChildInt("TEB", "hStdError", 0);
    release_memory();
    DeleteFileA(resfile);
#endif
}

static void test_GetNumaProcessorNode(void)
{
    SYSTEM_INFO si;
    UCHAR node;
    BOOL ret;
    int i;

    if (!pGetNumaProcessorNode)
    {
        win_skip("GetNumaProcessorNode is missing\n");
        return;
    }

    GetSystemInfo(&si);
    for (i = 0; i < 256; i++)
    {
        SetLastError(0xdeadbeef);
        node = (i < si.dwNumberOfProcessors) ? 0xFF : 0xAA;
        ret = pGetNumaProcessorNode(i, &node);
        if (i < si.dwNumberOfProcessors)
        {
            ok(ret, "GetNumaProcessorNode returned FALSE for processor %d\n", i);
            ok(node != 0xFF, "expected node != 0xFF, but got 0xFF\n");
        }
        else
        {
            ok(!ret, "GetNumaProcessorNode returned TRUE for processor %d\n", i);
            ok(node == 0xFF || broken(node == 0xAA) /* WinXP */, "expected node 0xFF, got %x\n", node);
            ok(GetLastError() == ERROR_INVALID_PARAMETER, "expected ERROR_INVALID_PARAMETER, got %d\n", GetLastError());
        }
    }
}

static void test_session_info(void)
{
    DWORD session_id, active_session;
    BOOL r;

    if (!pProcessIdToSessionId)
    {
        win_skip("ProcessIdToSessionId is missing\n");
        return;
    }

    r = pProcessIdToSessionId(GetCurrentProcessId(), &session_id);
    ok(r, "ProcessIdToSessionId failed: %u\n", GetLastError());
    trace("session_id = %x\n", session_id);

    active_session = pWTSGetActiveConsoleSessionId();
    trace("active_session = %x\n", active_session);
}

static void test_process_info(void)
{
    char buf[4096];
    static const ULONG info_size[] =
    {
        sizeof(PROCESS_BASIC_INFORMATION) /* ProcessBasicInformation */,
        sizeof(QUOTA_LIMITS) /* ProcessQuotaLimits */,
        sizeof(IO_COUNTERS) /* ProcessIoCounters */,
        sizeof(VM_COUNTERS) /* ProcessVmCounters */,
        sizeof(KERNEL_USER_TIMES) /* ProcessTimes */,
        sizeof(ULONG) /* ProcessBasePriority */,
        sizeof(ULONG) /* ProcessRaisePriority */,
        sizeof(HANDLE) /* ProcessDebugPort */,
        sizeof(HANDLE) /* ProcessExceptionPort */,
        0 /* FIXME: sizeof(PROCESS_ACCESS_TOKEN) ProcessAccessToken */,
        0 /* FIXME: sizeof(PROCESS_LDT_INFORMATION) ProcessLdtInformation */,
        0 /* FIXME: sizeof(PROCESS_LDT_SIZE) ProcessLdtSize */,
        sizeof(ULONG) /* ProcessDefaultHardErrorMode */,
        0 /* ProcessIoPortHandlers: kernel-mode only */,
        0 /* FIXME: sizeof(POOLED_USAGE_AND_LIMITS) ProcessPooledUsageAndLimits */,
        0 /* FIXME: sizeof(PROCESS_WS_WATCH_INFORMATION) ProcessWorkingSetWatch */,
        sizeof(ULONG) /* ProcessUserModeIOPL */,
        sizeof(BOOLEAN) /* ProcessEnableAlignmentFaultFixup */,
        sizeof(PROCESS_PRIORITY_CLASS) /* ProcessPriorityClass */,
        sizeof(ULONG) /* ProcessWx86Information */,
        sizeof(ULONG) /* ProcessHandleCount */,
        sizeof(ULONG_PTR) /* ProcessAffinityMask */,
        sizeof(ULONG) /* ProcessPriorityBoost */,
        0 /* sizeof(PROCESS_DEVICEMAP_INFORMATION) ProcessDeviceMap */,
        0 /* sizeof(PROCESS_SESSION_INFORMATION) ProcessSessionInformation */,
        0 /* sizeof(PROCESS_FOREGROUND_BACKGROUND) ProcessForegroundInformation */,
        sizeof(ULONG_PTR) /* ProcessWow64Information */,
        sizeof(buf) /* ProcessImageFileName */,
        sizeof(ULONG) /* ProcessLUIDDeviceMapsEnabled */,
        sizeof(ULONG) /* ProcessBreakOnTermination */,
        sizeof(HANDLE) /* ProcessDebugObjectHandle */,
        sizeof(ULONG) /* ProcessDebugFlags */,
        sizeof(buf) /* ProcessHandleTracing */,
        sizeof(ULONG) /* ProcessIoPriority */,
        sizeof(ULONG) /* ProcessExecuteFlags */,
#if 0 /* FIXME: Add remaning classes */
        ProcessResourceManagement,
        sizeof(ULONG) /* ProcessCookie */,
        sizeof(SECTION_IMAGE_INFORMATION) /* ProcessImageInformation */,
        sizeof(PROCESS_CYCLE_TIME_INFORMATION) /* ProcessCycleTime */,
        sizeof(ULONG) /* ProcessPagePriority */,
        40 /* ProcessInstrumentationCallback */,
        sizeof(PROCESS_STACK_ALLOCATION_INFORMATION) /* ProcessThreadStackAllocation */,
        sizeof(PROCESS_WS_WATCH_INFORMATION_EX[]) /* ProcessWorkingSetWatchEx */,
        sizeof(buf) /* ProcessImageFileNameWin32 */,
        sizeof(HANDLE) /* ProcessImageFileMapping */,
        sizeof(PROCESS_AFFINITY_UPDATE_MODE) /* ProcessAffinityUpdateMode */,
        sizeof(PROCESS_MEMORY_ALLOCATION_MODE) /* ProcessMemoryAllocationMode */,
        sizeof(USHORT[]) /* ProcessGroupInformation */,
        sizeof(ULONG) /* ProcessTokenVirtualizationEnabled */,
        sizeof(ULONG_PTR) /* ProcessConsoleHostProcess */,
        sizeof(PROCESS_WINDOW_INFORMATION) /* ProcessWindowInformation */,
        sizeof(PROCESS_HANDLE_SNAPSHOT_INFORMATION) /* ProcessHandleInformation */,
        sizeof(PROCESS_MITIGATION_POLICY_INFORMATION) /* ProcessMitigationPolicy */,
        sizeof(ProcessDynamicFunctionTableInformation) /* ProcessDynamicFunctionTableInformation */,
        sizeof(?) /* ProcessHandleCheckingMode */,
        sizeof(PROCESS_KEEPALIVE_COUNT_INFORMATION) /* ProcessKeepAliveCount */,
        sizeof(PROCESS_REVOKE_FILE_HANDLES_INFORMATION) /* ProcessRevokeFileHandles */,
        sizeof(PROCESS_WORKING_SET_CONTROL) /* ProcessWorkingSetControl */,
        sizeof(?) /* ProcessHandleTable */,
        sizeof(?) /* ProcessCheckStackExtentsMode */,
        sizeof(buf) /* ProcessCommandLineInformation */,
        sizeof(PS_PROTECTION) /* ProcessProtectionInformation */,
        sizeof(PROCESS_MEMORY_EXHAUSTION_INFO) /* ProcessMemoryExhaustion */,
        sizeof(PROCESS_FAULT_INFORMATION) /* ProcessFaultInformation */,
        sizeof(PROCESS_TELEMETRY_ID_INFORMATION) /* ProcessTelemetryIdInformation */,
        sizeof(PROCESS_COMMIT_RELEASE_INFORMATION) /* ProcessCommitReleaseInformation */,
        sizeof(?) /* ProcessDefaultCpuSetsInformation */,
        sizeof(?) /* ProcessAllowedCpuSetsInformation */,
        0 /* ProcessReserved1Information */,
        0 /* ProcessReserved2Information */,
        sizeof(?) /* ProcessSubsystemProcess */,
        sizeof(PROCESS_JOB_MEMORY_INFO) /* ProcessJobMemoryInformation */,
#endif
    };
    HANDLE hproc;
    ULONG i, status, ret_len, size;

    if (!pNtQueryInformationProcess)
    {
        win_skip("NtQueryInformationProcess is not available on this platform\n");
        return;
    }

    hproc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    if (!hproc)
    {
        win_skip("PROCESS_QUERY_LIMITED_INFORMATION is not supported on this platform\n");
        return;
    }

    for (i = 0; i < MaxProcessInfoClass; i++)
    {
        size = info_size[i];
        if (!size) size = sizeof(buf);
        ret_len = 0;
        status = pNtQueryInformationProcess(hproc, i, buf, info_size[i], &ret_len);
        if (status == STATUS_NOT_IMPLEMENTED) continue;
        if (status == STATUS_INVALID_INFO_CLASS) continue;
        if (status == STATUS_INFO_LENGTH_MISMATCH) continue;

        switch (i)
        {
        case ProcessBasicInformation:
        case ProcessQuotaLimits:
        case ProcessTimes:
        case ProcessPriorityClass:
        case ProcessPriorityBoost:
        case ProcessLUIDDeviceMapsEnabled:
        case 33 /* ProcessIoPriority */:
        case ProcessIoCounters:
        case ProcessVmCounters:
        case ProcessWow64Information:
        case ProcessDefaultHardErrorMode:
        case ProcessHandleCount:
            ok(status == STATUS_SUCCESS, "for info %u expected STATUS_SUCCESS, got %08x (ret_len %u)\n", i, status, ret_len);
            break;

        case ProcessImageFileName:
todo_wine
            ok(status == STATUS_SUCCESS, "for info %u expected STATUS_SUCCESS, got %08x (ret_len %u)\n", i, status, ret_len);
            break;

        case ProcessAffinityMask:
        case ProcessBreakOnTermination:
            ok(status == STATUS_ACCESS_DENIED /* before win8 */ || status == STATUS_SUCCESS /* win8 is less strict */,
               "for info %u expected STATUS_SUCCESS, got %08x (ret_len %u)\n", i, status, ret_len);
            break;

        case ProcessDebugObjectHandle:
            ok(status == STATUS_ACCESS_DENIED || status == STATUS_PORT_NOT_SET,
               "for info %u expected STATUS_ACCESS_DENIED, got %08x (ret_len %u)\n", i, status, ret_len);
            break;

        case ProcessExecuteFlags:
        case ProcessDebugPort:
        case ProcessDebugFlags:
todo_wine
            ok(status == STATUS_ACCESS_DENIED, "for info %u expected STATUS_ACCESS_DENIED, got %08x (ret_len %u)\n", i, status, ret_len);
            break;

        default:
            ok(status == STATUS_ACCESS_DENIED, "for info %u expected STATUS_ACCESS_DENIED, got %08x (ret_len %u)\n", i, status, ret_len);
            break;
        }
    }

    CloseHandle(hproc);
}

static void test_GetLogicalProcessorInformationEx(void)
{
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info;
    DWORD len;
    BOOL ret;

    if (!pGetLogicalProcessorInformationEx)
    {
        win_skip("GetLogicalProcessorInformationEx() is not supported\n");
        return;
    }

    ret = pGetLogicalProcessorInformationEx(RelationAll, NULL, NULL);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER, "got %d, error %d\n", ret, GetLastError());

    len = 0;
    ret = pGetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "got %d, error %d\n", ret, GetLastError());
    ok(len > 0, "got %u\n", len);

    len = 0;
    ret = pGetLogicalProcessorInformationEx(RelationAll, NULL, &len);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "got %d, error %d\n", ret, GetLastError());
    ok(len > 0, "got %u\n", len);

    info = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
    ret = pGetLogicalProcessorInformationEx(RelationAll, info, &len);
    ok(ret, "got %d, error %d\n", ret, GetLastError());
    ok(info->Size > 0, "got %u\n", info->Size);
    HeapFree(GetProcessHeap(), 0, info);
}

START_TEST(process)
{
    HANDLE job;
    BOOL b = init();
    ok(b, "Basic init of CreateProcess test\n");
    if (!b) return;

    if (myARGC >= 3)
    {
        if (!strcmp(myARGV[2], "dump") && myARGC >= 4)
        {
            doChild(myARGV[3], (myARGC >= 5) ? myARGV[4] : NULL);
            return;
        }
        else if (!strcmp(myARGV[2], "wait"))
        {
            Sleep(30000);
            ok(0, "Child process not killed\n");
            return;
        }
        else if (!strcmp(myARGV[2], "exit"))
        {
            Sleep(100);
            return;
        }
        else if (!strcmp(myARGV[2], "nested") && myARGC >= 4)
        {
            char                buffer[MAX_PATH];
            STARTUPINFOA        startup;
            PROCESS_INFORMATION info;

            memset(&startup, 0, sizeof(startup));
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_SHOWNORMAL;

            sprintf(buffer, "\"%s\" tests/process.c dump \"%s\"", selfname, myARGV[3]);
            ok(CreateProcessA(NULL, buffer, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &startup, &info), "CreateProcess failed\n");
            CloseHandle(info.hProcess);
            CloseHandle(info.hThread);
            return;
        }

        ok(0, "Unexpected command %s\n", myARGV[2]);
        return;
    }

    test_process_info();
    test_TerminateProcess();
    test_Startup();
    test_CommandLine();
    test_Directory();
    test_Toolhelp();
    test_Environment();
    test_SuspendFlag();
    test_DebuggingFlag();
    test_Console();
    test_ExitCode();
    test_OpenProcess();
    test_GetProcessVersion();
    test_GetProcessImageFileNameA();
    test_QueryFullProcessImageNameA();
    test_QueryFullProcessImageNameW();
    test_Handles();
    test_IsWow64Process();
    test_SystemInfo();
    test_RegistryQuota();
    test_DuplicateHandle();
    test_StartupNoConsole();
    test_GetNumaProcessorNode();
    test_session_info();
    test_GetLogicalProcessorInformationEx();

    /* things that can be tested:
     *  lookup:         check the way program to be executed is searched
     *  handles:        check the handle inheritance stuff (+sec options)
     *  console:        check if console creation parameters work
     */

    if (!pCreateJobObjectW)
    {
        win_skip("No job object support\n");
        return;
    }

    test_IsProcessInJob();
    test_TerminateJobObject();
    test_QueryInformationJobObject();
    test_CompletionPort();
    test_KillOnJobClose();
    test_WaitForJobObject();
    job = test_AddSelfToJob();
    test_jobInheritance(job);
    test_BreakawayOk(job);
    CloseHandle(job);
}
