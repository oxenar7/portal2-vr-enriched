@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if not errorlevel 1 (
    py -3 -c "import sys, tkinter; sys.exit(0 if sys.version_info >= (3,10) else 1)" >nul 2>nul
    if errorlevel 1 goto missing_python
    where pyw >nul 2>nul
    if not errorlevel 1 (
        start "" pyw -3 "%~dp0Portal 2 VR Enriched Launcher.pyw"
    ) else (
        py -3 "%~dp0Portal 2 VR Enriched Launcher.pyw"
    )
    exit /b
)
where python >nul 2>nul
if not errorlevel 1 (
    python -c "import sys, tkinter; sys.exit(0 if sys.version_info >= (3,10) else 1)" >nul 2>nul
    if errorlevel 1 goto missing_python
    where pythonw >nul 2>nul
    if not errorlevel 1 (
        start "" pythonw "%~dp0Portal 2 VR Enriched Launcher.pyw"
    ) else (
        python "%~dp0Portal 2 VR Enriched Launcher.pyw"
    )
    exit /b
)
:missing_python
echo Python 3.10 or newer with Tkinter is required.
echo Install Python for Windows, including Tcl/Tk, then run this launcher again.
pause
exit /b 1
