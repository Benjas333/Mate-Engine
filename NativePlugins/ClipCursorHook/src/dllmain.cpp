#define WIN32_LEAN_AND_MEAN
#include "MinHook.h"
#include <windows.h>

static RECT g_lastExternalClip = {};
static BOOL g_hasExternalClip = FALSE;
static BOOL g_ourExplicitClip = FALSE;
static thread_local int g_windowOpDepth = 0;
static thread_local BOOL g_clipCursorCalledDuringWindowOp = FALSE;
static thread_local RECT g_windowOpClipTarget = {};
static thread_local BOOL g_windowOpClipTargetIsNull = FALSE;

typedef BOOL(WINAPI *FnClipCursor)(const RECT *);
typedef BOOL(WINAPI *FnSetWindowPos)(HWND, HWND, int, int, int, int, UINT);
typedef BOOL(WINAPI *FnMoveWindow)(HWND, int, int, int, int, BOOL);

static FnClipCursor fpClipCursor = nullptr;
static FnSetWindowPos fpSetWindowPos = nullptr;
static FnMoveWindow fpMoveWindow = nullptr;

static BOOL RectsEqual(const RECT &a, const RECT &b) {
  return a.left == b.left && a.top == b.top && a.right == b.right &&
         a.bottom == b.bottom;
}

static RECT GetVirtualScreenRect() {
  RECT rect;
  rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  rect.right = rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  rect.bottom = rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
  return rect;
}

static BOOL GetCurrentClip(RECT *rect) { return GetClipCursor(rect); }

static void RestoreClip(const RECT &rect) { fpClipCursor(&rect); }

static BOOL IsCursorVisible() {
  CURSORINFO cursorInfo = {};
  cursorInfo.cbSize = sizeof(cursorInfo);
  return GetCursorInfo(&cursorInfo) &&
         (cursorInfo.flags & CURSOR_SHOWING) == CURSOR_SHOWING;
}

struct DelayedRestoreState {
  RECT clip;
};

static DWORD WINAPI DelayedRestoreWorker(LPVOID param) {
  DelayedRestoreState *state = static_cast<DelayedRestoreState *>(param);
  RECT clip = state->clip;
  delete state;

  Sleep(15);

  RECT current;
  if (GetCurrentClip(&current) && RectsEqual(current, GetVirtualScreenRect()) &&
      !IsCursorVisible()) {
    RestoreClip(clip);
  }

  return 0;
}

static void QueueDelayedRestore(const RECT &clip) {
  DelayedRestoreState *state = new DelayedRestoreState{clip};
  HANDLE thread = CreateThread(nullptr, 0, DelayedRestoreWorker, state, 0, nullptr);
  if (thread) {
    CloseHandle(thread);
  } else {
    delete state;
  }
}

static BOOL IsWindowOpReset(const RECT &before, const RECT &after) {
  if (RectsEqual(before, after))
    return FALSE;

  if (g_clipCursorCalledDuringWindowOp) {
    RECT target = g_windowOpClipTargetIsNull ? GetVirtualScreenRect()
                                             : g_windowOpClipTarget;
    return RectsEqual(after, target);
  }

  // Some SetWindowPos/MoveWindow paths reset the clip without calling the
  // exported ClipCursor function. Treat only a reset to the full virtual screen
  // as ours; a different post-op rect is likely a concurrent game capture.
  return RectsEqual(after, GetVirtualScreenRect()) && !IsCursorVisible();
}

BOOL WINAPI HookedClipCursor(const RECT *rect) {
  if (g_windowOpDepth > 0) {
    g_clipCursorCalledDuringWindowOp = TRUE;
    if (rect) {
      g_windowOpClipTarget = *rect;
      g_windowOpClipTargetIsNull = FALSE;
    } else {
      g_windowOpClipTargetIsNull = TRUE;
    }

    return fpClipCursor(rect);
  }

  if (g_ourExplicitClip) {
    g_ourExplicitClip = FALSE;
    return fpClipCursor(rect);
  }

  if (rect) {
    g_lastExternalClip = *rect;
    g_hasExternalClip = TRUE;
  } else {
    g_hasExternalClip = FALSE;
  }

  return fpClipCursor(rect);
}

BOOL WINAPI HookedSetWindowPos(HWND hWnd, HWND hWndInsertAfter, int x, int y,
                               int cx, int cy, UINT uFlags) {
  RECT before;
  BOOL hadBefore = GetCurrentClip(&before);

  ++g_windowOpDepth;
  g_clipCursorCalledDuringWindowOp = FALSE;
  BOOL result = fpSetWindowPos(hWnd, hWndInsertAfter, x, y, cx, cy, uFlags);
  --g_windowOpDepth;

  RECT after;
  if (hadBefore && GetCurrentClip(&after) && IsWindowOpReset(before, after))
    QueueDelayedRestore(before);

  return result;
}

BOOL WINAPI HookedMoveWindow(HWND hWnd, int x, int y, int cx, int cy,
                             BOOL bRepaint) {
  RECT before;
  BOOL hadBefore = GetCurrentClip(&before);

  ++g_windowOpDepth;
  g_clipCursorCalledDuringWindowOp = FALSE;
  BOOL result = fpMoveWindow(hWnd, x, y, cx, cy, bRepaint);
  --g_windowOpDepth;

  RECT after;
  if (hadBefore && GetCurrentClip(&after) && IsWindowOpReset(before, after))
    QueueDelayedRestore(before);

  return result;
}

extern "C" {
__declspec(dllexport) BOOL InstallHooks() {
  if (MH_Initialize() != MH_OK)
    return FALSE;

  if (MH_CreateHookApi(L"user32", "ClipCursor",
                       reinterpret_cast<LPVOID>(&HookedClipCursor),
                       reinterpret_cast<LPVOID *>(&fpClipCursor)) != MH_OK) {
    return FALSE;
  }

  if (MH_CreateHookApi(L"user32", "SetWindowPos",
                       reinterpret_cast<LPVOID>(&HookedSetWindowPos),
                       reinterpret_cast<LPVOID *>(&fpSetWindowPos)) != MH_OK) {
    return FALSE;
  }

  if (MH_CreateHookApi(L"user32", "MoveWindow",
                       reinterpret_cast<LPVOID>(&HookedMoveWindow),
                       reinterpret_cast<LPVOID *>(&fpMoveWindow)) != MH_OK) {
    return FALSE;
  }

  return MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
}

__declspec(dllexport) void UpdateExternalClip(const RECT *rect) {
  if (g_windowOpDepth > 0)
    return;

  if (rect) {
    g_lastExternalClip = *rect;
    g_hasExternalClip = TRUE;
  } else {
    g_hasExternalClip = FALSE;
  }
}

__declspec(dllexport) void NotifyExplicitClip() { g_ourExplicitClip = TRUE; }

__declspec(dllexport) void UninstallHooks() {
  MH_DisableHook(MH_ALL_HOOKS);
  MH_Uninitialize();
}
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_DETACH)
    UninstallHooks();
  return TRUE;
}
