# gxr-key-remap

Small evdev/uinput daemons that turn a Galaxy XR headset's hardware buttons
into mouse buttons on a Linux desktop session.

| Daemon          | Source device                 | Output                        |
|-----------------|-------------------------------|-------------------------------|
| `keys-remap`     | `pmic_pwrkey`                  | power button → left click     |
|                 | `pmic_resin`                   | volume down → `F13`           |
|                 | `gpio_keys` / `gpio-keys`      | volume up → right click       |
| `touchkey-remap` | `sec_touchkey`                | touch held → left button held |
|                 | `ABOV A96T396 Touch Slider`    | touch held → left button held |

Each daemon grabs its source device(s) exclusively (`EVIOCGRAB`) and
re-emits events through a virtual uinput device (`remapped-buttons`,
`remapped-touchkey`). The virtual devices declare relative axes so
udev/libinput treat them as mice.

Devices are discovered by their evdev names under `/dev/input`, independently
of event numbers or sysfs paths. Both the vendor 5.10 name `gpio_keys` and the
7.4 name `gpio-keys` are supported. The touch remapper accepts the vendor
`sec_touchkey` device and the mainline `ABOV A96T396 Touch Slider`. Both use
`BTN_TOUCH` for press/release; slider position events are ignored, so sliding
keeps the left button held. Both daemons require `/dev/uinput`
(`CONFIG_INPUT_UINPUT=y`, or `=m` with the module loaded).

## Build

Requires CMake ≥ 3.16, a C11 compiler and Linux kernel headers.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build      # runs touchkey-remap --self-test
```

## Install

```sh
sudo cmake --install build
sudo systemctl daemon-reload
sudo systemctl enable --now keys-remap.service touchkey-remap.service
```

By default this installs to `/usr/local`:

- `/usr/local/bin/keys-remap`, `/usr/local/bin/touchkey-remap`
- `/usr/local/lib/systemd/system/{keys-remap,touchkey-remap}.service`

The units' `ExecStart=` follows the configured prefix. Options:

| CMake option                | Default                              |
|-----------------------------|--------------------------------------|
| `CMAKE_INSTALL_PREFIX`      | `/usr/local`                         |
| `SYSTEMD_UNITDIR`           | `${CMAKE_INSTALL_PREFIX}/lib/systemd/system` |
| `GXR_INSTALL_SYSTEMD_UNITS` | `ON`                                 |
| `GXR_BUILD_TOOLS`           | `ON` (builds `keys-remap-verify`, not installed) |

## Verifying `keys-remap`

`keys-remap-verify <source_name>` checks that the daemon holds the grab on a
source device, injects a key press/release into it, and reports what the
`remapped-buttons` device emits:

```sh
sudo ./build/keys-remap-verify pmic_resin
```

## Publishing to the PPA

`packaging/publish.sh` builds a signed source package inside a Podman
container for the selected Ubuntu series and uploads it to
`ppa:lightofmysoul/gxr`. It defaults to Stonking; use `--series resolute`
or repeat `--series` to select multiple series. The host needs Podman, Git
and GnuPG with the signing key (unless using `--unsigned`).

```sh
./packaging/publish.sh --no-upload               # build, sign and check
./packaging/publish.sh                           # build, sign, check and upload
./packaging/publish.sh --test-build              # build arm64 packages before upload
./packaging/publish.sh --unsigned --test-build --allow-dirty  # check local changes
```

`--test-build` builds arm64 binaries from the extracted source package,
cross-compiling on an amd64 host or building natively on arm64. Runtime tests
run on native builds and are skipped when cross-compiling. Each series has
its own builder image, rebuilt when `packaging/Containerfile` or
`debian/control` changes. Artifacts are retained in `build/publish/`.

For a new upload, add a `debian/changelog` entry (bump the upstream version
or the `+gxrN` component) and commit it; normal uploads refuse a dirty tree.
Clean builds use the committed tree; `--allow-dirty` uses local changes and
requires `--no-upload` or `--unsigned`. The `packaging/` directory is excluded
from source packages. See `./packaging/publish.sh --help` and
`debian/README.source`.

## License

MIT
