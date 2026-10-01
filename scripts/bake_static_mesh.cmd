@echo off
rem Bakes a procedural Blender material (a node group such as Clay Doh) down to
rem base colour, normal and roughness, and writes the object as a static .glb.
rem The .blend is never saved.
rem
rem Usage, from anywhere:
rem   .\scripts\bake_static_mesh.cmd
rem   .\scripts\bake_static_mesh.cmd resources\blender\other.blend "Object Name" resources\glb\other.glb
rem
rem Anything after the third argument goes to the script: --size 4096,
rem --samples 64, --textures <directory outside resources>.
rem
rem Blender is NOT on PATH on this machine, hence the absolute path below.

setlocal

set "BLENDER=C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
set "BLEND=%~1"
set "OBJECT=%~2"
set "OUT=%~3"
if "%BLEND%"=="" set "BLEND=resources\blender\mvmt.blend"
if "%OBJECT%"=="" set "OBJECT=Clay Text Object"
if "%OUT%"=="" set "OUT=resources\glb\mvmt.glb"

if not exist "%BLENDER%" (
  echo Blender 5.1 not found at "%BLENDER%".
  echo Edit this script if it lives elsewhere -- the bake script requires 5.1 exactly.
  exit /b 1
)

cd /d "%~dp0.."

rem Everything after the bare -- goes to the script, not to Blender.
"%BLENDER%" "%BLEND%" --background --python src\tools\blender_bake.py -- --object "%OBJECT%" --out "%OUT%" %4 %5 %6 %7 %8 %9 2>nul
exit /b %ERRORLEVEL%
