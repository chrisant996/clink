@echo off
if exist "%~dp0.build\vs2022\bin\debug\clink.bat" (call "%~dp0.build\vs2022\bin\debug\clink.bat"& goto :eof)
if exist "%~dp0.build\vs2019\bin\debug\clink.bat" (call "%~dp0.build\vs2019\bin\debug\clink.bat"& goto :eof)
