#include "UAHMenuBar.h"
#include "IatHook.h"

#include <uxtheme.h>
#include <vssym32.h>

#include <mutex>
#include <unordered_set>

#pragma comment(lib, "comctl32.lib")
#define EXPORT_MONO(ret) extern "C" __declspec(dllexport) ret __stdcall

#if defined(__GNUC__) && __GNUC__ > 8
#define WINAPI_LAMBDA_RETURN(return_t) -> return_t WINAPI
#elif defined(__GNUC__)
#define WINAPI_LAMBDA_RETURN(return_t) WINAPI -> return_t
#else
#define WINAPI_LAMBDA_RETURN(return_t) -> return_t
#endif

enum IMMERSIVE_HC_CACHE_MODE
{
	IHCM_USE_CACHED_VALUE,
	IHCM_REFRESH
};

// 1903 18362
enum class PreferredAppMode
{
	Default,
	AllowDark,
	ForceDark,
	ForceLight,
	Max
};

enum WINDOWCOMPOSITIONATTRIB
{
	WCA_UNDEFINED = 0,
	WCA_NCRENDERING_ENABLED = 1,
	WCA_NCRENDERING_POLICY = 2,
	WCA_TRANSITIONS_FORCEDISABLED = 3,
	WCA_ALLOW_NCPAINT = 4,
	WCA_CAPTION_BUTTON_BOUNDS = 5,
	WCA_NONCLIENT_RTL_LAYOUT = 6,
	WCA_FORCE_ICONIC_REPRESENTATION = 7,
	WCA_EXTENDED_FRAME_BOUNDS = 8,
	WCA_HAS_ICONIC_BITMAP = 9,
	WCA_THEME_ATTRIBUTES = 10,
	WCA_NCRENDERING_EXILED = 11,
	WCA_NCADORNMENTINFO = 12,
	WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
	WCA_VIDEO_OVERLAY_ACTIVE = 14,
	WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
	WCA_DISALLOW_PEEK = 16,
	WCA_CLOAK = 17,
	WCA_CLOAKED = 18,
	WCA_ACCENT_POLICY = 19,
	WCA_FREEZE_REPRESENTATION = 20,
	WCA_EVER_UNCLOAKED = 21,
	WCA_VISUAL_OWNER = 22,
	WCA_HOLOGRAPHIC = 23,
	WCA_EXCLUDED_FROM_DDA = 24,
	WCA_PASSIVEUPDATEMODE = 25,
	WCA_USEDARKMODECOLORS = 26,
	WCA_LAST = 27
};

struct WINDOWCOMPOSITIONATTRIBDATA
{
	WINDOWCOMPOSITIONATTRIB Attrib;
	PVOID pvData;
	SIZE_T cbData;
};

using fnRtlGetNtVersionNumbers = void (WINAPI*)(LPDWORD major, LPDWORD minor, LPDWORD build);
using fnSetWindowCompositionAttribute = BOOL(WINAPI*)(HWND hWnd, WINDOWCOMPOSITIONATTRIBDATA*);
// 1809 17763
using fnShouldAppsUseDarkMode = bool (WINAPI*)(); // ordinal 132
using fnAllowDarkModeForWindow = bool (WINAPI*)(HWND hWnd, bool allow); // ordinal 133
using fnAllowDarkModeForApp = bool (WINAPI*)(bool allow); // ordinal 135, in 1809
using fnFlushMenuThemes = void (WINAPI*)(); // ordinal 136
using fnRefreshImmersiveColorPolicyState = void (WINAPI*)(); // ordinal 104
using fnIsDarkModeAllowedForWindow = bool (WINAPI*)(HWND hWnd); // ordinal 137
using fnGetIsImmersiveColorUsingHighContrast = bool (WINAPI*)(IMMERSIVE_HC_CACHE_MODE mode); // ordinal 106
using fnOpenNcThemeData = HTHEME(WINAPI*)(HWND hWnd, LPCWSTR pszClassList); // ordinal 49
// 1903 18362
using fnShouldSystemUseDarkMode = bool (WINAPI*)(); // ordinal 138
using fnSetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode appMode); // ordinal 135, in 1903
using fnIsDarkModeAllowedForApp = bool (WINAPI*)(); // ordinal 139

fnSetWindowCompositionAttribute _SetWindowCompositionAttribute = nullptr;
fnShouldAppsUseDarkMode _ShouldAppsUseDarkMode = nullptr;
fnAllowDarkModeForWindow _AllowDarkModeForWindow = nullptr;
fnAllowDarkModeForApp _AllowDarkModeForApp = nullptr;
fnFlushMenuThemes _FlushMenuThemes = nullptr;
fnRefreshImmersiveColorPolicyState _RefreshImmersiveColorPolicyState = nullptr;
fnIsDarkModeAllowedForWindow _IsDarkModeAllowedForWindow = nullptr;
fnGetIsImmersiveColorUsingHighContrast _GetIsImmersiveColorUsingHighContrast = nullptr;
fnOpenNcThemeData _OpenNcThemeData = nullptr;
// 1903 18362
//fnShouldSystemUseDarkMode _ShouldSystemUseDarkMode = nullptr;
fnSetPreferredAppMode _SetPreferredAppMode = nullptr;


bool g_darkModeSupported = false;
bool g_darkModeEnabled = false;
DWORD g_buildNumber = 0;

// limit dark scroll bar to specific windows and their children

std::unordered_set<HWND> g_darkScrollBarWindows;
std::mutex g_darkScrollBarMutex;

constexpr bool CheckBuildNumber(DWORD buildNumber)
{
	return (buildNumber == 17763 || // 1809
		buildNumber == 18362 || // 1903
		buildNumber == 18363 || // 1909
		buildNumber == 19041 || // 2004
		buildNumber == 19042 || // 20H2
		buildNumber == 19043 || // 21H1
		buildNumber == 19044 || // 21H2
		(buildNumber > 19044 && buildNumber < 22000) || // Windows 10 any version > 21H2 
		buildNumber >= 22000);  // Windows 11 builds
}

void AllowDarkModeForApp(bool allow)
{
	if (_AllowDarkModeForApp)
		_AllowDarkModeForApp(allow);
	else if (_SetPreferredAppMode)
		_SetPreferredAppMode(allow ? PreferredAppMode::ForceDark : PreferredAppMode::Default);
}

bool IsWindowOrParentUsingDarkScrollBar(HWND hwnd)
{
	HWND hwndRoot = GetAncestor(hwnd, GA_ROOT);

	std::lock_guard<std::mutex> lock(g_darkScrollBarMutex);
	if (g_darkScrollBarWindows.count(hwnd)) {
		return true;
	}
	if (hwnd != hwndRoot && g_darkScrollBarWindows.count(hwndRoot)) {
		return true;
	}

	return false;
}

void FixDarkScrollBar()
{
	HMODULE hComctl = LoadLibraryEx(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (hComctl)
	{
		auto addr = FindDelayLoadThunkInModule(hComctl, "uxtheme.dll", 49); // OpenNcThemeData
		if (addr)
		{
			DWORD oldProtect;
			if (VirtualProtect(addr, sizeof(IMAGE_THUNK_DATA), PAGE_READWRITE, &oldProtect) && _OpenNcThemeData)
			{
				auto MyOpenThemeData = [](HWND hWnd, LPCWSTR classList) WINAPI_LAMBDA_RETURN(HTHEME) {
					if (wcscmp(classList, L"ScrollBar") == 0)
					{
						if (IsWindowOrParentUsingDarkScrollBar(hWnd)) {
							hWnd = nullptr;
							classList = L"Explorer::ScrollBar";
						}
					}
					return _OpenNcThemeData(hWnd, classList);
				};

				addr->u1.Function = reinterpret_cast<ULONG_PTR>(static_cast<fnOpenNcThemeData>(MyOpenThemeData));
				VirtualProtect(addr, sizeof(IMAGE_THUNK_DATA), oldProtect, &oldProtect);
			}
		}
	}
}

bool IsHighContrast()
{
	HIGHCONTRASTW highContrast{};
	highContrast.cbSize = sizeof(HIGHCONTRASTW);
	if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(HIGHCONTRASTW), &highContrast, FALSE))
		return (highContrast.dwFlags & HCF_HIGHCONTRASTON) == HCF_HIGHCONTRASTON;
	return false;
}

void SetTitleBarThemeColor(HWND hWnd, BOOL dark)
{
	if (g_buildNumber < 18362)
		SetPropW(hWnd, L"UseImmersiveDarkModeColors", reinterpret_cast<HANDLE>(static_cast<intptr_t>(dark)));
	else if (_SetWindowCompositionAttribute)
	{
		WINDOWCOMPOSITIONATTRIBDATA data = { WCA_USEDARKMODECOLORS, &dark, sizeof(dark) };
		_SetWindowCompositionAttribute(hWnd, &data);
	}
}


//static WNDPROC s_DefWndProc = nullptr;
static HWND s_MainWindow = nullptr;

EXPORT_MONO(void) SetDarkMode(BOOL useDark, BOOL fixDarkScrollbar);
EXPORT_MONO(void) SetWindowDarkMode(HWND hWnd, BOOL darkMode);

EXPORT_MONO(LRESULT) DarkModeWndProc(HWND hWnd, UINT uMsg, WPARAM wParam,
	LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	//return UAHWndProc(hWnd, message, wParam, lParam, lr, defaultProc) ? TRUE : FALSE;

	LRESULT lr;
	if (g_darkModeEnabled && UAHWndProc(hWnd, uMsg, wParam, lParam, &lr, DefSubclassProc))
	{
		return lr;
	}

	return CallWindowProcA(DefSubclassProc, hWnd, uMsg, wParam, lParam);
}

EXPORT_MONO(void*) GetDarkModeWndProc(void)
{
	return (void*)DarkModeWndProc;
}

EXPORT_MONO(void) SetDefWndProc(WNDPROC wndProc)
{
	//s_DefWndProc = wndProc;
}

EXPORT_MONO(void) SetMainWindow(HWND hWnd)
{
	if (hWnd)
	{
		s_MainWindow = hWnd;
	}
}

EXPORT_MONO(void*) GetMainWindow(void)
{
	return (void*)s_MainWindow;
}

EXPORT_MONO(void) InitDarkMode(void)
{
	fnRtlGetNtVersionNumbers RtlGetNtVersionNumbers = nullptr;
	HMODULE hNtdllModule = GetModuleHandle(L"ntdll.dll");
	if (hNtdllModule)
	{
		RtlGetNtVersionNumbers = reinterpret_cast<fnRtlGetNtVersionNumbers>(GetProcAddress(hNtdllModule, "RtlGetNtVersionNumbers"));
	}

	if (RtlGetNtVersionNumbers)
	{
		DWORD major, minor;
		RtlGetNtVersionNumbers(&major, &minor, &g_buildNumber);
		g_buildNumber &= ~0xF0000000;
		if (major == 10 && minor == 0 && CheckBuildNumber(g_buildNumber))
		{
			HMODULE hUxtheme = LoadLibraryEx(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (hUxtheme)
			{
				_OpenNcThemeData = reinterpret_cast<fnOpenNcThemeData>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(49)));
				_RefreshImmersiveColorPolicyState = reinterpret_cast<fnRefreshImmersiveColorPolicyState>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(104)));
				_GetIsImmersiveColorUsingHighContrast = reinterpret_cast<fnGetIsImmersiveColorUsingHighContrast>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(106)));
				_ShouldAppsUseDarkMode = reinterpret_cast<fnShouldAppsUseDarkMode>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(132)));
				_AllowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133)));

				auto ord135 = GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
				if (g_buildNumber < 18362)
					_AllowDarkModeForApp = reinterpret_cast<fnAllowDarkModeForApp>(ord135);
				else
					_SetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(ord135);

				_FlushMenuThemes = reinterpret_cast<fnFlushMenuThemes>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136)));
				_IsDarkModeAllowedForWindow = reinterpret_cast<fnIsDarkModeAllowedForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(137)));

				HMODULE hUser32Module = GetModuleHandleW(L"user32.dll");
				if (hUser32Module)
				{
					_SetWindowCompositionAttribute = reinterpret_cast<fnSetWindowCompositionAttribute>(GetProcAddress(hUser32Module, "SetWindowCompositionAttribute"));
				}

				if (_OpenNcThemeData &&
					_RefreshImmersiveColorPolicyState &&
					_ShouldAppsUseDarkMode &&
					_AllowDarkModeForWindow &&
					(_AllowDarkModeForApp || _SetPreferredAppMode) &&
					_FlushMenuThemes &&
					_IsDarkModeAllowedForWindow)
				{
					g_darkModeSupported = true;
				}
			}
		}
	}
}

static std::wstring GetWindowTitle(HWND hWnd)
{
	int length = (int)SendMessage(hWnd, WM_GETTEXTLENGTH, 0, 0);
	std::wstring sbWindowTitle(length + 1, 0);
	SendMessage(hWnd, WM_GETTEXT, sbWindowTitle.size(), (LPARAM) sbWindowTitle.data());
	return sbWindowTitle;
}

EXPORT_MONO(void) InitDarkModeOnce(const wchar_t *unityVersion)
{
	static bool inited = false;
	if (!inited)
	{
		inited = true;
		InitDarkMode();
		EnumThreadWindows(GetCurrentThreadId(), [](HWND hWnd, LPARAM lParam) -> BOOL
		{
			const wchar_t* unityVersion = (const wchar_t *)lParam;
			std::wstring title = GetWindowTitle(hWnd);
			if (title.find(unityVersion) != std::wstring::npos)
			{
				SetMainWindow(hWnd);
				return FALSE;
			}
			return TRUE;
		}, (LPARAM)unityVersion);

		SetWindowSubclass(s_MainWindow, DarkModeWndProc, 0, 0);
	}
}

EXPORT_MONO(void) RefreshDarkMode(BOOL useDark, BOOL fixDarkScrollbar)
{
	SetDarkMode(useDark, fixDarkScrollbar);
	if (s_MainWindow != nullptr)
	{
		SetWindowDarkMode((HWND)s_MainWindow, useDark);
	}
}

EXPORT_MONO(void) FlushMenuThemes(void)
{
	if (_FlushMenuThemes)
	{
		_FlushMenuThemes();
	}
}

EXPORT_MONO(void) SetDarkMode(BOOL useDark, BOOL fixDarkScrollbar)
{
	if (g_darkModeSupported)
	{
		AllowDarkModeForApp(useDark);
		//_RefreshImmersiveColorPolicyState();
		FlushMenuThemes();
		if (fixDarkScrollbar)
		{
			FixDarkScrollBar();
		}
		g_darkModeEnabled = useDark;
		//g_darkModeEnabled = ShouldAppsUseDarkMode() && !IsHighContrast();
	}
}

EXPORT_MONO(BOOL) AllowDarkModeForWindow(HWND hWnd, BOOL allow)
{
	if (g_darkModeSupported && _AllowDarkModeForWindow)
		return _AllowDarkModeForWindow(hWnd, allow) ? TRUE : FALSE;
	return FALSE;
}


EXPORT_MONO(void) RefreshTitleBarThemeColor(HWND hWnd)
{
	BOOL dark = FALSE;
	if (_IsDarkModeAllowedForWindow && _ShouldAppsUseDarkMode)
	{
		if (_IsDarkModeAllowedForWindow(hWnd) && _ShouldAppsUseDarkMode() && !IsHighContrast())
		{
			dark = TRUE;
		}
	}
	SetTitleBarThemeColor(hWnd, dark);
}

EXPORT_MONO(void) SetWindowDarkMode(HWND hWnd, BOOL darkMode)
{
	SetDarkMode(darkMode, false);
	RefreshTitleBarThemeColor(hWnd);
	FlushMenuThemes();
	DrawMenuBar(hWnd);
}