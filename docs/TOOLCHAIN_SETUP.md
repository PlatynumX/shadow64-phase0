# Toolchain Setup

## Android/Termux path

Do not try to install the full libdragon Docker toolchain directly on Android. Use GitHub Actions from Termux with:

```bash
bash ~/storage/downloads/upload_shadow64_to_github_r11.sh
```

That script uploads the project and downloads the build artifact back to Android Downloads.

## PC/Linux path

Install Docker + Node.js, then:

```bash
npm install -g libdragon@latest
libdragon init --branch trunk
libdragon make
```

The official libdragon Docker/CLI flow uses Docker underneath; `libdragon init` downloads/starts the toolchain container and `libdragon make` builds the ROM from the current project directory.
