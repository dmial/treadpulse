# PlatformIO pre-script for env:sim: C++17 for C++ sources only. A -std=gnu++17
# in build_flags would also reach LVGL's C sources, which reject it.
Import("env")  # noqa: F821 (provided by PlatformIO)

env.Append(CXXFLAGS=["-std=gnu++17"])  # noqa: F821
