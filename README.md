# Video Inspector

Inspect videos with slow motion, panning, zooming, and looping.

## How To Build (Windows)
### Build Cinder
1. Download and install gitbash, Visual Studio C++, and CMake.  Make sure CMake is added to your path.
2. git clone git@github.com:cinder/Cinder.git cinder
3. cd cinder
4. mkdir build; cd build
5. cmake ..
6. Build Cinder for both Debug and Release modes by either opening in Visual Studio or running this command for both Debug and Release: cmake --build . --config Debug --parallel 8

### Build Video Inspector
1. Edit CMakeLists.txt line ~24 to set CINDER_PATH (the relative path to where Cinder is).
2. mkdir build; cd build
3. cmake ..
4. Open in Visual Studio.

