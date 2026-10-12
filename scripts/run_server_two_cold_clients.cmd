@echo off
rem Starts a dedicated MyGame_Server, then two "cold" MyGame_Client instances
rem (see run_client_cold.cmd): each client gets its own empty MAPS_DIR so both
rem must stream the compiled package from the server. Three windows open; close
rem them to stop.
rem
rem Usage:
rem   .\scripts\run_server_two_cold_clients.cmd

set BIN=%~dp0..\cmake_build\bin

start "MyGame_Server" "%BIN%\MyGame_Server.exe"
timeout /t 2 /nobreak >nul

set MAPS_DIR=cold_maps_1
start "MyGame_Client 1" "%BIN%\MyGame_Client.exe"
timeout /t 1 /nobreak >nul

set MAPS_DIR=cold_maps_2
start "MyGame_Client 2" "%BIN%\MyGame_Client.exe"
