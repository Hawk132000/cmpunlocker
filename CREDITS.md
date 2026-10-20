# Credits and upstream lineage

cmpunlocker is a community project built on publicly shared research and code.
This fork would not exist without the people below. See [LICENSE](LICENSE) for
the project license; this file records attribution and does not change it.

## Core upstream

| Who | Contribution |
|---|---|
| [Amogh Munikote / amoghmunikote](https://github.com/amoghmunikote/cmpunlocker) | Original cmpunlocker project: patched open GPU kernel modules, memory geometry unlock, SM recovery, ECC work, install/remove/verify framework, and the experimental P2P branch. |
| [asm64-hooligan](https://github.com/asm64-hooligan/cmpunlocker) | FBPA PLL/MEM privilege-window map, memory-overclock lineage, and the command-line BAR1 placement approach. |
| [bayley](https://github.com/bayley/cmpunlocker) | BAR1 P2P research, PMA/WPR overlap fix, and real-transfer validation of P2P rather than capability-only checks. |

## This fork

| Who | Contribution |
|---|---|
| [vektorprime](https://github.com/vektorprime) | Original fork work: FBPA PLL/MEM integration for 170tune, the `cmpalign` full-size BAR1 helper, the experimental BAR1 P2P patch stack, and persistent P2P install/verify behavior. |
| [vektorprime/170tune](https://github.com/vektorprime/170tune) | Fork-specific 170tune integration, profiles, and validation used with the FBPA windows opened by this repository. |

## Additional upstream and related work

| Who | Contribution |
|---|---|
| [cachenetics/170tune](https://github.com/cachenetics/170tune) | Original 170tune tuning and qualification harness (MIT): SM/HBM levers, integrity gates, and the receipt/persistence model. |
| [Humvee55 / duggasco](https://github.com/duggasco) | Mailbox P2P transport, late-PMA region guard, and `--no-gen2` work in the related cmpunlocker lineage. |
| [bendy2](https://github.com/bendy2) | PCIe Gen2 retrain through a BAR0 `ioremap` in the driver, merged upstream as PR #18. |
| [IriKa](https://github.com/IRIKA) | ECC limiting work for the CMP 170HX. |
| [lesj0610](https://github.com/lesj0610) | GPU profiling. |
| [Arman Rafiee](https://github.com/armand0e) | Late-PMA reserved-region guard and RM thread-state timeout fixes. |
| [MeatKO](https://github.com/MeatKO) | DKMS removal mechanism. |
| [kira-ariaki](https://github.com/kira-ariaki) | Inventory reconciliation and journal-based log fallback in `verify.sh`. |
| [tehroot](https://github.com/tehroot) | PCIe Gen2 patch hunk-header correction. |
| [ProjectPhysX](https://github.com/ProjectPhysX/OpenCL-Benchmark) | OpenCL-Benchmark, used for the original performance results. |

## NVIDIA components

| Who | Contribution |
|---|---|
| [NVIDIA](https://github.com/NVIDIA/open-gpu-kernel-modules) | Open GPU kernel modules and GSP firmware. NVIDIA components remain under NVIDIA's own licenses; this repository does not relicense them and does not distribute firmware or VBIOS images. |

## Notes

- This is a fork, not the upstream project. Please direct upstream-specific issues
  to [amoghmunikote/cmpunlocker](https://github.com/amoghmunikote/cmpunlocker).
- Experimental BAR1 P2P patches are local to this fork and are not upstreamed.
- If a contribution is missing or incorrectly attributed, open an issue or PR with
  the relevant commit or link.
- NVIDIA, CUDA, and CMP are trademarks of NVIDIA Corporation. This project is not
  affiliated with or endorsed by NVIDIA.
