# Uploading this repo to GitHub

## Option A: website only (no git needed)
1. Unzip `ps4-ambilight-ir-bridge.zip`.
2. On github.com click **+** -> **New repository**.
   Name: `ps4-ambilight-ir-bridge`, Public, **do not** tick "Add a README".
3. Click **uploading an existing file**.
4. Drag in the *contents* of the unzipped folder (including the `firmware`, `docs`
   and `.github` folders). Hidden files like `.gitignore` may not appear in your
   file picker; use Option B if so.
5. Write a commit message such as `Initial commit (untested)` and click **Commit changes**.

## Option B: git command line
```bash
cd ps4-ambilight-ir-bridge
git init
git add .
git commit -m "Initial commit (untested)"
git branch -M main
git remote add origin https://github.com/ahmadanka1/ps4-ambilight-ir-bridge.git
git push -u origin main
```
Create the empty repository on github.com first (step 2 above).

## After uploading
- Check that `config.h` is **not** in the repo (only `config.example.h`).
- Add a description and topics: `ps4`, `ambilight`, `esp32`, `ir`, `goldhen`.
- Optional: open an issue on mitchshimy/PS4-Ambilight to tell the author about the project.
