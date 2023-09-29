#if UNITY_EDITOR
using System;
using System.Runtime.InteropServices;
using UnityEditor;
using UnityEngine;
using WLI = AN.Win32.WindowLongIndex;

namespace AN
{
    static class DarkMode
    {
        static IntPtr mainWindow;
        static IntPtr defWndProc;
        
        static bool appFocused;

        private static bool darkModeEnabled = true;

        [InitializeOnLoadMethod]
        static void Init()
        {
            AssemblyReloadEvents.beforeAssemblyReload += BeforeAssemblyReload;
            AssemblyReloadEvents.afterAssemblyReload += AfterAssemblyReload;
            EditorApplication.quitting += OnEditorQuitting;
            // EditorApplication.update += OnEditorUpdate;
            
            FindMainWindowHandle();
            
            EditorApplication.delayCall += () =>
            {
                darkModeEnabled = EditorGUIUtility.isProSkin;
                FindMainWindowHandle();
                InitDarkMode();
                SetWndProc();
                SetDarkMode(darkModeEnabled, false);
                SetWindowDarkMode(mainWindow, darkModeEnabled);
            };
        }

        static void AfterAssemblyReload()
        {
            FindMainWindowHandle();
            SetWndProc();
            // force redraw menu bar
            SetDarkMode(darkModeEnabled, false);
            SetWindowDarkMode(mainWindow, darkModeEnabled);
        }
        
        static void BeforeAssemblyReload()
        {
            if (defWndProc == IntPtr.Zero) return;
            ResetWndProc();
        }
        
        private static void OnEditorQuitting()
        {
            ResetWndProc();
        }
        
        private static void OnEditorUpdate()
        {
            var isAppActive = UnityEditorInternal.InternalEditorUtility.isApplicationActive;
            if (!appFocused && isAppActive)
                OnUnityFocus(appFocused = true);
            else if (appFocused && !isAppActive)
                OnUnityFocus(appFocused = false);
        }
        
        static void OnUnityFocus(bool focus)
        {
            // Auto Refresh on alt-tab doesn't work in Borderless mode
            if (focus && defWndProc != IntPtr.Zero)
                AssetDatabase.Refresh();
        }
        
        static void FindMainWindowHandle()
        {
            var unityVersion = Application.unityVersion;
            uint threadId = Win32.GetCurrentThreadId();
            Win32.EnumThreadWindows(threadId, (hWnd, lParam) =>
            {
                var title = Win32.GetWindowTitle(hWnd);
                if (title.Contains(unityVersion))
                {
                    mainWindow = hWnd;
                    //Debug.Log(title);
                    return false;
                }
                return true;
            }, IntPtr.Zero);
        }
        
        static void SetWndProc()
        {
            if (defWndProc == IntPtr.Zero)
            {
                var targetWindow = mainWindow;
                defWndProc = Win32.GetWindowLong(targetWindow, WLI.WindowProc);

                SetDefWndProc(defWndProc);

                Win32.SetWindowLong(targetWindow, WLI.WindowProc, GetDarkModeWndProc());
            }
        }
        
        static void ResetWndProc()
        {
            if (defWndProc != IntPtr.Zero)
            {
                Win32.SetWindowLong(mainWindow, WLI.WindowProc, defWndProc);
                defWndProc = IntPtr.Zero;
            }
        }
        
        // static IntPtr WindowProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
        // {
        //     // if (hWnd == IntPtr.Zero)
        //     //     return IntPtr.Zero;
        //
        //     // IntPtr lr;
        //     // if (darkModeEnabled && DarkModeWndProc(hWnd, msg, wParam, lParam, out lr, defWndProc))
        //     // {
        //     //     return lr;
        //     // }
        //
        //     return Win32.CallWindowProc(defWndProc, hWnd, msg, wParam, lParam);
        // }

        [DllImport("ANUnityLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void InitDarkMode();
        
        [DllImport("ANUnityLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void SetDarkMode([MarshalAs(UnmanagedType.Bool)] bool useDark, [MarshalAs(UnmanagedType.Bool)] bool fixDarkScrollbar);
        
        // [DllImport("ANUnityLib.dll", CallingConvention = CallingConvention.Cdecl)]
        // [return: MarshalAs(UnmanagedType.Bool)]
        // private static extern bool DarkModeWndProc(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam, out IntPtr lr, IntPtr defaultProc);
        
        [DllImport("ANUnityLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr DarkModeWndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [DllImport("ANUnityLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr GetDarkModeWndProc();
        
        [DllImport("ANUnityLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void SetWindowDarkMode(IntPtr hWnd, [MarshalAs(UnmanagedType.Bool)] bool darkMode);

        [DllImport("ANUnityLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void SetDefWndProc(IntPtr wndProc);
        
        [DllImport("kernel32.dll")]
        private static extern IntPtr LoadLibrary(string dllName);

        [DllImport("kernel32.dll")]
        private static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

        [DllImport("kernel32.dll")]
        private static extern bool FreeLibrary(IntPtr hModule);
    }
}

#endif//UNITY_EDITOR