@echo off
rem Runs MyGame_Client as a "cold" client: MAPS_DIR points at an empty folder, so
rem the client has no local copy of any map and must stream the compiled package
rem from the server. This is the streaming smoke test.
rem
rem Usage (with a MyGame_Server already running):
rem   .\scripts\run_client_cold.cmd
rem
rem Normal play (client uses its real "maps" folder) is just:
rem   .\cmake_build\bin\MyGame_Client.exe
rem
rem A .cmd (not .ps1) so PowerShell's execution policy doesn't block it. The
rem folder need not exist; any path lacking the server's map forces a cache miss.
rem It doubles as the on-disk cache location once step 7 (package caching) lands.

set MAPS_DIR=cold_maps
"%~dp0..\cmake_build\bin\MyGame_Client.exe"
