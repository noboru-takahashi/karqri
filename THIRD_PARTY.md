# External dependencies and distribution boundaries

Karqri's own source and documentation are covered by the root [MIT LICENSE](LICENSE).
That license does not relicense external dependencies or the separate CHARM-II
restoration materials. Source provenance and rights ownership require separate
evidence; this dependency inventory does not establish that those questions have
been resolved.

## Current source-only publication

The current publication includes Karqri source, documentation, demos, tests, and
tools only. It does not distribute UF2/ELF firmware, host executables, SDK or
third-party source checkouts, Python runtimes, or Python dependency packages.
Build directories and generated outputs are outside this distribution boundary.
Dependencies are obtained separately by the person building or running the code.

| Dependency | Use | License and distribution boundary |
| --- | --- | --- |
| Raspberry Pi Pico SDK 2.3.0 | Pico build, hardware timer, USB stdio | 2.3.0 is the verified environment version, not a version enforced by Karqri's CMake configuration. CMake imports an external checkout through `PICO_SDK_PATH`. The inspected SDK's `LICENSE.TXT` has BSD-3-Clause terms; individual components can have different licenses. SDK source is not vendored. |
| TinyUSB | USB CDC through the Pico SDK | Compiled by the configured Pico build. The inspected SDK's `lib/tinyusb/LICENSE` is MIT. TinyUSB source is not vendored; component notices must be considered when distributing firmware that includes it. |
| pico_printf | SDK-provided formatted output in the inspected Pico build | The SDK's `src/rp2_common/pico_printf/printf.c` and adjacent `LICENSE` identify Marco Paland's MIT-licensed implementation. This is an example of an SDK component with its own notice, separate from the SDK's top-level license. Its source is not vendored. |
| picotool 2.3.0 | Host-side UF2 generation and firmware post-processing through the SDK | The inspected build uses 2.3.0; its `LICENSE.TXT` has BSD-3-Clause terms. The SDK can obtain and build it when a suitable installed tool is unavailable. The tool is not bundled in the Karqri source distribution. Using it to generate UF2 does not itself distribute the host tool. |
| ARM GNU toolchain / libgcc | Compilation and target runtime support | The toolchain is external. GCC runtime files bearing the applicable notice use GPLv3 with the GCC Runtime Library Exception; that is not a blanket license for every toolchain component. Before binary distribution, identify the runtime code actually included and verify its applicable terms and exception conditions. |
| newlib | C runtime selected in the inspected Pico build | External to Karqri. newlib contains code from multiple sources with file-specific terms; it must not be described as having one blanket license. Before firmware distribution, identify the included code and its applicable notices. |
| POSIX / host libc, including `ucontext` | Host port and demo | Host-provided headers/runtime; no libc implementation is vendored. Terms depend on the selected host/runtime and, for executable distribution, the components included and the linking method. |
| Python 3.10+ standard library | Collector, Viewer, and offline tests | Interpreter/runtime not bundled. Python has PSF license terms and incorporated components with their own terms. Viewer imports `Validation` from this repository's `capture.py`. |
| pyserial 3.5 | Live Windows COM capture only | BSD-3-Clause; pinned in `tools/pico_validation/requirements.txt` and not vendored. The official source archive includes `LICENSE.txt`; the inspected official wheel has copyright/SPDX notices and BSD metadata but no full license-text file. Offline tests, replay, and Viewer do not require pyserial. |
| C compiler, CMake, Ninja, Make | Build tools | Obtained separately, not bundled. Tool use and redistribution of the tools are distinct from distribution of Karqri source or generated firmware. |

### pyserial 3.5 package evidence

The official `pyserial-3.5.tar.gz` includes `pyserial-3.5/LICENSE.txt`, with
BSD-3-Clause terms. The inspected `pyserial-3.5-py2.py3-none-any.whl` contains
BSD license metadata and source-file copyright/SPDX identifiers, but no full
license-text file. Both archives were checked against the SHA-256 values in
[PyPI's 3.5 release metadata](https://pypi.org/pypi/pyserial/3.5/json).
The [upstream 3.5 license](https://github.com/pyserial/pyserial/blob/v3.5/LICENSE.txt)
provides the terms.

For redistribution of pyserial, those terms require retention of the copyright,
conditions, and disclaimer in source redistributions, reproduction of them in
accompanying documentation or materials for binary redistributions, and no use of
rights-holder or contributor names for endorsement without permission. A wheel's
metadata alone must not be assumed to supply the full required notices when
bundling it. The current requirements file refers to an independently obtained
package; Karqri does not redistribute that package in this source publication.

## Future binary or bundled distributions

UF2/ELF firmware, host tools, and bundled Python environments are not part of the
current publication. Before distributing any of them:

- Identify the actual distributed versions and included components. A library
  named in a link command or a map's `LOAD` entry does not by itself establish
  which code survives in the final binary.
- For firmware, review the included SDK components, TinyUSB, pico_printf, newlib,
  libgcc, and any other linked code. The SDK's top-level BSD license alone is
  not a complete binary notice inventory. The [GCC Runtime Library Exception](https://github.com/gcc-mirror/gcc/blob/releases/gcc-10.3.0/COPYING.RUNTIME)
  applies only where its terms and the relevant file notices support it.
- If redistributing picotool or another host executable, review that tool and its
  bundled/linked dependencies separately from the firmware it generates.
- If bundling Python, pyserial, or a packaged collector, review the actual runtime
  and packages and supply their required texts and notices. Do not assume the
  pyserial wheel contains the complete license text.
- Supply the license texts and notices required for the components actually
  redistributed. This inventory is not a substitute for those materials.

Referencing a separately obtained dependency does not by itself establish that
its complete license text must be copied into this source-only repository.
Conversely, the non-vendored boundary does not settle source-provenance questions
or all obligations of a future distribution. No blanket dependency or binary
license clearance is claimed here.
