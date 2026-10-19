@echo off
rem Opens the BC2 VR developer launcher (Windows PowerShell 5.1 or pwsh via -STA).
powershell -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0DevLauncher.ps1" %*
