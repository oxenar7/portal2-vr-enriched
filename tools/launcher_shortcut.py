"""Create Windows shortcuts that carry the launcher icon and taskbar identity.

.cmd and .pyw files cannot have an icon of their own. The application ID written here
matches the window's, so a pinned shortcut and the running launcher share one button.
"""

from ctypes import HRESULT, POINTER, Structure, WINFUNCTYPE, byref, c_ubyte, c_ulong, c_ushort, c_void_p, c_wchar_p, \
    cast, create_unicode_buffer, windll
from pathlib import Path
import sys
from uuid import UUID

if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from tools import launcher_art as art
    from tools.launcher_paths import application_root
else:
    from . import launcher_art as art
    from .launcher_paths import application_root

SHORTCUT_NAME = "Portal 2 VR Enriched Launcher.lnk"


class GUID(Structure):
    _fields_ = [("data", c_ubyte * 16)]

    def __init__(self, text):
        super().__init__((c_ubyte * 16)(*UUID(text).bytes_le))


class PROPERTYKEY(Structure):
    _fields_ = [("fmtid", GUID), ("pid", c_ulong)]


class PROPVARIANT(Structure):
    _fields_ = [("vt", c_ushort), ("reserved", c_ushort * 3), ("value", c_wchar_p), ("padding", c_void_p)]


CLSID_SHELL_LINK = "00021401-0000-0000-c000-000000000046"
IID_SHELL_LINK = "000214f9-0000-0000-c000-000000000046"
IID_PERSIST_FILE = "0000010b-0000-0000-c000-000000000046"
IID_PROPERTY_STORE = "886d8eeb-8cf2-4446-8d02-cdba1dbdcf99"
PKEY_APP_USER_MODEL_ID = "9f4c2855-9f79-4b39-a8d0-e1d42de1d5f3", 5
VT_LPWSTR = 31


def _call(interface, index, argument_types, *arguments):
    """Call a COM method by vtable index; a failing HRESULT raises OSError."""
    vtable = cast(interface, POINTER(POINTER(c_void_p))).contents
    return WINFUNCTYPE(HRESULT, c_void_p, *argument_types)(vtable[index])(interface, *arguments)


def _query(interface, iid):
    result = c_void_p()
    _call(interface, 0, (POINTER(GUID), POINTER(c_void_p)), byref(GUID(iid)), byref(result))
    return result


def create_shortcut(path: Path, target: Path, script: Path, icon: Path) -> None:
    windll.ole32.CoInitialize(None)
    link = c_void_p()
    windll.ole32.CoCreateInstance.restype = HRESULT
    windll.ole32.CoCreateInstance(byref(GUID(CLSID_SHELL_LINK)), None, 1, byref(GUID(IID_SHELL_LINK)), byref(link))
    _call(link, 20, (c_wchar_p,), str(target))
    _call(link, 11, (c_wchar_p,), f'"{script}"')
    _call(link, 9, (c_wchar_p,), str(script.parent))
    _call(link, 17, (c_wchar_p, c_ulong), str(icon), 0)
    _call(link, 7, (c_wchar_p,), "Portal 2 VR Enriched Launcher")
    store = _query(link, IID_PROPERTY_STORE)
    key = PROPERTYKEY(GUID(PKEY_APP_USER_MODEL_ID[0]), PKEY_APP_USER_MODEL_ID[1])
    value = PROPVARIANT(vt=VT_LPWSTR, value=art.APP_ID)
    _call(store, 6, (POINTER(PROPERTYKEY), POINTER(PROPVARIANT)), byref(key), byref(value))
    _call(store, 7, ())
    _call(_query(link, IID_PERSIST_FILE), 6, (c_wchar_p, c_ulong), str(path), 1)


def desktop_folder() -> Path:
    # Not simply ~/Desktop: OneDrive moves it.
    buffer = create_unicode_buffer(260)
    windll.shell32.SHGetFolderPathW.restype = HRESULT
    windll.shell32.SHGetFolderPathW(None, 0x10, None, 0, buffer)
    return Path(buffer.value)


def main() -> int:
    root = application_root()
    pythonw = Path(sys.executable).with_name("pythonw.exe")
    for folder in (root, desktop_folder()):
        create_shortcut(folder / SHORTCUT_NAME, pythonw, root / "Portal 2 VR Enriched Launcher.pyw", art.ICON)
        print(folder / SHORTCUT_NAME)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
