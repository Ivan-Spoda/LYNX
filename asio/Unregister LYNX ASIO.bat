@echo off
rem Removes LYNX ASIO from the ASIO driver list of other programs (64-bit and 32-bit). Needs administrator rights.
net session >nul 2>&1 || (powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs" & exit /b)
"%SystemRoot%\System32\regsvr32.exe" /u "%~dp0lynx-asio.dll"
if exist "%~dp0lynx-asio32.dll" if exist "%SystemRoot%\SysWOW64\regsvr32.exe" "%SystemRoot%\SysWOW64\regsvr32.exe" /u "%~dp0lynx-asio32.dll"
