AmazonTemplate3 – Build instructions with Gravity IDE on Linux

Prerequisites.

Step 1. Install Vulkan headers required by Qt.
Command to run in a terminal:
sudo apt update
sudo apt install libvulkan-dev

Gravity IDE setup.

Step 2. Install the CMake Tools extension.
Open Gravity IDE.
Open the Extensions panel.
Install the extension named “CMake Tools”.

Step 3. Open the project.
Use File → Open Folder.
Select the project root directory that contains the top-level CMakeLists.txt file.

Step 4. Select a compiler kit.
Open the command palette with Ctrl+Shift+P.
Run the command “CMake: Select a Kit”.
Choose “GCC x86_64-linux-gnu”.

Step 5. Configure the Qt path.
Qt is installed via the Qt installer and must be provided explicitly to CMake.

Open the command palette with Ctrl+Shift+P.
Run “Preferences: Open Workspace Settings (JSON)”.
Add the following configuration, adapting the path if needed:

{
"cmake.configureSettings": {
"CMAKE_PREFIX_PATH": "/home/cedric/Qt/6.7.3/gcc_64"
}
}

Step 6. Configure the project.
Open the command palette.
Run “CMake: Configure”.
Configuration is successful when “Configuring done” and “Generating done” appear.

Step 7. Build the project.
Open the command palette.
Run “CMake: Build”.

Notes.
CMake presets are not used.
All machine-specific paths are stored in workspace settings only.
The build directory is generated automatically by CMake Tools.
