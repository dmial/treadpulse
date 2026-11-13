# PlatformIO pre-script for the board envs. With custom_sdkconfig, pioarduino
# 54.03.20 recompiles the Arduino core and copies the new libraries, memory.ld
# and sdkconfig.h into the framework package - but it misses two things:
# - sections.ld, the linker script that decides which functions are placed in
#   IRAM. Linking with the stock file quietly loses options such as
#   CONFIG_GDMA_CTRL_FUNC_IN_IRAM.
# - the libraries one folder deeper than the rest: mbedtls builds its crypto,
#   X.509 and TLS code as separate libraries under esp-idf/mbedtls/mbedtls/.
#   The package keeps the stock ones, so a CONFIG_MBEDTLS_* option shows up in
#   sdkconfig.h but never reaches the firmware (the TLS buffers stayed in
#   internal RAM although CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC was set).
# Both stay only in the build folder of the env that ran the recompile, so
# another env (freenove_s3_5in_demo), or the same env after a clean, would link
# with the stock files. This copies them next to the recompiled libraries,
# where the link finds them.
import filecmp
import shutil
from pathlib import Path

Import("env")  # noqa: F821 (provided by PlatformIO)

board = env.BoardConfig()  # noqa: F821
mcu = board.get("build.mcu", "esp32")
memory_type = board.get("build.arduino.memory_type",
                        board.get("build.flash_mode", "dio") + "_qspi")
package_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs")  # noqa: F821
build_dir = Path(env.subst("$BUILD_DIR"))  # noqa: F821

# Nested library in the build folder -> its name in the package. The stock core
# names mbedtls' own TLS library libmbedtls_2.a, because the mbedtls component's
# certificate bundle already takes libmbedtls.a.
NESTED_LIBS = {
    "esp-idf/mbedtls/mbedtls/library/libmbedcrypto.a": "libmbedcrypto.a",
    "esp-idf/mbedtls/mbedtls/library/libmbedx509.a": "libmbedx509.a",
    "esp-idf/mbedtls/mbedtls/library/libmbedtls.a": "libmbedtls_2.a",
    "esp-idf/mbedtls/mbedtls/3rdparty/everest/libeverest.a": "libeverest.a",
    "esp-idf/mbedtls/mbedtls/3rdparty/p256-m/libp256m.a": "libp256m.a",
}


def copy_if_changed(rebuilt: Path, installed: Path) -> None:
    if rebuilt.is_file() and not (installed.is_file()
                                  and filecmp.cmp(rebuilt, installed, shallow=False)):
        shutil.copyfile(rebuilt, installed)
        print(f"*** Copied the recompiled core's {rebuilt.name} to {installed} ***")


if package_dir:
    libs = Path(package_dir) / mcu
    # pioarduino keeps sdkconfig.orig once it has replaced the stock libraries
    if (libs / "sdkconfig.orig").is_file():
        copy_if_changed(build_dir / "sections.ld", libs / memory_type / "sections.ld")
        for nested, name in NESTED_LIBS.items():
            copy_if_changed(build_dir / nested, libs / "lib" / name)
