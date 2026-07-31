# Supplying ignored source trees

The repository intentionally does not track most imported vendor and middleware
source trees. This keeps project history focused on firmware-owned code and
avoids republishing large third-party distributions. A fork therefore needs
local copies of the ignored trees before it can build the firmware.

The ignored paths are:

- `Drivers/` — STM32F4 CMSIS, STM32F4 HAL, and the DP83848 component driver
- `Middlewares/` — lwIP 2.1.2, Mbed TLS, and the nested Mbed TLS Framework
  submodule
- `FreeRTOS-Kernel/` — FreeRTOS Kernel 11.1.0 with the Cortex-M4F GCC port

The Makefile deliberately compiles only the files required by this firmware.
Do not delete unused files from a locally installed vendor distribution solely
to reduce the build; unlisted files are not compiled.

## Prepare a fork

1. Clone the fork and initialize tracked submodules:

   ```sh
   git clone <fork-url>
   cd stmf4-health-check
   git submodule update --init --recursive
   ```

   The `--recursive` option is required. It installs both repositories recorded
   by the project:

   ```text
   Middlewares/Third_Party/MbedTLS/
   Middlewares/Third_Party/MbedTLS/framework/
   ```

   The first directory contains the pinned Mbed TLS 3.6 LTS source. The second
   is Mbed TLS Framework, a nested submodule used by the upstream development,
   generation, and test tooling. The firmware Makefile does not compile the
   Framework directly, but keeping it at the recorded revision makes the Mbed
   TLS checkout complete and reproducible.

   Submodule initialization does not install the other ignored source trees.

2. Copy the matching source trees from a known-good checkout or their upstream
   distributions, preserving these exact paths:

   ```text
   Drivers/BSP/Components/dp83848/
   Drivers/CMSIS/
   Drivers/STM32F4xx_HAL_Driver/
   FreeRTOS-Kernel/
   Middlewares/Third_Party/LwIP/
   ```

   Do not overwrite `Middlewares/Third_Party/MbedTLS/` after initializing the
   submodule.

3. Confirm that the required versions are present:

   - lwIP 2.1.2
   - FreeRTOS Kernel 11.1.0
   - the STM32F4 HAL/CMSIS and DP83848 sources compatible with this board
   - the Mbed TLS and Mbed TLS Framework commits recorded by the repository
     submodules

4. Build from the repository root:

   ```sh
   make clean
   make -j4
   ```

## Mbed TLS and Mbed TLS Framework

Mbed TLS is the only imported dependency pinned by this repository. Its
top-level Git submodule points to:

```text
https://github.com/Mbed-TLS/mbedtls.git
```

That repository declares its Framework dependency at:

```text
https://github.com/Mbed-TLS/mbedtls-framework
```

After cloning a fork, initialize both levels from the firmware repository root:

```sh
git submodule update --init --recursive
```

For an existing checkout that contains Mbed TLS but has an empty `framework/`
directory, synchronize the recorded URLs and initialize the nested submodule:

```sh
git submodule sync --recursive
git submodule update --init --recursive
```

Verify both pinned revisions:

```sh
git submodule status --recursive
```

Both output lines must begin with a space. A leading `-` means the submodule is
not initialized, a leading `+` means it is checked out at a different commit,
and a leading `U` indicates a merge conflict.

Do not copy a separate Mbed TLS release over the submodule directory and do not
independently update its Framework revision. Update these dependencies through
Git submodule commits so every collaborator and fork builds from the same
source.

## Sharing a fork

Ordinary `git add` does not include these directories because `.gitignore`
excludes them. That is intentional. Collaborators should normally document the
source package and version they used, then let each developer install the same
dependencies locally.

If a fork owner intentionally decides to publish one of the ignored source
trees, they must first verify its license and provenance. They can then either
change the fork's `.gitignore`, add an upstream repository as a submodule, or
force-add only the explicitly reviewed path:

```sh
git add -f path/to/reviewed/source
```

Never force-add an entire ignored directory without reviewing its contents.
Generated build artifacts, local logs, editor settings, credentials, and
private keys must remain untracked.
