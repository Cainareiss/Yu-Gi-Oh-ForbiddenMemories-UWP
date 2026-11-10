#ifndef MEMORIES_UWP_WINAPI_H
#define MEMORIES_UWP_WINAPI_H
#include <windows.h>
/* mingw-w64's fileapifromapp.h does not yet expose these Windows SDK APIs. */
WINBASEAPI HANDLE WINAPI FindFirstFileExFromAppW(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID,
                                                FINDEX_SEARCH_OPS, LPVOID, DWORD);
WINBASEAPI WINBOOL WINAPI MoveFileFromAppW(LPCWSTR, LPCWSTR);
WINBASEAPI WINBOOL WINAPI ReplaceFileFromAppW(LPCWSTR, LPCWSTR, LPCWSTR, DWORD, LPVOID, LPVOID);
#endif
