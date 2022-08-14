#ifdef WIN32

#include <winsock2.h>
#include <windows.h>

int _pmInitLibc()
{
    int result = -1;
    WSADATA wsaData;

    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
        return -1;

    return 0;
}

int _pmUninitLibc()
{
    WSACleanup();
    return 0;
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        _pmInitLibc();
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        _pmUninitLibc();
        break;
    default:
        break;
    }
    return TRUE;
}

#endif
