# MiniDOS External Apps

This folder documents how to build external programs for MiniDOS and place
them in drive `A:` as `.ELF` files.

Important:
- These `.ELF` files are 32-bit executables loaded by the MiniDOS ELF loader.
- They are executed by the MiniDOS kernel in protected mode.

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
- Keep programs simple for now (no command-line arguments yet).

## Build and install utility

Use:

```bash
./external_apps/add_app.sh <path/to/app.c> [APPNAME]
```

Examples:

```bash
./external_apps/add_app.sh external_apps/templates/hello.c
./external_apps/add_app.sh external_apps/templates/stress.c STRESS
./external_apps/add_app.sh /tmp/my_app.c TESTAPP
```

The utility will:
1. Build a 32-bit ELF app.
2. Copy it to `A:` inside `minidos.img` as `.ELF`.

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

The repository also includes `external_apps/templates/stress.c`, a shell-launched
stress app that churns repeated directory scans, ELF reads, graphics presents,
and return-to-shell flow while emitting stable serial markers (`STRS100`,
`STRS110`, `STRS190`, `STRS900`) for regression scripts.

## Manual flow (reference)

If needed, manual commands are:

```bash
nasm -f elf32 external_apps/runtime/entry.asm -o build/external_apps/entry.o
gcc -m32 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pic -fno-pie \
    -fno-common -fno-asynchronous-unwind-tables -fno-stack-check -nostdlib \
    -Iexternal_apps/runtime -c my_app.c -o build/external_apps/app.o
ld -m elf_i386 -T external_apps/runtime/app.ld -o build/external_apps/app.elf \
    build/external_apps/entry.o build/external_apps/app.o
mcopy -o -i minidos.img build/external_apps/app.elf ::/MYAPP.ELF
```
