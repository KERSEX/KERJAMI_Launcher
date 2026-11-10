# Copy to build.local.psd1 and adjust the paths for your machine.
# build.local.psd1 is ignored by Git.
@{
    # Qt 6 installation prefix (the folder that contains bin, lib and plugins).
    QtDir        = 'D:\Qt\6.11.2-static'

    # Root of the llvm-mingw toolchain. Remove this line to use the compiler from PATH.
    ToolchainDir = 'D:\Tools\llvm-mingw'

    # Remove these lines if cmake and ninja are in PATH.
    CMakeExe     = 'D:\Tools\cmake\bin\cmake.exe'
    NinjaExe     = 'D:\Tools\ninja\ninja.exe'
}
