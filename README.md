# cmpunlocker — NVIDIA CMP 170HX

A Linux fork of [cmpunlocker](https://github.com/amoghmunikote/cmpunlocker) for the
NVIDIA CMP 170HX (GA100). It restores firmware/OTP-locked compute and memory
features, adds a full-size BAR1 path and optional BAR1 P2P, and exposes the FBPA
PLL/MEM windows used by [170tune](https://github.com/vektorprime/170tune) for live
memory tuning.

This fork keeps the card **stock at boot**. It does not bake an HBM clock or
overclock into the driver; live tuning is done from userspace after the driver is
loaded.

> **Status:** community research project. The unlock and P2P paths are
> experimental. This project is not affiliated with or endorsed by NVIDIA.
> Modifying GPU firmware/OTP behavior can void warranties, corrupt data, hang the
> host, or damage hardware. Use at your own risk. No warranty is provided.

## Contents

- [What this fork adds](#what-this-fork-adds)
- [Supported hardware](#supported-hardware)
- [Working status](#working-status)
- [Requirements](#requirements)
- [Quick start](#quick-start)
- [Choose your install path](#choose-your-install-path)
- [Install options](#install-options)
- [Verify](#verify)
- [Full-size BAR1](#full-size-bar1)
- [BAR1 P2P](#bar1-p2p)
- [170tune compatibility](#170tune-compatibility)
- [Benchmark](#benchmark)
- [Safety and limitations](#safety-and-limitations)
- [Fork lineage](#fork-lineage)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [Credits](#credits)
- [License](#license)

## What this fork adds

- **FBPA PLL/MEM privilege windows** for live HBM clock, timing, and refresh
  control through 170tune. The windows are opened in the post-BooterLoad phase;
  no clock is compiled into the driver.
- **Full-size BAR1 helper (`cmpalign`)** for kernel v7.0, which otherwise falls
  back to a 32 GB BAR1 window even when resizable BAR is unlocked.
- **Experimental BAR1 P2P** patches and persistent registry keys, including a
  real-data verification path.
- **`verify.sh` P2P checks** for the installed module, active registry keys,
  full-size BAR1, and the `nvidia-smi topo -p2p` capability matrix.
- **10 GB SKU handling** that intentionally skips the orphaned unicast FBPA row
  while keeping the broadcast memory/PLL windows.

Everything else is inherited from upstream cmpunlocker and its contributors. See
[Credits](#credits) and [CREDITS.md](CREDITS.md).

## Supported hardware

| SKU | PCI device ID | Stock memory | Unlocked memory |
|---|---|---:|---:|
| CMP 170HX 8 GB | `10de:20c2` | 8 GB | 64 GB |
| CMP 170HX 10 GB | `10de:2082` | 10 GB | 40 GB |

Supported systems are Linux x86-64 hosts with the CMP 170HX installed. The card
has no usable display output. Multi-GPU systems are supported by the install code,
including mixed 8 GB and 10 GB cards. The 8 GB path is the one validated in this
fork; 10 GB and mixed inventories still need hardware validation.

## Working status

"Working" means exercised on physical CMP 170HX hardware, not merely implemented.
"Implemented" means the path exists but was not validated on that SKU or topology
in this fork.

| Capability | Status | Verified / notes |
|---|---|---|
| SM floorsweep / compute recovery (SS0/SS1) | Working | Inherited from upstream; used on the reference 8 GB cards. |
| Memory unlock, 8 GB -> 64 GB | Working | `verify.sh` reports 65536 MiB on both reference cards. |
| Memory unlock, 10 GB -> 40 GB | Implemented, unverified | Selected automatically by PCI ID; no 10 GB card was available for validation. |
| PCIe Gen2 x4 retrain | Working | Negotiated Gen2 x4 on the reference host; Gen3 is not available. |
| DRAM and SRAM ECC | Working | Enabled and healthy on the reference host. |
| Full-size BAR1 via `cmpalign` (`--with-bar1-64g`) | Working | 65536 MiB on kernel `7.0.0-38-generic`; 40 GB is expected on the 10 GB SKU but is unverified. |
| Full-size BAR1 via cmdline (`--with-bar1-cmdline`) | Implemented, host-dependent | Needs Above 4G Decoding; did not produce 64 GB alone on the reference board. |
| BAR1 P2P, dual 8 GB, B360M/PCH | Experimental, working | 256 MiB peer copy: 1.396 GB/s with exact checksum; `topo -p2p` OK both ways. |
| BAR1 P2P, 10 GB / mixed / other topologies | Untested | Requires full-size BAR1 plus a real copy or NCCL validation. |
| FBPA PLL/MEM windows | Working | Used by 170tune on the 8 GB reference card. |
| 170tune live SM/HBM tuning | Working | SM/HBM levers and persistence verified with 170tune's gates. |
| JTAG / Host2Jtag register access | Working | Inherited from upstream. |
| VFIO passthrough | Working | Inherited from upstream. |
| GPU profiling | Working | Inherited from upstream. |
| Persistence across reboot | Working | Patched modules, Gen2 service, P2P registry keys, and initramfs survive reboot. |
| Tensor parallel (TP2) with P2P | Not yet tested | The workload where P2P is expected to matter most. |
| Pipeline parallel (PP2) with P2P | Not yet tested | Low P2P traffic; needs separate workload/Xid validation. |

There is no Gen3 unlock for the CMP 170HX. The card's native electrical link is
Gen1 x4; this project retrains it to Gen2 x4. The missing x16 lanes are a board
hardware limitation, not a software setting. P2P and BAR1 do not change the link
width.

## Requirements

- Linux x86-64, root access.
- NVIDIA CMP 170HX (`10de:20c2` or `10de:2082`).
- NVIDIA open kernel modules already installed. Supported versions:
  `615.71.09`, `610.57.04`, `610.43.03`, `610.43.02`.
- Matching kernel headers: `linux-headers-$(uname -r)` or `kernel-devel`.
- `python3` with PyYAML, plus `make`, `gcc`, `patch`, `curl`, and `sha256sum`.
- Secure Boot disabled; the patched modules are unsigned.
- Network access on first install to download the matching NVIDIA
  `open-gpu-kernel-modules` source tree.
- Optional: CUDA toolkit and `nvcc` for the benchmark and 170tune integrity
  checks.
- Optional for 170tune: `iomem=relaxed` on the kernel command line.

## Quick start

```bash
git clone https://github.com/vektorprime/cmpunlocker.git
cd cmpunlocker

# Basic unlock, automatic 8 GB / 10 GB profile detection
sudo ./install.sh

# Recommended for BAR1 + P2P
sudo ./install.sh --with-bar1-64g --with-p2p

# Power off completely, then power on
sudo shutdown -h now
```

After the cold boot:

```bash
sudo ./verify.sh
```

For a 10 GB card, profile detection is automatic. To override it:

```bash
sudo ./install.sh --profile=8gb    # 8 GB SKU -> 64 GB
sudo ./install.sh --profile=10gb   # 10 GB SKU -> 40 GB
```

Uninstall:

```bash
sudo ./remove.sh --yes
```

Then power off and boot again.

## Choose your install path

Use the smallest combination that gives you what you need. Optional flags are
additive; a plain install is the upstream-equivalent path.

| Goal | Command | Why this combination | Main tradeoff |
|---|---|---|---|
| Basic unlock, single GPU, 170tune | `sudo ./install.sh` | Simplest path. BAR1 stays at the 32 GB fallback, which is enough for single-GPU compute and 170tune (BAR0). | No full-VRAM BAR1; no P2P. |
| Unlock + full 64 GB BAR1 | `sudo ./install.sh --with-bar1-64g` | Preferred on kernel v7.0: `cmpalign` backports the v7.1 allocator fix. Verified to produce 65536 MiB. | Out-of-tree module; rebuild after kernel changes. |
| Full BAR1 where firmware leaves MMIO placement to the kernel | `sudo ./install.sh --with-bar1-cmdline` | Adds `pci=realloc pci=hpmmioprefsize=2T` and uses the asm64-hooligan approach; no extra module. | Needs Above 4G Decoding; host-dependent. Not sufficient alone on the reference board. |
| Full BAR1 when one method is not enough | `sudo ./install.sh --with-bar1-64g --with-bar1-cmdline` | Combines the allocator fix with a larger/reallocated MMIO window. | More moving parts; still depends on firmware. |
| Experimental P2P with full BAR1 | `sudo ./install.sh --with-bar1-64g --with-p2p` | P2P needs a full-size peer aperture. `cmpalign` is the verified BAR1 path. | Experimental registry keys; `ForceP2P=0x11` can hang some hosts. |
| Experimental P2P via cmdline BAR1 | `sudo ./install.sh --with-bar1-cmdline --with-p2p` | Use only if cmdline BAR1 is the method that works on your host. | Same P2P risk; BAR1 method remains host-dependent. |
| Host-only, no VM passthrough | `sudo ./install.sh --no-passthrough` | Skips VFIO preparation. Add any BAR1/P2P flags you need. | Card is not pre-arranged for passthrough. |
| IOMMU/Gen2 already managed elsewhere | `sudo ./install.sh --no-iommu --no-gen2-service` | Leaves bootloader and early-boot retraining under your own tooling. | Link may stay at Gen1 if your tooling does not retrain it. |

Default behavior: IOMMU passthrough, the early-boot Gen2 service, and VFIO
preparation are enabled. Use the corresponding `--no-*` flag to opt out.

## Install options

| Option | What it changes | Choose when / tradeoff |
|---|---|---|
| `--profile=8gb` / `--profile=10gb` | Changes the metadata label only; unlock geometry still follows the PCI device ID. Ignored for mixed inventories. | Use only if the detected label is wrong. It cannot turn an 8 GB card into a 40 GB card. |
| `--with-bar1-64g` | Builds and installs `cmpalign.ko`, armed before `nvidia`, to backport the v7.1 resource-placement fix. | Preferred on kernel v7.0 and firmware that pre-assigns windows. Requires kernel headers and a rebuild after kernel upgrades. |
| `--with-bar1-cmdline` | Adds `pci=realloc pci=hpmmioprefsize=2T` to the kernel command line. | Choose if you do not want an extra module, already use the asm64-hooligan setup, or need the allocator to re-place windows. Needs Above 4G Decoding; firmware-dependent. Can be combined with `--with-bar1-64g`. |
| `--with-p2p` | Builds the three BAR1 P2P patches, writes the persistent registry keys, and rebuilds the initramfs. Requires a full-size BAR1 method. | Multi-GPU peer access/NCCL only. Experimental; not needed for single-GPU or normal CUDA. |
| `--no-iommu` | Leaves the kernel command line untouched instead of adding `intel_iommu=on`/`amd_iommu=on` and `iommu=pt`. | Choose if IOMMU/VFIO is managed by your bootloader or another tool. |
| `--no-gen2-service` | Skips the early-boot PCIe Gen2 retrain service. | Choose if you retrain the link yourself. Otherwise the card may remain at Gen1. |
| `--no-passthrough` | Skips VFIO preparation. | Choose for a host-only install with no VM passthrough. |

`--with-p2p` requires either `--with-bar1-64g` or `--with-bar1-cmdline`; the
installer rejects it otherwise. P2P patches are independent of the BAR1 method,
but they need the resulting full-size aperture.

## Verify

```bash
sudo ./verify.sh

# Memory and link state
nvidia-smi --query-gpu=index,memory.total,pci.bus_id,pcie.link.gen.current,pcie.link.width.current --format=csv

# Unlock, BAR1, and P2P logs
sudo dmesg | grep -E 'CMPUNLOCK|SEC2_DEBUG|BAR1'

# Gen2 retrain status
sudo ./tools/service.sh verify

# Topology and P2P capability
nvidia-smi topo -m
nvidia-smi topo -p2p r
nvidia-smi topo -p2p w
```

`verify.sh` checks the memory unlock, BAR1 sizing, and, when P2P is installed,
the P2P patch/registry/BAR1/capability state. A P2P capability result of `OK` is
not proof that bytes move; see [BAR1 P2P](#bar1-p2p).

## Full-size BAR1

With resizable BAR enabled, NVRM tries to grow BAR1 to the full memory size. On
kernel v7.0 the PCI resource allocator can fail to place the window and silently
fall back to 32 GB. That limits peer/GPUDirect access to the lower half of VRAM.
Full BAR1 is not required for single-GPU compute or 170tune; it is required for
P2P and other full-VRAM peer access.

| Method | What it does | Choose it when | Tradeoff |
|---|---|---|---|
| `--with-bar1-64g` (`cmpalign`) | Backports the v7.1 allocator alignment fix with a kprobe. `cmpalign.ko` is armed before `nvidia` via a modprobe soft dependency. | You are on kernel v7.0, firmware pre-assigns the windows, or you want the path verified to produce 65536 MiB here. | Out-of-tree module; rebuild it after a kernel upgrade. |
| `--with-bar1-cmdline` | Adds `pci=realloc pci=hpmmioprefsize=2T` so the allocator can re-place BARs and has a larger high-MMIO window. | You prefer no extra module, already use the asm64-hooligan setup, or your firmware leaves placement to the kernel. | Touches the boot command line; needs Above 4G Decoding; firmware-dependent. Alone, it did not produce 64 GB on the reference board. |
| Both flags | Uses both mechanisms together. | One method alone is insufficient on your board, or you are testing a new firmware/kernel combination. | More moving parts; still not guaranteed. |
| Neither flag | Keeps the stock 32 GB BAR1 fallback. | Single-GPU compute, 170tune, or you do not need peer access to all VRAM. | P2P and full-VRAM peer apertures are unavailable. |

Full BAR1 is opt-in because it changes the PCI resource layout and can fail to
place on some firmware/kernel combinations; a plain install stays closest to
upstream. Both BAR1 methods may be combined. `--with-p2p` does not choose a BAR1
method; it only adds the P2P patches and registry keys, so pick the BAR1 method
that works on your host. See [docs/BAR1-64GB.md](docs/BAR1-64GB.md) for the
mechanism, measurements, and rollback details.

## BAR1 P2P

P2P is disabled by default. Enable it with a full-size BAR1:

```bash
sudo ./install.sh --with-bar1-64g --with-p2p
sudo shutdown -h now
```

The installer builds three BAR1 P2P patches, writes the persistent registry keys
to `/etc/modprobe.d/cmp-pcie-gen2.conf`, and rebuilds the initramfs. After boot:

```bash
sudo ./verify.sh
nvidia-smi topo -p2p r
nvidia-smi topo -p2p w
```

A capability matrix can be forced to report `OK` without working peer transfers.
Verify with a real copy and compare contents, or use NCCL with
`NCCL_DEBUG=INFO` and look for `P2P`/`CUMEM` rather than host `SHM`.

Reference validation: two 8 GB CMP 170HX cards on an i5-9500 / B360M host, 64 GB
BAR1, Gen2 x4. A 256 MiB GPU0-to-GPU1 copy measured **1.396 GB/s** with an exact
checksum match. These results do not extend to 10 GB, mixed-SKU, or other
root-complex configurations without testing.

P2P is topology-dependent. It is most useful for tensor-parallel collectives and
prefill; pipeline-parallel traffic usually gains much less. A common PCIe switch
or two CPU-attached root ports is the reliable topology. PCH/root-complex
topologies may work, but must be tested. At Gen2 x4, expect roughly 1.4–1.7 GB/s
per direction, not full x16 bandwidth.

Do not unload and reload the NVIDIA module on a live system to apply P2P
parameters, and be aware that `ForceP2P=0x11` has been reported to hang some
machines. Apply the registry keys, rebuild the initramfs, and reboot.

## 170tune compatibility

The default patch set opens the FBPA PLL/MEM privilege windows required by
[170tune](https://github.com/vektorprime/170tune). This fork deliberately does not
bake an HBM clock into the driver, so the card boots at stock and a bad tuning
value is a reboot away from being gone.

- **8 GB (`10de:20c2`)**: FBPA_MEM, broadcast FBPA_PLL0/1/2, and unicast FBPA0
  PLL. Live NDIV, timings, and refresh levers are verified.
- **10 GB (`10de:2082`)**: FBPA_MEM and broadcast FBPA_PLL0/1/2. The unicast FBPA0
  row is intentionally skipped because its window is orphaned on this SKU; the
  170tune fork drives the memory lever through the first live FBPA. Implemented,
  but not yet validated on a physical 10 GB card in this fork.

170tune also requires `iomem=relaxed` on the kernel command line and root access.
The installer does not add `iomem=relaxed`; add it through your bootloader and
verify with `cat /proc/cmdline`. Then run:

```bash
sudo 170tune preflight
```

See the 170tune documentation for its qualification and safety gates. “It did
not crash” is not a stability result for this card.

## Benchmark

A prebuilt x86-64 benchmark is included:

```bash
./benchmark/nvidia_bench
./benchmark/nvidia_bench 1
./benchmark/nvidia_bench 0 50
./benchmark/nvidia_bench --csv
```

To rebuild it with the CUDA toolkit:

```bash
cd benchmark
nvcc -O3 -o nvidia_bench nvidia_bench.cu -lnvidia-ml -ldl \
  -gencode arch=compute_80,code=sm_80
strip nvidia_bench
```

The benchmark reports SM count, memory size and bandwidth, PCIe link speed,
tensor-core throughput, clocks, and hardware features. The original performance
screenshots in upstream cmpunlocker use
[OpenCL-Benchmark](https://github.com/ProjectPhysX/OpenCL-Benchmark).

## Safety and limitations

- This is not a supported NVIDIA configuration. Firmware/OTP modifications can
  void warranties and are not endorsed by NVIDIA.
- Back up data and keep a rescue boot option. Secure Boot must be disabled for the
  unsigned patched modules.
- HBM overclocking can produce silent data corruption. Use 170tune's integrity
  gates and qualify on your own card; a passing benchmark alone is not enough.
- `ForceP2P=0x11` can hang the machine. Have a hard power cycle available and do
  not apply it by reloading `nvidia` on a live system.
- BAR1 placement depends on firmware, kernel version, and MMIO layout. The
  `cmpalign` path is the only one verified to produce 64 GB on the reference
  board.
- The CMP 170HX has no Gen3 path and only x4 electrical lanes. P2P and BAR1 do not
  change that.
- Driver upgrades can leave the patched modules unbuilt. Re-run `install.sh` after
  a kernel or driver change. The installer removes conflicting DKMS modules.
- P2P and BAR1 do not fix Xid or illegal-memory-access faults in a workload.
  Validate the driver, model, and workload independently before attributing those
  errors to the link configuration.

## Fork lineage

This repository is a focused fork, not a replacement for upstream:

- [amoghmunikote/cmpunlocker](https://github.com/amoghmunikote/cmpunlocker) —
  upstream unlock, patched open kernel modules, memory geometry, ECC, build and
  verification framework, and the experimental P2P branch.
- [asm64-hooligan/cmpunlocker](https://github.com/asm64-hooligan/cmpunlocker) —
  FBPA PLL/MEM window map, memory-overclock lineage, and the command-line BAR1
  approach.
- [bayley/cmpunlocker](https://github.com/bayley/cmpunlocker) — BAR1 P2P research
  and real-transfer validation.
- [vektorprime/170tune](https://github.com/vektorprime/170tune) — live SM/HBM
  tuning, integrity gates, and persistent profiles.

The local P2P patch stack is experimental and is not upstreamed. Keep it on a
separate branch when maintaining a fork.

## Documentation

- [Installation](docs/INSTALLATION.md)
- [Full-size BAR1](docs/BAR1-64GB.md)
- [FBPA PLL and 170tune plan](docs/FBPA-PLL-170TUNE-PLAN.md)
- [Contributing](docs/CONTRIBUTING.md)
- [Credits](CREDITS.md)
- [License](LICENSE)

## Contributing

Keep changes small and tested on physical hardware. Include the exact card SKU,
host/PCIe topology, kernel, driver version, and whether BAR1 or P2P was enabled.
See [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md). Keep fork-specific P2P work on a
separate branch from unrelated hardware experiments.

## Credits

See [CREDITS.md](CREDITS.md) for the full list. In particular:

- **Amogh Munikote** and the cmpunlocker contributors for the upstream project,
  the unlock patch set, and the P2P research branch.
- **asm64-hooligan** for the FBPA PLL/MEM windows and the BAR1 command-line
  approach.
- **bayley** for BAR1 P2P research and real-transfer testing.
- **vektorprime** for this fork, the FBPA/170tune integration, the `cmpalign`
  helper, the P2P patch stack, and the verify/persistence work.
- **cachenetics/170tune** for the original tuning/qualification harness and its
  receipt-based persistence model.
- **NVIDIA** for the open GPU kernel modules and GSP firmware. NVIDIA components
  remain under their own licenses.

## License

This repository is distributed under the **GNU General Public License v2.0**.
See [LICENSE](LICENSE).

The installer downloads NVIDIA `open-gpu-kernel-modules` sources at build time;
those sources remain under NVIDIA's license. No proprietary firmware or VBIOS
images are distributed by this repository. NVIDIA, CUDA, and CMP are trademarks
of NVIDIA Corporation. This project is not affiliated with NVIDIA.
