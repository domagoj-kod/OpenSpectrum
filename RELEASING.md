# Releasing OpenSpectrum

Releases are built automatically by GitHub Actions (`.github/workflows/release.yml`)
when you push a version tag. No more hand-compiling on two machines or hand-zipping
binaries (and forgetting their libraries).

## Cut a release

```bash
# 1. Make sure main is in the state you want to ship and is pushed.
git checkout main && git pull

# 2. Create a signed, annotated tag whose message IS the release notes.
git tag -s v2.5.0 -F /path/to/notes.txt     # or: git tag -s v2.5.0   (opens editor)

# 3. Push the tag. This is what triggers the pipeline.
git push origin v2.5.0
```

Within a few minutes a GitHub Release `v2.5.0` appears with two assets attached:

- `OpenSpectrum-v2.5.0-x86_64.AppImage` — self-contained Linux build (bundles
  SDL2/librtlsdr/libusb; runs on any modern x86-64 distro).
- `OpenSpectrum-v2.5.0-windows-x86_64.zip` — the `.exe` **plus all required MinGW
  DLLs** (SDL2, librtlsdr, libusb, libwinpthread, …).

The Release body is taken from the annotated tag's message.

## Test the pipeline without publishing

Use the **Run workflow** button (Actions tab → Release → Run workflow). It builds
both platforms and uploads them as **workflow artifacts** (downloadable from the
run page) but does **not** create a public Release. Good for a dry run.

## Build a bundle locally (same scripts CI uses)

```bash
make dist                 # current platform; version from `git describe`
make dist VERSION=v2.5.0  # explicit version label
```

- Linux needs: `build-essential pkg-config libsdl2-dev librtlsdr-dev
  libusb-1.0-0-dev curl file` — `curl` fetches `linuxdeploy`, and `appimagetool`
  shells out to `file`. No rasterizer is required: the app icon is the committed
  `packaging/openspectrum.png` (source: `packaging/openspectrum.svg`).
- Windows needs an MSYS2 MINGW64 shell with the toolchain + `make zip`.

### Dry-run the whole job locally with `act`

`act` (GitHub Actions in Docker) runs the Linux job end to end and produces a real
AppImage. Two expected, harmless quirks:

- The **upload-artifact** step fails with `Unable to get the ACTIONS_RUNTIME_TOKEN`
  — `act` has no artifact backend. It works on real GitHub runners. To make it go
  green locally, give `act` a local store:
  `act -j "Linux AppImage" --artifact-server-path /tmp/act-artifacts`.
- GitHub-hosted `ubuntu-latest` ships `file` preinstalled; the `act` image already
  has it, so only a bare local `make dist` may need `apt-get install -y file`.

## Cost / runner notes

- The workflow runs **only on tag push** (and manual dispatch), never on ordinary
  commits — roughly 25–30 billed minutes per release. Windows minutes bill ×2.
- Private-repo free tier is 2,000 minutes/month; with Stop-usage at $0 you can
  never be charged — workflows simply pause if the allotment is ever exhausted.

## Runtime note

The release binaries are built with `-march=haswell` (AVX2), so they require a
2013-or-newer x86-64 CPU. RTL-SDR access on Linux still needs the usual udev
permissions / plugdev membership; the AppImage bundles libraries, not device rules.
