# PlatformIO pre-script for the board envs. With custom_sdkconfig, pioarduino
# 54.03.20 recompiles the Arduino core and copies the new libraries, memory.ld
# and sdkconfig.h into the framework package - but not sections.ld, the linker
# script that decides which functions are placed in IRAM. The new one stays only
# in the build folder of the env that ran the recompile, so another env
# (freenove_s3_5in_demo), or the same env after a clean, would link with the
# stock file and quietly lose options such as CONFIG_GDMA_CTRL_FUNC_IN_IRAM.
# This copies it next to the recompiled libraries, where the link finds it.
import filecmp
import shutil
from pathlib import Path

Import("env")  # noqa: F821 (provided by PlatformIO)

board = env.BoardConfig()  # noqa: F821
mcu = board.get("build.mcu", "esp32")
memory_type = board.get("build.arduino.memory_type",
                        board.get("build.flash_mode", "dio") + "_qspi")
package_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs")  # noqa: F821

if package_dir:
    libs = Path(package_dir) / mcu
    rebuilt = Path(env.subst("$BUILD_DIR")) / "sections.ld"  # noqa: F821
    installed = libs / memory_type / "sections.ld"
    # pioarduino keeps sdkconfig.orig once it has replaced the stock libraries
    if ((libs / "sdkconfig.orig").is_file() and rebuilt.is_file()
            and not (installed.is_file() and filecmp.cmp(rebuilt, installed, shallow=False))):
        shutil.copyfile(rebuilt, installed)
        print(f"*** Copied the recompiled core's sections.ld to {installed} ***")
