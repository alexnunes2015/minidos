# MiniDOS External Apps

This folder documents how to build external programs for MiniDOS and place
them in drive `A:` as `.COM` files.

Important:
- These `.COM` files are **not** DOS real-mode `.COM` files.
- They are 32-bit flat binaries executed by the MiniDOS kernel in protected mode.

## Requirements

- `gcc` with 32-bit output support (`-m32`)
- `ld`
- `objcopy`
- `nasm`
- `mtools` (`mcopy`)
- `minidos.img` already created (`make`)

## App contract

- Implement `int app_main(const minidos_app_api_t* api)` in C.
- No libc: do not use `printf`, `malloc`, or system headers that need runtime.
- Return an integer exit code; the shell prints it after execution.
- Use `api->puts` / `app_puts(api, "...")` for output.
- Use `api->get_char` / `app_get_char(api)` for single-char input.
- Keep programs simple for now (no command-line arguments yet).

## Build and install utility

Use:

```bash
./external_apps/add_app.sh <path/to/app.c> [APPNAME]
```

Examples:

```bash
./external_apps/add_app.sh external_apps/templates/hello.c
./external_apps/add_app.sh /tmp/my_app.c TESTAPP
```

The utility will:
1. Build a 32-bit `.COM` binary.
2. Copy it to `A:` inside `minidos.img`.

After booting MiniDOS, execute by typing:

```text
hello
```

or (equivalent):

```text
hello.com
```

## Manual flow (reference)

If needed, manual commands are:

```bash
nasm -f elf32 external_apps/runtime/entry.asm -o build/external_apps/entry.o
gcc -m32 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pic -fno-pie \
    -fno-common -fno-asynchronous-unwind-tables -fno-stack-check -nostdlib \
    -Iexternal_apps/runtime -c my_app.c -o build/external_apps/app.o
ld -m elf_i386 -T external_apps/runtime/app.ld -o build/external_apps/app.elf \
    build/external_apps/entry.o build/external_apps/app.o
objcopy -O binary build/external_apps/app.elf build/external_apps/MYAPP.COM
mcopy -o -i minidos.img@@1048576 build/external_apps/MYAPP.COM ::/MYAPP.COM
```
