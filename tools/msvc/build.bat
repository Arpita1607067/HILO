@echo off
rem Builds bin\hilo_fast.exe, bin\hilo_any.exe and bin\hilo_prove.exe with Microsoft Visual C++,
rem for Windows machines without g++. The folder tools\msvc holds a stand-in for the GCC header <bits/stdc++.h>.
rem Usage (from any folder):  tools\msvc\build.bat
rem Then:                     NOBUILD=1 sh run_hilo.sh
rem If Visual Studio is installed elsewhere, set VCVARS to the full path of vcvars64.bat first.

if not defined VCVARS set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (echo vcvars64.bat not found; set VCVARS to its path & exit /b 1)
call "%VCVARS%" >nul 2>nul

cd /d "%~dp0..\.."
if not exist bin mkdir bin
set "FLAGS=/nologo /O2 /std:c++17 /EHsc /I tools\msvc /Fo:bin\"
cl %FLAGS% /Fe:bin\hilo_fast.exe src\hilo_fast.cpp || exit /b 1
cl %FLAGS% /Fe:bin\hilo_any.exe src\hilo_any.cpp || exit /b 1
cl %FLAGS% /Fe:bin\hilo_prove.exe src\hilo_prove.cpp || exit /b 1
del bin\*.obj 2>nul
echo built bin\hilo_fast.exe bin\hilo_any.exe bin\hilo_prove.exe
