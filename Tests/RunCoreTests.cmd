@echo off
setlocal
set "TEST_VS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 2
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TEST_VS=%%i"
if not defined TEST_VS exit /b 2
set "TEST_ARCH=x64"
if /i "%~2"=="x86" set "TEST_ARCH=x86"
if not "%~2"=="" if /i not "%~2"=="x86" if /i not "%~2"=="x64" exit /b 2
if /i "%TEST_ARCH%"=="x86" (call "%TEST_VS%\VC\Auxiliary\Build\vcvars32.bat" >nul) else (call "%TEST_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul)
if errorlevel 1 exit /b 2
if not exist "%~dp0..\build\core-tests\%TEST_ARCH%" mkdir "%~dp0..\build\core-tests\%TEST_ARCH%"
pushd "%~dp0..\build\core-tests\%TEST_ARCH%"
if errorlevel 1 exit /b 2
set "TEST_SUITES=Dispatcher Elevator FleetRebalancer Simulation Concurrency Floor Statistics System Reliability"
if not "%~1"=="" if /i not "%~1"=="All" (
    set "TEST_SUITES="
    for %%s in (Dispatcher Elevator FleetRebalancer Simulation Concurrency Floor Statistics System Reliability) do if /i "%~1"=="%%s" set "TEST_SUITES=%%s"
)
if not defined TEST_SUITES (popd & exit /b 2)
for %%s in (%TEST_SUITES%) do (
    if not exist "%~dp0%%sTests.cpp" (popd & exit /b 2)
        cl /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /MDd /Zi /I"%~dp0..\ElevatorSimulation" "%~dp0%%sTests.cpp" "%~dp0..\ElevatorSimulation\Core\Passenger.cpp" "%~dp0..\ElevatorSimulation\Core\Floor.cpp" "%~dp0..\ElevatorSimulation\Core\Elevator.cpp" "%~dp0..\ElevatorSimulation\Core\EventScheduler.cpp" "%~dp0..\ElevatorSimulation\Core\Dispatcher.cpp" "%~dp0..\ElevatorSimulation\Core\FixedThreadPool.cpp" "%~dp0..\ElevatorSimulation\Core\FleetRebalancer.cpp" "%~dp0..\ElevatorSimulation\Core\Simulation.cpp" "%~dp0..\ElevatorSimulation\Core\SimulationWorker.cpp" "%~dp0..\ElevatorSimulation\Statistics\Statistics.cpp" /Fe:%%sTests.exe
        if errorlevel 1 goto test_failed
        %%sTests.exe
        if errorlevel 1 goto test_failed
)
popd
exit /b 0
:test_failed
popd
exit /b 1
