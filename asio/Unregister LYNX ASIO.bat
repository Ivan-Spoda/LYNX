@echo off
rem Removes LYNX ASIO from the ASIO driver list of other programs. Needs administrator rights.
net session >nul 2>&1 || (powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs" & exit /b)
"%SystemRoot%\System32\regsvr32.exe" /u "%~dp0lynx-asio.dll"
