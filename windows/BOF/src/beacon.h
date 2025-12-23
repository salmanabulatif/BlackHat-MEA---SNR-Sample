#pragma once
#include <windows.h>
#include <lmcons.h>
#include <wlanapi.h>

// BOF Communication
#define CALLBACK_OUTPUT      0x0
#define CALLBACK_ERROR       0x0d
#define HEAP_ZERO_MEMORY     0x00000008

DECLSPEC_IMPORT VOID WINAPI BeaconPrintf(UINT32 type, PCHAR fmt, ...);

// KERNEL32 Functions 
DECLSPEC_IMPORT HANDLE WINAPI KERNEL32$GetProcessHeap(VOID);
DECLSPEC_IMPORT LPVOID WINAPI KERNEL32$HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes);
DECLSPEC_IMPORT WINBOOL WINAPI KERNEL32$HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem);
DECLSPEC_IMPORT DWORD WINAPI KERNEL32$GetTickCount(VOID);
DECLSPEC_IMPORT VOID WINAPI KERNEL32$Sleep(DWORD dwMilliseconds);

// WLANAPI Functions 
DECLSPEC_IMPORT DWORD WINAPI WLANAPI$WlanOpenHandle(DWORD dwClientVersion, PVOID pReserved, PDWORD pdwNegotiatedVersion, PHANDLE phClientHandle);
DECLSPEC_IMPORT DWORD WINAPI WLANAPI$WlanEnumInterfaces(HANDLE hClientHandle, PVOID pReserved, PWLAN_INTERFACE_INFO_LIST* ppInterfaceList);
DECLSPEC_IMPORT DWORD WINAPI WLANAPI$WlanQueryInterface(HANDLE hClientHandle, CONST GUID* pInterfaceGuid, WLAN_INTF_OPCODE OpCode, PVOID pReserved, PDWORD pdwDataSize, PVOID* ppData, PWLAN_OPCODE_VALUE_TYPE pWlanOpcodeValueType);
DECLSPEC_IMPORT DWORD WINAPI WLANAPI$WlanGetNetworkBssList(HANDLE hClientHandle, CONST GUID* pInterfaceGuid, CONST DOT11_SSID* pDot11Ssid, DOT11_BSS_TYPE dot11BssType, WINBOOL bSecurityEnabled, PVOID pReserved, PWLAN_BSS_LIST* ppWlanBssList);
DECLSPEC_IMPORT DWORD WINAPI WLANAPI$WlanCloseHandle(HANDLE hClientHandle, PVOID pReserved);
DECLSPEC_IMPORT VOID WINAPI WLANAPI$WlanFreeMemory(PVOID pMemory);

// MSVCRT Functions
DECLSPEC_IMPORT int WINAPI MSVCRT$sprintf(char* buffer, const char* format, ...);
DECLSPEC_IMPORT size_t WINAPI MSVCRT$strlen(const char* str);

// ADDED FOR OPTIMIZATION :
DECLSPEC_IMPORT void* WINAPI MSVCRT$memcpy(void* dest, const void* src, size_t count);
DECLSPEC_IMPORT int WINAPI MSVCRT$memcmp(const void* buf1, const void* buf2, size_t count);