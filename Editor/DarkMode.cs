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
                SetWindowDarkMode(mainWindow, darkModeEnabled);
            };
        }

        static void AfterAssemblyReload()
        {
            SetWndProc();
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

                Win32.WndProc newWndProc = WindowProc;
                Win32.SetWindowLong(targetWindow, WLI.WindowProc, newWndProc.Method.MethodHandle.GetFunctionPointer());
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
        
        static IntPtr WindowProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
        {
            if (hWnd == IntPtr.Zero)
                return IntPtr.Zero;

            IntPtr lr;
            if (darkModeEnabled && DarkModeWndProc(hWnd, msg, wParam, lParam, out lr))
            {
                return lr;
            }
      
            return Win32.CallWindowProc(defWndProc, hWnd, msg, wParam, lParam);
        }

        private static void OnEditorUpdate()
        {
        }

        [DllImport("ANUnityLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void InitDarkMode();
        
        [DllImport("ANUnityLib.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DarkModeWndProc(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam, out IntPtr lr);

        [DllImport("ANUnityLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void SetWindowDarkMode(IntPtr hWnd, [MarshalAs(UnmanagedType.Bool)] bool darkMode);
    }
}

#endif//UNITY_EDITOR