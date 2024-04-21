#if UNITY_EDITOR
using System;
using System.Runtime.InteropServices;
using UnityEditor;
using UnityEngine;
using UnityEngine.Assertions;
using WLI = AN.Win32.WindowLongIndex;

namespace AN
{
    static class DarkMode
    {
        private const string s_LibName = "UnityEditorDarkWin32.dll";

        [InitializeOnLoadMethod]
        static void Init()
        {
            EditorApplication.delayCall += () =>
            {
                InitDarkModeOnce(Application.unityVersion);
                RefreshDarkMode(EditorGUIUtility.isProSkin, false);
            };
        }
        
        [DllImport(s_LibName)]
        private static extern void InitDarkModeOnce([MarshalAs(UnmanagedType.LPWStr)] string unityVersion);

        [DllImport(s_LibName)]
        private static extern void RefreshDarkMode(bool useDark, bool fixDarkScrollbar);
    }
}

#endif//UNITY_EDITOR