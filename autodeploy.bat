@echo off

set log_file=deploy.log
set current_datetime=%date% %time%
(
    echo Log started at: %current_datetime%
) > "%log_file%" 2>&1

    echo Checking if Python is installed...
    python --version
    if %errorlevel% neq 0 (
        echo Python is not installed. https://www.python.org/downloads/ Please install Python and try again.
        echo Press Enter to exit.
        pause >nul
        exit 
    ) else (
        echo Python is already installed.
    )

    echo Checking if pip is installed...
    pip --version
    if %errorlevel% neq 0 (
        get-pip.py
    ) else (
        echo pip is already installed.
    )

set file_path="pip_show_platformio_info.txt"
:: Check if PlatformIO is installed
echo check Platform IO
pip show platformio > %file_path% 2>&1
findstr /C:"WARNING: Package(s) not found" %file_path% > nul
if %errorlevel% neq 0 (          
    echo PlatformIO is already installed.
) else (
    echo PlatformIO is not installed. Proceeding with installation...
    pip install platformio
        if %errorlevel% neq 0 (
            echo Error during PlatformIO installation.
            echo Press Enter to exit.
            pause >nul
            exit 
    ) else (
        echo PlatformIO has been successfully installed.
        pip show platformio > %file_path% 2>&1
        type %file_path%
    )
)>> "%log_file%" 2>&1

:: Path to the Arduino project
set project_path=.\

:: Arduino board type (e.g., uno, nano, etc.)
set board_type=espressif32

:: Port to which Arduino is connected
set port=arduino_port

echo Configuring Arduino project and uploading
(
:: Calling PlatformIO CLI to upload the project
python -m platformio run --target upload -d .\ -e denky32
)>> "%log_file%" 2>&1
:: Check if the operation was successful
if %errorlevel% neq 0 (
echo Error uploading the project.
echo Press Enter to exit.
pause >nul
exit
) else (
echo The project has been successfully uploaded to Arduino.
)

echo Press Enter to exit.
pause >nul