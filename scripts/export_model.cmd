@echo off
rem Re-runs the Blender exporter over the canonical .blend, writing .skeleton,
rem .mesh (plus textures) and the five aim-pose .animation files.
rem
rem Usage, from anywhere:
rem   .\scripts\export_model.cmd
rem   .\scripts\export_model.cmd resources\blender\some_other.blend
rem
rem Blender is NOT on PATH on this machine, hence the absolute path below.
rem The exporter refuses any version other than 5.1 -- it targets a verified API
rem surface, so a silent run against 4.x is exactly what that check prevents.
rem
rem It cd's to the repo root first: --out and --poses both default to paths
rem relative to it, so running from scripts\ would scatter output into scripts\.
rem
rem A .cmd (not .ps1) so PowerShell's execution policy doesn't block it, matching
rem run_client_cold.cmd.

setlocal

set "BLENDER=C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
set "BLEND=%~1"
if "%BLEND%"=="" set "BLEND=resources\blender\actual_with_poses.blend"

if not exist "%BLENDER%" (
  echo Blender 5.1 not found at "%BLENDER%".
  echo Edit this script if it lives elsewhere -- the exporter requires 5.1 exactly.
  exit /b 1
)

cd /d "%~dp0.."

rem Everything after the bare -- goes to the script, not to Blender.
"%BLENDER%" "%BLEND%" --background --python src\tools\blender_export.py -- --out resources\models
exit /b %ERRORLEVEL%
