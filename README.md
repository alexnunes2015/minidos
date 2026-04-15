# minidos

Sistema operacional educacional com bootloader BIOS, kernel 16/32-bit e shell básica. O projeto está preparado para um workflow `AI-agent first`, com validação automatizada, debug por serial e execução em QEMU.

## Estrutura

- [src/boot/](src/boot/) — boot sector + stage2.
- [src/kernel/core/](src/kernel/core/) — entry point, linker script e coordenação principal do kernel.
- [src/kernel/video/](src/kernel/video/) — consola, framebuffer e VGA.
- [src/kernel/storage/](src/kernel/storage/) — ATA, drives e FAT, com separação entre core FAT, diretórios e I/O de ficheiros.
- [src/kernel/input/](src/kernel/input/) — teclado e rato.
- [src/kernel/time/](src/kernel/time/) — RTC e timer/PIT.
- [src/kernel/debug/](src/kernel/debug/) — serial e logging.
- [src/kernel/memory/](src/kernel/memory/) — paging.
- [src/kernel/process/](src/kernel/process/) — processo e scheduler.
- [src/kernel/shell/](src/kernel/shell/) — shell e runtime de apps.
- [scripts/](scripts/) — criação de imagens de disco.
- [tests/](tests/) — testes em QEMU e utilitários.
- [docs/](docs/) — notas de design, fixes e troubleshooting.
- [assets/bootlogo/](assets/bootlogo/) — arte do logo de boot.

## Build e execução

```sh
make
make run
```

Validação e debug rápidos:

```sh
make verify-image
make phase0-check
make ci
make run-no-reboot
make run-trace
make run-gdb
make gdb-kernel
```

## Testes

```sh
make test
make test-ver
make test-dir
make test-drives
make test-serial
make test-graphics
make phase0-check
make ci
```

Para rodar comandos personalizados:

```sh
./tests/test_runner.sh "ver" "drives"
```

## Dependências

- gcc (com suporte a -m32)
- ld
- objcopy
- nasm
- qemu-system-i386
- mkfs.vfat
- mtools (`mdir`, `mcopy`, `mmd`; `mformat` para alguns testes)
- gdb (opcional, para `make gdb-kernel`)

## Logo de boot (opcional)

Veja [assets/bootlogo/README.md](assets/bootlogo/README.md) para converter imagens e gerar [assets/bootlogo/logo.raw](assets/bootlogo/logo.raw).

## Notas úteis

- Saída de debug via serial (COM1). Consulte [docs/TEST_SCRIPTS.md](docs/TEST_SCRIPTS.md).
- O build atual gera uma imagem `1.44MB` FAT12 para compatibilidade de boot em [scripts/build_disk.sh](scripts/build_disk.sh).
- Em runtime, o disco de boot em modo floppy é acedido pelo kernel através de um BIOS disk thunk; volumes adicionais continuam centrados no caminho FAT16/ATA em [src/kernel/storage/disk.c](src/kernel/storage/disk.c).
- O drive `A:` é montado como volume whole-disk quando o meio de boot é uma floppy FAT válida.
- Usa [docs/DEVELOPMENT_PROTOCOL.md](docs/DEVELOPMENT_PROTOCOL.md) como contrato operacional para trabalho feito por agentes.
- Usa [docs/DEBUGGING.md](docs/DEBUGGING.md) para ciclos de diagnóstico e GDB.
- Próximos passos e decisões continuam em [docs/DESIGN.md](docs/DESIGN.md) e [docs/ROADMAP.md](docs/ROADMAP.md).

## Troubleshooting

Consulte [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md), [docs/DEBUGGING.md](docs/DEBUGGING.md) e [docs/FIXES.md](docs/FIXES.md).
