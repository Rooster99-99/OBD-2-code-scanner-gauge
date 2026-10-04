# PlatformIO post-build step: after every build, write one combined
# firmware file (bootloader + partition table + app) to release/.
# That single file flashes at address 0x0 with a web flasher or esptool,
# so no one needs PlatformIO to install it.

Import("env")  # noqa: F821  (provided by PlatformIO)
import os


def merge_bin(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    app_bin = os.path.join(build_dir, env.subst("${PROGNAME}.bin"))
    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "release")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, env.subst("obd-gauge-${PIOENV}-full.bin"))

    images = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        images += [env.subst(offset), '"%s"' % env.subst(path)]
    images += [env.subst("$ESP32_APP_OFFSET"), '"%s"' % app_bin]

    cmd = " ".join([
        '"$PYTHONEXE"', '"$OBJCOPY"', "--chip", "esp32", "merge_bin",
        "-o", '"%s"' % out,
        "--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "4MB",
    ] + images)
    env.Execute(cmd)
    print("Combined firmware: " + out + "  (flash at 0x0)")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)  # noqa: F821
