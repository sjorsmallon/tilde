@echo off
rem Runs src\tools\orm_pack.py over every material folder under
rem resources\textures: ao.png + roughness.png + metallic.png -> orm.png, the
rem three sources deleted. A folder that already has an orm.png is skipped.
rem
rem Usage, from anywhere:
rem   .\scripts\pack_orm.cmd                              every folder
rem   .\scripts\pack_orm.cmd resources\textures\harsh_bricks [...]   just these
rem
rem Needs Pillow: pip install Pillow
rem
rem A .cmd (not .ps1) so PowerShell's execution policy doesn't block it, matching
rem run_client_cold.cmd.

setlocal enabledelayedexpansion
cd /d "%~dp0.."

if not "%~1"=="" (
  python src\tools\orm_pack.py %*
  exit /b !errorlevel!
)

set FOLDERS=
for /d %%D in (resources\textures\*) do set FOLDERS=!FOLDERS! "%%D"

if "!FOLDERS!"=="" (
  echo no material folders under resources\textures
  exit /b 1
)

python src\tools\orm_pack.py !FOLDERS!
exit /b !errorlevel!
