@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /utf-8 /W4 main.cpp filesystem_repository.cpp /Fo:build\ /Fe:build\SafeCopy.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /utf-8 /W4 tests.cpp filesystem_repository.cpp /Fo:build\ /Fe:build\SafeCopyTests.exe
if errorlevel 1 exit /b 1
build\SafeCopyTests.exe %*
exit /b %errorlevel%
