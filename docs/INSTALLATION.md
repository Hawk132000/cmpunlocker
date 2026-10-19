# Installation

Here are the steps to install cmpunlocker on your system.

---

## Requirements

- Linux (x86-64)
- Root access
- NVIDIA CMP 170HX
- **nvidia-open 610.xx.xx+ already installed** (libs + firmware)
- Kernel headers matching the running kernel (`linux-headers-$(uname -r)` / `kernel-devel`)
- Secure Boot disabled (patched modules are unsigned)
- Network access on first install (downloads matching stock `open-gpu-kernel-modules` sources)
- Python 3 (used at build time to select 8GB/10GB geometry)

## Install

To install cmpunlocker, run the following command:

```bash
sudo ./install.sh
```

To force a certain memory profile, use the `--profile` option:

```bash
sudo ./install.sh --profile=8gb    # 8GB card → 64GB unlock
sudo ./install.sh --profile=10gb   # 10GB card → 40GB unlock
```

Then perform a reboot.

## Full-size BAR1 (opt-in, two methods)

The driver-side REBAR unlock (`bar1-resize-unlock.patch`) is always applied; on
kernel v7.0 the *placement* of the resulting 64 GB window still fails without one
of the two optional methods below. Default is off — a plain install matches
upstream behavior (BAR1 keeps the stock 32 GB fallback).

**Method 1 — cmpalign module:** `sudo ./install.sh --with-bar1-64g`

Builds and installs **cmpalign**, a small out-of-tree kernel module that backports
the v7.1 PCI resource-placement fix so both cards keep the full 64 GB BAR1 window
instead of falling back to 32 GB. It is armed before `nvidia` on every boot via
`/etc/modprobe.d/cmpalign.conf` (`softdep nvidia pre: cmpalign`). No BIOS or
command-line changes.

**Method 2 — kernel command line:** `sudo ./install.sh --with-bar1-cmdline`

Adds `pci=realloc pci=hpmmioprefsize=2T` to the kernel command line (GRUB or
systemd-boot) so the v7.0 allocator has room to place the 64 GB window — the
approach from the asm64-hooligan fork. Requires **Above 4G Decoding** enabled in
BIOS/UEFI. Takes effect after reboot; `remove.sh` restores the pre-install command
line from its backup.

Either method can be verified after reboot with `sudo ./verify.sh` (Step 4) or
`sudo dmesg | grep 'final size'` — expect `65536 MB` twice. The two methods are
independent and may be combined. **Measured: method 2 alone was insufficient on a
dual-CMP 170HX board with firmware-fixed BAR windows** (still `32768 MB` with the
params active) — prefer method 1 there; see [BAR1-64GB.md](BAR1-64GB.md).
`--with-bar1-cmdline --with-p2p` remains a valid pairing on hosts where the
cmdline method does produce the full-size BAR1 (reboot once before testing P2P).

- Roll back method 1 at any time: `sudo rmmod cmpalign`, then reload the NVIDIA
  modules (or simply run `sudo ./remove.sh --yes`)
- Like the patched NVIDIA modules, `cmpalign.ko` is built for the running kernel —
  re-run `sudo ./install.sh --with-bar1-64g` after each kernel upgrade

Technical details: [BAR1-64GB.md](BAR1-64GB.md).

## Uninstall

To uninstall cmpunlocker, run the following command:

```bash
sudo ./remove.sh --yes
```

Then perform a reboot.
