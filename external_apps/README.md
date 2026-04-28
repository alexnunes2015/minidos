# MiniDOS External Apps

This folder documents how to build external programs for MiniDOS and place
them in drive `A:` as `.ELF` or `.COM` files.

Important:
- These `.ELF` files are 32-bit executables loaded by the MiniDOS ELF loader.
- Legacy `.COM` files are flat 32-bit binaries loaded at the same user-mode base address.
- Both formats are executed by the MiniDOS kernel in protected mode on the same ring3 runtime.
- ELF/COM programs are mapped at `0x01000000` into a dynamic per-app span; the current maximum mapped user span is 4 MiB.

## Requirements

- `gcc` with 32-bit output support (`-m32`)
- `ld`
- `nasm`
- `mtools` (`mcopy`)
- `minidos.img` already created (`make`)

## App contract

- Implement `int app_main(const minidos_app_api_t* api)` in C.
- No libc: do not use `printf`, `malloc`, or system headers that need runtime.
- Return an integer exit code; the shell prints it after execution.
- Use `app_puts(api, "...")` for output.
- Use `app_get_char(api)` for single-char input.
- Use `app_file_size(api, "FILE.ELF")` for basic file stat.
- Use `app_drive_valid(api, 0)` to test whether drive `A:` is mounted; pass
  `1` for `B:`, up to `25` for `Z:`.
- Use `app_chdir(api, "A:")` to switch the app filesystem context to a drive
  root before listing or opening entries there.
- Keep programs simple for now (no command-line arguments yet).

## Build and install utility

Use:

```bash
./external_apps/add_app.sh [--format elf|com] [--dir DIRNAME] <path/to/app.c> [APPNAME]
```

Examples:

```bash
./external_apps/add_app.sh external_apps/apps/hello/hello.c
./external_apps/add_app.sh external_apps/apps/stress/stress.c STRESS
./external_apps/add_app.sh --dir PTEST external_apps/apps/ptio/ptio.c PTIO
./external_apps/add_app.sh --format com external_apps/apps/hello/hello.c HELLOCOM
./external_apps/add_app.sh /tmp/my_app.c TESTAPP
```

Each bundled example lives under `external_apps/apps/<name>/<name>.c`, so point the helper at that path when you want to rebuild an app or copy it into `minidos.img`.
The helper now compiles every sibling `*.c` file in that app directory, so UI apps like `STARTUI` can be split into smaller modules without `#include`-ing implementation files.

The utility will:
1. Build a 32-bit app in the selected format (`.ELF` by default, `.COM` with `--format com`).
2. Copy it to `A:` inside `minidos.img` with the matching extension, or into `A:\DIRNAME` when `--dir` is used.

When `--dir` is used, the helper also copies non-code companion files that sit next to the source file (for example `.BMP`, `.ICO`, `.WAV`, `.TXT`) into the same target directory. The bundled `STARTUI` desktop resources are installed by `make` under `A:\AIOS`.
If `assets/Icons/Folder.png` exists, the `STARTUI` build also regenerates
`external_apps/apps/win95_demo/win95_icons.h` automatically and copies the
source PNG to `A:\AIOS\FOLDER.PNG`.

If `assets/cursor/cursor.png` exists, the app build also regenerates
`external_apps/runtime/minidos_cursor_bitmap.h` automatically before compilation.
If the PNG is absent, `assets/cursor/cursor.bmp` is used as fallback.

After booting MiniDOS, execute by typing:

```text
hello
```

or (equivalent):

```text
run hello
```

The repository also includes `external_apps/apps/stress/stress.c`, a shell-launched
stress app that churns repeated directory scans, ELF reads, graphics presents,
and return-to-shell flow while emitting stable serial markers (`STRS100`,
`STRS110`, `STRS190`, `STRS900`) for regression scripts.

`external_apps/apps/ptbig/ptbig.c` is the large-ELF regression app. Its `.bss`
pushes the mapped image beyond the old 1 MiB slot and emits `PTBIG100` /
`PTBIG190` when the dynamic userland span is working.

## Manual flow (reference)

If needed, manual commands are:

```bash
nasm -f elf32 external_apps/runtime/entry.asm -o build/external_apps/entry.o
gcc -m32 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pic -fno-pie \
    -fno-common -fno-asynchronous-unwind-tables -fno-stack-check -nostdlib \
    -Iexternal_apps/runtime -Imy_app_dir -c my_app_dir/main.c -o build/external_apps/main.o
gcc -m32 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pic -fno-pie \
    -fno-common -fno-asynchronous-unwind-tables -fno-stack-check -nostdlib \
    -Iexternal_apps/runtime -Imy_app_dir -c my_app_dir/view.c -o build/external_apps/view.o
ld -m elf_i386 -T external_apps/runtime/app.ld -o build/external_apps/app.elf \
    build/external_apps/entry.o build/external_apps/main.o build/external_apps/view.o
mcopy -o -i minidos.img build/external_apps/app.elf ::/MYAPP.ELF
```

For a flat `.COM` binary instead, link with `external_apps/runtime/app_com.ld` and
convert the intermediate ELF with `objcopy -O binary`.
