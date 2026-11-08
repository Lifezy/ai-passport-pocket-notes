@echo off
setlocal
for /d %%D in ("%USERPROFILE%\.espressif\python_env\idf5.5_py*_env") do (
    if exist "%%D\Scripts\pythonw.exe" (
        start "" "%%D\Scripts\pythonw.exe" "%~dp0tools\wifi_setup.py"
        exit /b 0
    )
)
echo ESP-IDF 5.5 Python environment was not found. Please run project setup first.
pause
exit /b 1
