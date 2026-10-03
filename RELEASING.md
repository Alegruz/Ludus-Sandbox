# Release Ludus Sandbox to itch.io

Open `ludus.project.json` in the Ludus Editor. This repository already contains
its game-owned release configuration and workflow; **Release > Package Release**
can build the `web-release` profile after browser Release inputs are prepared.
**Set Up Releases** is for projects without these files and refuses overwrites.

## Prepare and package locally

```bash
./scripts/release-prepare
ludus project package . --profile web-release --version 0.1.0
ludus project package verify <printed-package-directory>
```

`release-prepare` runs the existing bootstrap with `--with-web --web-release`.
It builds the pinned engine's native Development SDK and browser Release SDK,
acquires pinned shader tools, writes ignored machine-local CMake presets, and
configures both trees. To reuse installed inputs during iteration:

```bash
./scripts/release-prepare --ludus-source /path/to/Ludus --sdk-dir /path/to/native-development-sdk --web-sdk-dir /path/to/browser-release-sdk
```

The browser Release package contains `index.html`, current `index.js` and
`index.wasm`, the iframe test page, notices and SDK/toolchain licenses. Shaders are compiled and embedded
at build time. Headers, static SDK libraries, shader compilers and debug files
are excluded. SDK identity, source state, engine-lock/config digests and file
hashes/modes are recorded. The engine lock is explicitly unresolved because the
SDK is built from the immutable source pin in `config/ludus-version.txt`; the
package records the actual configured SDK identity and is treated as local inputs.

The release workflow extracts `game.zip` and runs the existing Chromium pixel,
controls, fallback and iframe checks before uploading. Static verification checks
package consistency; software-GPU tests do not establish hardware performance
or hosted itch.io acceptance.
Both browser app presets use the Release SDK; development and release app
build trees remain separate.

## Configure automatic uploads once

Create an itch.io game page and configure the repository on GitHub:

1. Set repository variable `ITCH_IO_TARGET` to `username/game`.
2. Create environment `itch-release` and its secret `BUTLER_API_KEY` (a butler/wharf
   API key). Enter it directly in GitHub; never put it in this repository.
3. Merge the release integration, then push a `v*` tag or manually run the
   **itch.io release** workflow with a version.

The first job builds, verifies and browser-tests the exact package without upload credentials. The
upload job downloads the artifact and verifies its exact digest again. Only its
upload step receives the secret. It uses engine tooling pinned by
`config/ludus-tools-revision.txt` and official butler pinned by version and SHA256.
Uploads are serialized; failed or interrupted uploads are not automatically retried.
Inspect the saved upload receipt before retrying an unknown outcome.

The destination mapping uses channel `html5`. After the first push, select the
HTML game type and mark that channel playable in browser in itch.io's Edit game
page. Set the game viewport appropriately and test the hosted build. A successful
upload receipt means the upload was submitted, not that hosted gameplay passed.
The page's visibility remains an itch.io setting.

Native Vulkan packaging is not configured by this browser release profile; it
needs its own reviewed runtime dependency rules.
