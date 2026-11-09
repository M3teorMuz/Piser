// Права администратора: установка в "C:\Program Files\Common Files\VST3" требует повышения.
// Вынесено в отдельный файл, чтобы windows.h не пересекался с типами JUCE.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

bool isRunningElevated()
{
    return IsUserAnAdmin() != FALSE;
}

bool relaunchElevated()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW (nullptr, path, MAX_PATH);
    HINSTANCE r = ShellExecuteW (nullptr, L"runas", path, nullptr, nullptr, SW_SHOWNORMAL);
    return (INT_PTR) r > 32;          // false, если пользователь отказал в запросе UAC
}
