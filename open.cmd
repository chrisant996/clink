@echo off
start "" "%~dp0.build\vs2019\clink.sln"
sleep 2
call code "%~dp0"
cd /d "%~dp0"
