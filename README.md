# gxr-key-remap

Small evdev/uinput daemons that turn a phone's hardware buttons into mouse
buttons on a Linux desktop session.

| Daemon           | Source device  | Output                        |
|------------------|----------------|-------------------------------|
| `keys-remap`     | `pmic_pwrkey`  | power button → left click     |
|                  | `pmic_resin`   | volume down → `F13`           |
|                  | `gpio_keys`    | volume up → right click       |
| `touchkey-remap` | `sec_touchkey` | touch held → left button held |

Each daemon grabs its source device(s) exclusively (`EVIOCGRAB`) and
re-emits events through a virtual uinput device (`remapped-buttons`,
`remapped-touchkey`). The virtual devices declare relative axes so
udev/libinput treat them as mice.

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

`publish.sh` builds one signed source package per Ubuntu series inside an
Ubuntu 26.04 Podman container and uploads them to `ppa:lightofmysoul/gxr`.
The host only needs Podman, Git and GnuPG with the signing key.

```sh
./publish.sh --no-upload    # build, sign and validate only
./publish.sh                # build, sign, validate and dput
./publish.sh --unsigned     # no key needed; sign later with debsign
```

For a new upload, add a `debian/changelog` entry (bump the upstream version
or the `+gxrN` component) and commit it; normal uploads refuse a dirty tree.
See `./publish.sh --help` and `debian/README.source`.

## License

MIT
