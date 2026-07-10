using System;
using System.Runtime.InteropServices;
using UnityEngine;

public class ClipCursorHook : MonoBehaviour
{
    [DllImport("ClipCursorHook")]
    static extern bool InstallHooks();

    [DllImport("ClipCursorHook")]
    static extern void UpdateExternalClip(ref RECT rect);

    [DllImport("ClipCursorHook")]
    static extern void UpdateExternalClip(IntPtr zero);

    [DllImport("ClipCursorHook")]
    static extern void NotifyExplicitClip();

    [DllImport("ClipCursorHook")]
    static extern void UninstallHooks();

    [DllImport("user32.dll")]
    static extern bool GetClipCursor(out RECT rect);

    [DllImport("user32.dll")]
    static extern bool ClipCursor(ref RECT rect);

    [DllImport("user32.dll")]
    static extern bool ClipCursor(IntPtr zero);

    [DllImport("user32.dll")]
    static extern int GetSystemMetrics(int nIndex);

    const int SM_XVIRTUALSCREEN = 76;
    const int SM_YVIRTUALSCREEN = 77;
    const int SM_CXVIRTUALSCREEN = 78;
    const int SM_CYVIRTUALSCREEN = 79;

    [StructLayout(LayoutKind.Sequential)]
    struct RECT
    {
        public int Left,
            Top,
            Right,
            Bottom;

        public readonly bool Equals(RECT o) =>
            Left == o.Left && Top == o.Top && Right == o.Right && Bottom == o.Bottom;
    }

    private RECT _lastPolledClip;
    private bool _lastPolledHasClip;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
    static void AutoInstall()
    {
        var go = new GameObject("[ClipCursorHook]");
        DontDestroyOnLoad(go);
        go.AddComponent<ClipCursorHook>();
    }

    void OnEnable()
    {
        try
        {
            bool ok = InstallHooks();
            Debug.Log($"[ClipHook] Installed hooks: {ok}");
        }
        catch (DllNotFoundException e)
        {
            Debug.LogError($"[ClipHook] DLL not found: {e.Message}");
        }
        catch (Exception e)
        {
            Debug.LogError($"[ClipHook] Unexpected error: {e}");
        }

        _lastPolledHasClip = TryGetRestrictedClip(out _lastPolledClip);
        InvokeRepeating(nameof(Poll), 0f, 0.01f);
    }

    void OnDisable()
    {
        CancelInvoke(nameof(Poll));
        UninstallHooks();
    }

    void Poll()
    {
        bool hasClip = TryGetRestrictedClip(out RECT current);

        bool changed =
            hasClip != _lastPolledHasClip || (hasClip && !current.Equals(_lastPolledClip));

        if (!changed)
            return;

        if (hasClip)
            UpdateExternalClip(ref current);
        else
            UpdateExternalClip(IntPtr.Zero);

        _lastPolledClip = current;
        _lastPolledHasClip = hasClip;
    }

    public void SetOurClip(RectInt area)
    {
        NotifyExplicitClip();
        var r = new RECT
        {
            Left = area.xMin,
            Top = area.yMin,
            Right = area.xMax,
            Bottom = area.yMax,
        };
        ClipCursor(ref r);
    }

    public void ReleaseOurClip()
    {
        NotifyExplicitClip();
        ClipCursor(IntPtr.Zero);
    }

    static bool TryGetRestrictedClip(out RECT clip)
    {
        if (!GetClipCursor(out clip))
            return false;

        return !clip.Equals(GetVirtualScreenRect());
    }

    static RECT GetVirtualScreenRect()
    {
        int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        return new RECT
        {
            Left = left,
            Top = top,
            Right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN),
            Bottom = top + GetSystemMetrics(SM_CYVIRTUALSCREEN),
        };
    }
}
