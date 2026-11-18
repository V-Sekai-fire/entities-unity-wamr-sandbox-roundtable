# Cross-compile ub_native.dll for Windows x86_64 from macOS or Linux, with zig's clang (mingw-w64 ABI).
# clang, not gcc: WAMR's hardware bounds checks use SEH __try blocks that gcc does not accept.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
get_filename_component(_here "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
set(CMAKE_C_COMPILER   ${_here}/zig-cc)
set(CMAKE_CXX_COMPILER ${_here}/zig-c++)
set(CMAKE_AR           ${_here}/zig-ar)
set(CMAKE_RANLIB       ${_here}/zig-ranlib)
set(CMAKE_ASM_COMPILER ${_here}/zig-cc)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_CXX_FLAGS_INIT "-msse4.2")
