@echo off
del bpill.exe /q
copy .\..\bpill.exe bpill.exe /y

REM for %%i in (*.dat) do bpill -nc -raw %%i %%i.tga -type 0 -width 32 -height 2500 -bytes 1
for %%i in (*.dat) do bpill -nc -raw %%i %%i.tga -type 0 -width 32 -height 2500 -offset 2400 -cmpr1 0 -cmpr2 255
pause
