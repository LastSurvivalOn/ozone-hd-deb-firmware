# Licensing

Copyright (C) 2026 LastSurvivalOn

This project is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version. It is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See [LICENSE](LICENSE) for the full text.

That covers everything in this repository (scripts, programs, configuration, documentation and
pictures) except the parts below, which keep the licenses of the code they come from or belong to.

| Part | License | Why |
|---|---|---|
| [native/gpu/ddx](native/gpu/ddx) | MIT, see its [COPYING](native/gpu/ddx/COPYING) | ARM's Mali X driver (and Dave Airlie's `compat-api.h`); our changes to it are under the same terms |
| [native/gpu/kernel](native/gpu/kernel) | GPL-2.0-only, see its [COPYING](native/gpu/kernel/COPYING) | a Linux kernel module (`fbump`) and patches to ARM's GPLv2 Mali and UMP drivers |
| [native/webkit/patches](native/webkit/patches) | BSD-2-Clause, see its [COPYING](native/webkit/patches/COPYING) | changes to WebKit files under that license (Igalia's), so they can go back to WebKit |

## Proprietary parts the programs work with

The box needs some of its vendors' binaries, which are not in this repository and not covered by
its licenses: ARM's Mali-450 user-space driver (`libMali.so`, `libUMP.so`, downloaded by
`native/install-gpu.sh`), the vendor kernel modules (NAND, WiFi, Mali), and the Android libraries
of the stock firmware that `native/android-files.txt` lists (the TV frontend among them), copied
from the user's own backup. Some of our programs load them: `native/gpu/egl-fix` links
`libMali.so`, and `native/dtv/dtvfe` loads the TV application's libraries.

So, as an additional permission under GNU GPL version 3 section 7:

> If you modify this Program, or any covered work, by linking or combining it with ARM's Mali
> user-space driver libraries, or with the libraries of the box's stock Android firmware (or
> modified versions of those libraries), containing parts covered by the terms of their own
> licenses, the licensors of this Program grant you additional permission to convey the resulting
> work. Corresponding Source for a non-source form of such a combination shall not include the
> source code for the parts of those libraries.
