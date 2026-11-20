#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define REAPERAPI_IMPLEMENT
#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_CountTracks
#define REAPERAPI_WANT_GetAllProjectPlayStates
#define REAPERAPI_WANT_GetCursorPosition
#define REAPERAPI_WANT_GetExtState
#define REAPERAPI_WANT_GetMainHwnd
#define REAPERAPI_WANT_GetMediaTrackInfo_Value
#define REAPERAPI_WANT_GetPlayState
#define REAPERAPI_WANT_GetTrack
#define REAPERAPI_WANT_Main_OnCommand
#define REAPERAPI_WANT_OnStopButton
#define REAPERAPI_WANT_plugin_register
#define REAPERAPI_WANT_RefreshToolbar2
#define REAPERAPI_WANT_SetEditCurPos
#define REAPERAPI_WANT_SetExtState
#include <reaper_plugin_functions.h>

#include <string>

namespace {
constexpr wchar_t windowClass[] = L"ReaperGlobalRecord.HotkeyWindow";
constexpr UINT modifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
constexpr int recordingFlag = 4;
enum class ShortcutAction { recordOrDiscard, keep };
constexpr int recordCommand = 1013;
constexpr int keepCommand = 40667;
constexpr int discardCommand = 40668;

struct Shortcut {
    UINT key;
    ShortcutAction action;
    const wchar_t* label;
    const wchar_t* atomName;
    ATOM id = 0;
};

Shortcut shortcuts[] = {
    {'R', ShortcutAction::recordOrDiscard, L"Ctrl+Alt+R", L"ReaperGlobalRecord.Start"},
    {VK_SPACE, ShortcutAction::keep, L"Ctrl+Alt+Пробел", L"ReaperGlobalRecord.Keep"},
};

HWND messageWindow = nullptr;
HINSTANCE module = nullptr;
bool classRegistered = false;
constexpr char settingsSection[] = "GlobalRecord";
constexpr char enabledKey[] = "HotkeysEnabled";
custom_action_register_t toolbarAction{
    0, "GlobalRecordToggleHotkeys", "Global Record: Глобальные клавиши записи", nullptr
};
int toggleCommand = 0;
bool commandHook = false;
bool toggleHook = false;
bool toolbarTimer = false;

// Native extension APIs in REAPER 7.86, absent from the generated ReaScript header.
using GetToolbarItem = bool (*)(const char*, int, int*, int*, const char**, const char**);
using AddToolbarItem = bool (*)(const char*, int, int, int, const char*, const char*, int);
GetToolbarItem getToolbarItem = nullptr;
AddToolbarItem addToolbarItem = nullptr;

bool hasArmedTrack()
{
    const int count = CountTracks(nullptr);
    for (int index = 0; index < count; ++index) {
        MediaTrack* track = GetTrack(nullptr, index);
        if (GetMediaTrackInfo_Value(track, "I_RECARM") != 0)
            return true;
    }
    return false;
}

void runAction(ShortcutAction action)
{
    if (!IsWindowEnabled(GetMainHwnd()))
        return;

    const int state = GetPlayState();
    if (state & recordingFlag) {
        // The native stop action can move the edit cursor to the end of the
        // pass. Preserve the cue for a retry or later manual playback.
        const double cue = GetCursorPosition();
        Main_OnCommand(action == ShortcutAction::keep ? keepCommand : discardCommand, 0);
        if (GetPlayState() == 0 && GetCursorPosition() != cue)
            SetEditCurPos(cue, false, false);
        return;
    }
    if (GetAllProjectPlayStates(nullptr) & recordingFlag)
        return;
    if (action == ShortcutAction::keep) {
        if (state & 1)
            OnStopButton();
        return;
    }
    if (!hasArmedTrack()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    Main_OnCommand(recordCommand, 0);
}

LRESULT CALLBACK handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_HOTKEY) {
        for (const Shortcut& shortcut : shortcuts) {
            if (shortcut.id != 0 && wParam == shortcut.id) {
                runAction(shortcut.action);
                break;
            }
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void releaseShortcuts()
{
    for (Shortcut& shortcut : shortcuts) {
        if (shortcut.id != 0) {
            UnregisterHotKey(messageWindow, shortcut.id);
            GlobalDeleteAtom(shortcut.id);
            shortcut.id = 0;
        }
    }
    if (messageWindow) {
        DestroyWindow(messageWindow);
        messageWindow = nullptr;
    }
    if (classRegistered) {
        UnregisterClassW(windowClass, module);
        classRegistered = false;
    }
}

std::wstring registerShortcuts()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = handleMessage;
    wc.hInstance = module;
    wc.lpszClassName = windowClass;
    classRegistered = RegisterClassW(&wc) != 0;
    if (!classRegistered)
        return L"Не удалось зарегистрировать обработчик горячих клавиш Windows.";

    // REAPER loads extensions on its UI thread. WM_HOTKEY is dispatched on
    // that same thread, with no focus change or key injection into the editor.
    messageWindow = CreateWindowExW(0, windowClass, L"", 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, nullptr, module, nullptr);
    if (!messageWindow)
        return L"Не удалось создать окно для получения горячих клавиш Windows.";

    for (Shortcut& shortcut : shortcuts) {
        shortcut.id = GlobalAddAtomW(shortcut.atomName);
        if (!shortcut.id || !RegisterHotKey(messageWindow, shortcut.id, modifiers, shortcut.key)) {
            return std::wstring(L"Не удалось включить ") + shortcut.label +
                L". Возможно, сочетание занято другой программой или второй копией REAPER. "
                L"Освободите сочетание и нажмите кнопку глобальных клавиш ещё раз.";
        }
    }
    return {};
}

bool setShortcutsEnabled(bool enabled, bool reportError)
{
    if (!enabled) {
        releaseShortcuts();
        return true;
    }
    const std::wstring error = registerShortcuts();
    if (error.empty())
        return true;

    releaseShortcuts();
    if (reportError)
        MessageBoxW(GetMainHwnd(), error.c_str(), L"REAPER Global Record", MB_OK | MB_ICONWARNING);
    return false;
}

bool handleToolbarAction(KbdSectionInfo*, int command, int, int, int, HWND)
{
    if (command != toggleCommand)
        return false;
    const bool enable = !messageWindow;
    if (setShortcutsEnabled(enable, true))
        SetExtState(settingsSection, enabledKey, enable ? "1" : "0", true);
    RefreshToolbar2(0, toggleCommand);
    return true;
}

int toolbarState(int command)
{
    if (command != toggleCommand)
        return -1;
    return messageWindow ? 1 : 0;
}

void installToolbarButton()
{
    // The first UI timer runs after REAPER has loaded the user's toolbar.
    plugin_register("-timer", reinterpret_cast<void*>(installToolbarButton));
    toolbarTimer = false;
    constexpr char name[] = "Main toolbar";
    int existingCommand = 0;
    for (int i = 0; getToolbarItem(name, i, &existingCommand, nullptr, nullptr, nullptr); ++i) {
        if (existingCommand == toggleCommand)
            return;
    }
    addToolbarItem(name, -1, toggleCommand, 0, "Глобальные клавиши записи",
                   "toolbar_global_record_keys.png", 0);
}

void unregisterAction()
{
    if (toolbarTimer)
        plugin_register("-timer", reinterpret_cast<void*>(installToolbarButton));
    if (toggleHook)
        plugin_register("-toggleaction", reinterpret_cast<void*>(toolbarState));
    if (commandHook)
        plugin_register("-hookcommand2", reinterpret_cast<void*>(handleToolbarAction));
    if (toggleCommand)
        plugin_register("-custom_action", &toolbarAction);
    toolbarTimer = toggleHook = commandHook = false;
    toggleCommand = 0;
}
} // namespace

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
    HINSTANCE instance, reaper_plugin_info_t* host)
{
    if (!host) {
        releaseShortcuts();
        unregisterAction();
        return 0;
    }
    if (host->caller_version != REAPER_PLUGIN_VERSION || !host->GetFunc ||
        REAPERAPI_LoadAPI(host->GetFunc) != 0)
        return 0;

    module = instance;
    toggleCommand = plugin_register("custom_action", &toolbarAction);
    if (!toggleCommand)
        return 0;
    commandHook = plugin_register("hookcommand2", reinterpret_cast<void*>(handleToolbarAction)) != 0;
    toggleHook = plugin_register("toggleaction", reinterpret_cast<void*>(toolbarState)) != 0;
    if (!commandHook || !toggleHook) {
        unregisterAction();
        return 0;
    }
    const char* saved = GetExtState(settingsSection, enabledKey);
    if (!saved || strcmp(saved, "0") != 0)
        setShortcutsEnabled(true, false);

    getToolbarItem = reinterpret_cast<GetToolbarItem>(host->GetFunc("GetCustomMenuOrToolbarItem"));
    addToolbarItem = reinterpret_cast<AddToolbarItem>(host->GetFunc("AddCustomMenuOrToolbarItem"));
    if (getToolbarItem && addToolbarItem)
        toolbarTimer = plugin_register("timer", reinterpret_cast<void*>(installToolbarButton)) != 0;
    static char name[] = "Global Record";
    host->Register("ext_name", name);
    return 1;
}
