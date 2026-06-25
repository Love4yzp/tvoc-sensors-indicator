@echo off
REM Single entry point for the SenseCAP Indicator firmware tooling.
REM
REM   dev build ^| flash ^| monitor          -> ESP32-S3 (ESP-IDF)
REM   dev rp2040 build ^| upload ^| monitor   -> RP2040 coprocessor (PlatformIO)
REM
REM ESP-IDF activation: only the ESP32-S3 commands need it. If idf.py is not
REM already on PATH, this activates ESP-IDF from %IDF_PATH% (set per machine /
REM CI; never committed). It does NOT hardcode an install path.

if /I "%~1"=="build"   goto needidf
if /I "%~1"=="flash"   goto needidf
if /I "%~1"=="monitor" goto needidf
goto run

:needidf
where idf.py >nul 2>nul && goto run
if not defined IDF_PATH (
  echo ESP-IDF not active. Set IDF_PATH ^(or use the ESP-IDF prompt^), then retry.>&2
  exit /b 1
)
call "%IDF_PATH%\export.bat" >nul

:run
python "%~dp0scripts\dev.py" %*
