# Shadow64 R11 Termux quick start

Put these files in Android Downloads:

- shadow64_phase0_r11_android_upload.zip
- upload_shadow64_to_github_r11.sh

Then paste:

```bash
termux-setup-storage
pkg update -y && pkg install -y git gh unzip jq coreutils findutils python zip
cp ~/storage/downloads/upload_shadow64_to_github_r11.sh ~/upload_shadow64_to_github_r11.sh
bash ~/upload_shadow64_to_github_r11.sh
```

Expected ROM:

```text
~/storage/downloads/shadow64_phase0_r11-run-<RUN_ID>.z64
```

If it fails, upload:

```text
~/storage/downloads/shadow64_r11_run_<RUN_ID>_diagnostics.zip
```
