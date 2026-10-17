@echo off
rem Registers LYNX ASIO so that other programs (FL Studio, Reaper, Ableton...) list it as an ASIO driver.
rem Needs administrator rights: Windows asks for them. LYNX itself does not need this step.
net session >nul 2>&1 || (powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs" & exit /b)
"%SystemRoot%\System32\regsvr32.exe" "%~dp0lynx-asio.dll"
