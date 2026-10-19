# Full-size BAR1 (64 GB) — the `cmpalign` backport

Status: **working** (verified on `7.0.0-38-generic`, both cards at `final size = 65536 MB`).

TL;DR — on kernel v7.0 NVRM tries to grow BAR1 to 64 GB, the kernel's resource
allocator cannot place the resulting bridge window, and BAR1 silently falls back to
32 GB (half the card's 64 GB VRAM). `driver/cmpalign/` is a ~100-line out-of-tree
kernel module that backports the missing v7.1 allocator fix with a kprobe. It is
installed by `install.sh --with-bar1-64g` (opt-in; default off so a plain install
matches upstream's 32 GB fallback), removed by `remove.sh`, checked by `verify.sh`,
and armed before `nvidia` at every boot via a modprobe soft dependency. An
alternative cmdline-based method (`--with-bar1-cmdline`, the asm64-hooligan fork's
approach) is also available — see below.

---

## Symptom

With the unlock's `NVreg_EnableResizableBar=1`, every NVIDIA module load logs:

```
NVRM: CMP BAR1: resize enabled, will attempt up to 64 GB
NVRM: CMP BAR1: final size = 32768 MB        <- want 65536
```

and `nvidia-smi -q -d MEMORY` reports `BAR1 Memory Usage Total: 32768 MiB` on cards
that have 65536 MiB VRAM. Nothing errors; the fallback is silent.

Why it matters: BAR1 is the aperture through which *other* PCIe agents (the peer
GPU, an RDMA NIC, the CPU) address the card's VRAM. With BAR1 at 32 GB only the
lower half of each card is visible to P2P/GPUDirect/zero-copy. Single-GPU CUDA
compute is unaffected (cudaMalloc never required BAR1 ≥ VRAM), so this is a
cross-GPU / RDMA concern, not an inference-capacity concern.

---

## Root cause

The window NVRM must place after the resize is

| | value |
|---|---|
| window size | `0x1002000000` = 64 GiB BAR1 + 32 MiB BAR3 |
| alignment | 64 GiB (bridge min-window alignment) |
| remainder | `size & (align-1)` = 32 MiB |
| root window | `4000000000-7fffffffff` (256 GiB, top of 39-bit phys) |

On **kernel v7.0** the allocator starts every candidate at an *aligned* address and
never moves it back down, so each candidate overflows its free gap by exactly the
32 MiB remainder:

- top gap → candidate `0x7000000000`, spans to `0x8001ffffff` — 32 MiB past the
  top of RAM (`0x7fffffffff`)
- gap below the other card → candidate `0x5000000000`, spans to `0x6001ffffff` —
  32 MiB into the neighbor's window (`5800000000-6001ffffff`)

Both attempts fail (`can't assign; no space`), and NVRM retries with a 32 GB BAR1,
which happens to fit — hence the observed fallback windows
(`4800000000-5001ffffff` / `5800000000-6001ffffff`).

**v7.1 fixed this** by subtracting the remainder from the candidate before the
containment check ("tail remainder relocation to space before `res->start`"):

- upstream commit [`9036bd0efcb61`](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=9036bd0efcb6162a77f3bf9bacbafba7686c7275)
  — *"PCI: Align head space better"*, first release **v7.1**
- it adds `pci_align_resource()` in `drivers/pci/setup-res.c` and makes the weak
  `pcibios_align_resource()` forward to it; on v7.0 both the weak wrapper and
  `arch/x86/pci/i386.c` simply `return res->start` for MEM resources

Ubuntu `7.0.0-38-generic` is a v7.0 base, so the fix is absent — confirmed by
`grep`ing the function out of the v7.0 vs v7.1 sources.

With the v7.1 subtraction the candidates become valid — one card takes the mid
window, the other the top window (which card gets which depends on the free-block
walk order and can differ between boots):

| window (final) | derivation |
|---|---|
| `4ffe000000-5fffffffff` (64 GiB + 32 MiB) | `0x5000000000 - 0x2000000` |
| `6ffe000000-7fffffffff` (64 GiB + 32 MiB, flush to top) | `0x7000000000 - 0x2000000` |

---

## The fix: `driver/cmpalign/cmpalign.c`

An out-of-tree module that registers a **kprobe on `pcibios_align_resource`**
(x86-64 SysV args read from `pt_regs`: `si`=res, `dx`=size, `cx`=align) and, in the
pre-handler, reproduces the v7.1 subtraction by editing `res->start` before the
original function runs. The v7.0 allocator passes its stack-local `avail` to the
align function and then uses `avail` for the containment check, so the edit is
honoured — no kernel image is modified.

Guards (all must pass, otherwise the probe is a no-op):

1. `res->flags & IORESOURCE_MEM` — memory resources only
2. `res->start > SZ_4G` — only high bridge windows; device BARs and anything low are never touched
3. `align` is a nonzero power of two
4. `size & (align-1) != 0` — size *not* aligned ⇒ remainder exists (ordinary device BARs have `size == align` and exit here)
5. `remainder = size & (align-1)` is a multiple of 1 MiB (x86 window granularity)
6. `res->start >= remainder` — no underflow

Then: `res->start -= remainder`, logged as
`cmpalign: shifted window candidate down by 0x2000000 (start now 0x4ffe000000)`.

Deliberate differences from upstream:

- **No `empty_res` guard** — v7.1 also refuses a candidate that would start *below*
  the free gap. The probe cannot see the caller's free-space descriptor, so this
  check is omitted; if a shifted candidate lands below the gap the kernel simply
  rejects it inside `__request_resource` and falls through, i.e. the worst case is
  the old behaviour. On this host every gap pad is far larger than any 1 MiB
  remainder, and boot-time PCI assignment completes seconds before the module loads.
- **Version gate** — `init()` returns `-ENODEV` when
  `LINUX_VERSION_CODE >= KERNEL_VERSION(7,1,0)`, so the module can stay installed
  forever without double-shifting once the kernel moves to a 7.1-based HWE.

---

## Alternative method: kernel cmdline (`--with-bar1-cmdline`)

The asm64-hooligan fork solves the same v7.0 placement failure from the other side:
instead of fixing the candidate alignment, it enlarges the space the allocator has
to work with —

- `pci=realloc` — allow the kernel to re-shuffle existing BAR allocations
- `pci=hpmmioprefsize=2T` — grow the prefetchable high-MMIO window to 2 TB so the
  64 GiB-aligned candidates fit without overflowing their gaps

(plus **Above 4G Decoding** in BIOS/UEFI). This repo carries that approach as an
alternative opt-in flag: `sudo ./install.sh --with-bar1-cmdline` writes the params
to GRUB or `/etc/kernel/cmdline`, reusing the IOMMU cmdline machinery and its
`.cmpunlocker.bak` backup that `remove.sh` restores.

| | cmpalign (`--with-bar1-64g`) | cmdline (`--with-bar1-cmdline`) |
|---|---|---|
| mechanism | v7.1 alignment backport (kprobe) | enlarge/reallocate MMIO space |
| touches cmdline | no | yes (2 params) |
| BIOS requirement | none beyond stock | Above 4G Decoding |
| scope | only the oversized MEM allocations | whole PCI resource layout |
| extra module | yes (`cmpalign.ko`, taints) | none |
| kernel-upgrade upkeep | rebuild the `.ko` | none (params persist) |

Both methods are opt-in, independent, and may be combined; without either flag
BAR1 stays at the stock 32 GB fallback (upstream-identical behavior). The
experimental BAR1-P2P patches (`--with-p2p`) expect a full-size BAR1 — either
method satisfies that; the cmdline flag is the lighter pairing since it adds no
module.

**Measured result on this host (2026-10-16, dual CMP 170HX):** the cmdline method
alone is **not sufficient**. With `pci=realloc pci=hpmmioprefsize=2T` active and
cmpalign removed, both cards still log `final size = 32768 MB` (REBAR itself is
unlocked correctly: `CYA 0x26→0x30`, `CAP 0x400→0x1ffc00`). The board's firmware
pre-assigns each card a 64 GiB + 32 MiB prefetchable window; `hpmmioprefsize` only
shapes windows the *kernel* carves, and NVRM's own fallback to 32 GB succeeds
before `pci=realloc` ever sees a failed BAR to re-place. The kprobe backport
remains the method that delivers 64 GB on this class of system; the cmdline flag
is kept for hosts whose firmware leaves the prefetch window to the kernel.

---

## Integration (how to replicate)

| piece | where | what it does |
|---|---|---|
| module source | `driver/cmpalign/{cmpalign.c,Makefile,.gitignore}` | plain out-of-tree kbuild, mirrors `driver/passthrough/` |
| install | `install.sh --with-bar1-64g` (step "Building and installing cmpalign"; **default off**) | `make` → `install` `.ko` into `/lib/modules/$(uname -r)/updates/cmpunlocker/` → `depmod` → write `/etc/modprobe.d/cmpalign.conf` → `modprobe cmpalign`; a build failure only `warn`s (the unlock itself is unaffected) |
| remove | `remove.sh` | `rmmod cmpalign`, deletes the `.ko` (via the existing `updates/cmpunlocker` wipe) and the softdep file — *before* its nvidia reload, so the same run returns to 32 GB |
| verify | `verify.sh` Step 4 | `.ko` installed? module loaded? `nvidia-smi` BAR1 total = 65536 (non-fatal warn otherwise) |
| test | `tests/test_compile.py::test_cmpalign_module_builds` | compiles the module against the running kernel's headers in a temp dir |

Boot persistence relies on the soft dependency only:

```
# /etc/modprobe.d/cmpalign.conf
softdep nvidia pre: cmpalign
```

- nvidia is **not** packed into the initramfs on this system (verified with
  `lsinitramfs`), so it always loads from the rootfs via modprobe (~6.4 s at boot);
  modprobe loads `cmpalign` first, synchronously, then `nvidia` — which runs the
  BAR1 resize with the probe armed.
- kmod treats soft-dependency failures as non-fatal (see
  `kmod_module_probe_insert_module()`: softdeps are inserted with
  `required = false`), so if `cmpalign.ko` is ever missing (e.g. after a kernel
  upgrade before `install.sh` is re-run) `nvidia` still loads and BAR1 returns to
  the 32 GB fallback — the failure mode is graceful, never a dead GPU.

Replicating on a fresh machine: `sudo ./install.sh --with-bar1-64g` (same requirement
as the patched NVIDIA modules — re-run it after every kernel upgrade), then reboot.

---

## Expected result

Verified cold-boot log (`7.0.0-38-generic`):

```
$ sudo dmesg | grep -E 'cmpalign|CMP BAR1'
[    5.694547] cmpalign: loading out-of-tree module taints kernel.
[    5.694552] cmpalign: module verification failed: signature and/or required key missing - tainting kernel
[    5.705725] cmpalign: armed kprobe on pcibios_align_resource
[    6.448336] cmpalign: shifted window candidate down by 0x2000000 (start now 0x4ffe000000)
[    6.448369] NVRM: CMP BAR1: final size = 65536 MB
[    6.496938] cmpalign: shifted window candidate down by 0x2000000 (start now 0x4ffe000000)
[    6.496940] cmpalign: shifted window candidate down by 0x2000000 (start now 0x5ffe000000)
[    6.496941] cmpalign: shifted window candidate down by 0x2000000 (start now 0x6ffe000000)
[    6.496943] cmpalign: shifted window candidate down by 0x2000000 (start now 0x6ffe000000)
[    6.497004] NVRM: CMP BAR1: final size = 65536 MB

$ nvidia-smi -q -d MEMORY | grep -A2 'BAR1 Memory Usage' | grep Total
        Total  : 65536 MiB
        Total  : 65536 MiB

$ sudo ./verify.sh        # Step 4/4: ✓ BAR1 total is 65536 MiB (full-size window)
```

Notes:

- The two `taints kernel` lines are expected and benign (out-of-tree, unsigned
  module; Secure Boot is off so the signature warning only taints, it does not block).
- The probe is armed at ~5.7 s (softdep loads cmpalign before nvidia), the resizes
  run at ~6.4 s — i.e. boot-time ordering is correct on every boot.
- One GPU typically logs a single `shifted` line, the other several: the probe also
  fires on candidates the allocator goes on to reject (each free-gap block is tried
  once). What matters is the two `final size = 65536 MB` lines.
- Which card ends in the mid window (`4ffe000000-5fffffffff`) vs the top window
  (`6ffe000000-7fffffffff`) depends on the free-block walk order and can differ
  between boots; both are valid.

---

## Reversibility

Instant rollback to stock v7.0 behaviour (kernel image untouched):

```bash
sudo rmmod cmpalign                      # disarms the kprobe
# reload the NVIDIA modules              # → final size = 32768 MB
sudo modprobe cmpalign                   # re-arm (or just reboot)
```

Full removal: `sudo ./remove.sh --yes` (drops module, softdep and restores stock
NVIDIA modules).

Failure modes, all graceful:

| situation | behaviour |
|---|---|
| module build fails during install | warning only; unlock proceeds, BAR1 stays 32 GB |
| `cmpalign.ko` missing at boot | kmod ignores the softdep; nvidia loads normally |
| kernel ≥ 7.1 | module init returns `-ENODEV` (kernel already has the fix) |
| `rmmod` while something holds it | fails with EBUSY; stock behaviour unchanged |

---

## Troubleshooting

- **`rmmod: ERROR: Module nvidia is in use`** — a lingering `nvidia-smi` (or any
  process with `/dev/nvidia*` open, e.g. a monitoring loop) pins the driver.
  `fuser -v /dev/nvidia*` shows the holder; kill it (and its parent loop, `ps -fp <PPID>`)
  and retry. `nvidia_drm`/`nvidia_uvm`/`nvidia_modeset` must unload before `nvidia`.
- **BAR1 still 32768 after reload** — check `lsmod | grep cmpalign` and
  `sudo dmesg | grep cmpalign`. No `shifted` lines means the probe did not fire:
  module not loaded (re-run the install step) or the softdep file was removed.
- **`cmpalign: kernel 7.x already ships pci_align_resource; staying disarmed`** —
  expected on future HWE kernels; the module becomes a no-op, the kernel does the work.
- **After a kernel upgrade** both the patched `nvidia.ko` *and* `cmpalign.ko` are
  absent for the new kernel — re-run `sudo ./install.sh`, as always with this repo.

---

## Alternatives considered

| option | why not |
|---|---|
| rebuild the Ubuntu kernel with the v7.1 patch | upstream-exact, but 30-60 min builds, replaces the running `vmlinuz`, must match Ubuntu source/config to keep vermagic and module CRCs, and every test iteration needs a reboot |
| wait for a 7.1 HWE kernel | zero effort but unknown timeline; stays at 32 GB until then |

The kprobe module is minutes to build, testable without rebooting (`rmmod`/`modprobe`
nvidia), trivially reversible, self-disabling on ≥7.1, and leaves the kernel image
untouched — hence it landed here.

## References

- Upstream fix: commit `9036bd0efcb6162a77f3bf9bacbafba7686c7275` — *PCI: Align head space better* (in v7.1, absent from v7.0)
- `drivers/pci/setup-res.c` (`pci_align_resource`, weak `pcibios_align_resource`), `kernel/resource.c` (`__find_resource_space`) in v7.0 vs v7.1
- `arch/x86/pci/i386.c` (`pcibios_align_resource` on x86)
- Repo: `driver/cmpalign/`, `install.sh`, `remove.sh`, `verify.sh`, `tests/test_compile.py`
