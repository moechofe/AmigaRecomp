"""Inject keyboard and pointer input into an X server, with nothing but ctypes.

tools/capture.py runs FS-UAE on a private Xvfb and drives it through this: XTEST events are
real input to that server. The server has no window manager, so a window is found by walking
the root's children and focused with XSetInputFocus. The libraries are loaded when a Display
is made, so importing this module works on any platform.
"""

from __future__ import annotations

import ctypes
import time
from ctypes import c_char_p, c_int, c_long, c_ubyte, c_uint, c_ulong, c_void_p, POINTER

ANY_PROPERTY_TYPE = 0
REVERT_TO_PARENT = 2
STEP = 8                        # FS-UAE clamps one motion event to -128..127; stay well inside


class Display:
    def __init__(self, name: str) -> None:
        self.name = name
        try:
            self.x = ctypes.CDLL("libX11.so.6")
            self.xtst = ctypes.CDLL("libXtst.so.6")
        except OSError as error:
            raise SystemExit(f"libX11/libXtst are not installed: {error}")
        x, t = self.x, self.xtst
        x.XOpenDisplay.argtypes, x.XOpenDisplay.restype = [c_char_p], c_void_p
        x.XDefaultRootWindow.argtypes, x.XDefaultRootWindow.restype = [c_void_p], c_ulong
        x.XInternAtom.argtypes, x.XInternAtom.restype = [c_void_p, c_char_p, c_int], c_ulong
        x.XFlush.argtypes = [c_void_p]
        x.XFree.argtypes = [c_void_p]
        x.XCloseDisplay.argtypes = [c_void_p]
        x.XSetInputFocus.argtypes = [c_void_p, c_ulong, c_int, c_ulong]
        x.XGetInputFocus.argtypes = [c_void_p, POINTER(c_ulong), POINTER(c_int)]
        x.XQueryTree.argtypes = [c_void_p, c_ulong, POINTER(c_ulong), POINTER(c_ulong),
                                 POINTER(POINTER(c_ulong)), POINTER(c_uint)]
        x.XStringToKeysym.argtypes, x.XStringToKeysym.restype = [c_char_p], c_ulong
        x.XKeysymToKeycode.argtypes, x.XKeysymToKeycode.restype = [c_void_p, c_ulong], c_ubyte
        x.XGetWindowProperty.argtypes = [
            c_void_p, c_ulong, c_ulong, c_long, c_long, c_int, c_ulong, POINTER(c_ulong),
            POINTER(c_int), POINTER(c_ulong), POINTER(c_ulong), POINTER(POINTER(c_ubyte))]
        x.XGetWindowProperty.restype = c_int
        handler = ctypes.CFUNCTYPE(c_int, c_void_p, c_void_p)
        self._ignore = handler(lambda display, event: 0)   # a window that vanished is not fatal
        x.XSetErrorHandler.argtypes = [handler]
        x.XSetErrorHandler(self._ignore)
        t.XTestQueryExtension.argtypes = [c_void_p, POINTER(c_int), POINTER(c_int),
                                          POINTER(c_int), POINTER(c_int)]
        t.XTestFakeKeyEvent.argtypes = [c_void_p, c_uint, c_int, c_ulong]
        t.XTestFakeButtonEvent.argtypes = [c_void_p, c_uint, c_int, c_ulong]
        t.XTestFakeRelativeMotionEvent.argtypes = [c_void_p, c_int, c_int, c_ulong]

        for _ in range(20):                 # a server just started may not be accepting yet
            self.dpy = x.XOpenDisplay(name.encode())
            if self.dpy:
                break
            time.sleep(0.25)
        if not self.dpy:
            raise SystemExit(f"cannot open the X display {name}")
        values = [c_int(), c_int(), c_int(), c_int()]
        if not t.XTestQueryExtension(self.dpy, *(ctypes.byref(v) for v in values)):
            raise SystemExit("the X server has no XTEST extension")
        self.root = x.XDefaultRootWindow(self.dpy)
        self._atoms: dict[str, int] = {}

    def close(self) -> None:
        self.x.XCloseDisplay(self.dpy)

    def atom(self, name: str) -> int:
        if name not in self._atoms:
            self._atoms[name] = self.x.XInternAtom(self.dpy, name.encode(), 0)
        return self._atoms[name]

    def property(self, window: int, name: str, length: int = 1024) -> list[int] | bytes | None:
        """Format-32 properties come back as a list of ints, format-8 as bytes."""
        kind, form, count, after = c_ulong(), c_int(), c_ulong(), c_ulong()
        data = POINTER(c_ubyte)()
        status = self.x.XGetWindowProperty(
            self.dpy, window, self.atom(name), 0, length, 0, ANY_PROPERTY_TYPE,
            ctypes.byref(kind), ctypes.byref(form), ctypes.byref(count), ctypes.byref(after),
            ctypes.byref(data))
        if status != 0 or not data or not count.value:
            return None
        try:
            if form.value == 32:
                return list(ctypes.cast(data, POINTER(c_long))[:count.value])
            if form.value == 8:
                return ctypes.string_at(data, count.value)
            return None
        finally:
            self.x.XFree(data)

    def windows(self) -> list[tuple[int, int | None, str]]:
        """The root's children: (window, _NET_WM_PID, title)."""
        root, parent, count = c_ulong(), c_ulong(), c_uint()
        children = POINTER(c_ulong)()
        if not self.x.XQueryTree(self.dpy, self.root, ctypes.byref(root), ctypes.byref(parent),
                                 ctypes.byref(children), ctypes.byref(count)):
            return []
        found = []
        try:
            for window in children[:count.value]:
                pid = self.property(window, "_NET_WM_PID")
                title = (self.property(window, "_NET_WM_NAME")
                         or self.property(window, "WM_NAME") or b"")
                found.append((window, pid[0] if pid else None, title.decode("utf-8", "replace")))
        finally:
            if children:
                self.x.XFree(children)
        return found

    def window_of(self, pid: int, title: str = "") -> int | None:
        """The window whose _NET_WM_PID is `pid`; failing that, the first whose title starts
        with `title`. A sandboxed program reports a PID from inside its own PID namespace, so
        the PID of the process that was launched never matches."""
        clients = self.windows()
        for window, owner, _ in clients:
            if owner == pid:
                return window
        if title:
            for window, _, name in clients:
                if name.startswith(title):
                    return window
        return None

    def active(self) -> int:
        focus, revert = c_ulong(), c_int()
        self.x.XGetInputFocus(self.dpy, ctypes.byref(focus), ctypes.byref(revert))
        return focus.value

    def activate(self, window: int) -> bool:
        """Give the window the keyboard focus, and say whether it has it."""
        if self.active() != window:
            self.x.XSetInputFocus(self.dpy, window, REVERT_TO_PARENT, 0)
            self.x.XFlush(self.dpy)
            time.sleep(0.05)
        return self.active() == window

    def chord(self, keysyms: list[str], hold: float = 0.0) -> None:
        """Press the keys together and release them in reverse."""
        codes = []
        for name in keysyms:
            code = self.x.XKeysymToKeycode(self.dpy, self.x.XStringToKeysym(name.encode()))
            if not code:
                raise SystemExit(f"no keycode for {name}")
            codes.append(code)
        for code in codes:
            self.xtst.XTestFakeKeyEvent(self.dpy, code, 1, 0)
        self.x.XFlush(self.dpy)
        time.sleep(hold)
        for code in reversed(codes):
            self.xtst.XTestFakeKeyEvent(self.dpy, code, 0, 0)
        self.x.XFlush(self.dpy)

    def button(self, number: int = 1, hold: float = 0.08) -> None:
        self.xtst.XTestFakeButtonEvent(self.dpy, number, 1, 0)
        self.x.XFlush(self.dpy)
        time.sleep(hold)
        self.xtst.XTestFakeButtonEvent(self.dpy, number, 0, 0)
        self.x.XFlush(self.dpy)

    def move(self, dx: int, dy: int) -> None:
        """Relative pointer motion, in steps of at most STEP pixels per axis."""
        while dx or dy:
            sx = max(-STEP, min(STEP, dx))
            sy = max(-STEP, min(STEP, dy))
            self.xtst.XTestFakeRelativeMotionEvent(self.dpy, sx, sy, 0)
            self.x.XFlush(self.dpy)
            dx, dy = dx - sx, dy - sy
            time.sleep(0.01)
