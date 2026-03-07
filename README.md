# minidos

Sistema operacional educacional com bootloader, kernel 16/32-bit e shell básica. Inclui scripts de build e testes automatizados em QEMU.

## Estrutura

- [src/boot/](src/boot/) — bootloader (MBR + stage2).
- [src/kernel/](src/kernel/) — kernel, drivers e shell.
- [scripts/](scripts/) — criação de imagens de disco.
- [tests/](tests/) — testes em QEMU e utilitários.
- [docs/](docs/) — notas de design, fixes e troubleshooting.
- [assets/bootlogo/](assets/bootlogo/) — arte do logo de boot.

## Build e execução

```sh
make
make run
```

## Testes

```sh
make test
make test-ver
make test-dir
make test-drives
make test-serial
make phase0-check
```

Para rodar comandos personalizados:

```sh
./tests/test_runner.sh "ver" "drives"
```

## Dependências

- gcc (com suporte a -m32)
- ld
- nasm
- qemu-system-i386
- sudo (loop-mount) **ou** mtools (mformat, mcopy)

## Logo de boot (opcional)

Veja [assets/bootlogo/README.md](assets/bootlogo/README.md) para converter imagens e gerar [assets/bootlogo/logo.raw](assets/bootlogo/logo.raw).

## Notas úteis

- Saída de debug via serial (COM1). Consulte [docs/TEST_SCRIPTS.md](docs/TEST_SCRIPTS.md).
- Escrita em disco ATA PIO (LBA) está implementada em [src/kernel/disk.c](src/kernel/disk.c).
- Acesso ATA atualmente limitado ao disco primário master (`disk_id` 0).
- Direção do projeto: `floppy-first`. O comportamento desejado por omissão é arrancar e operar com a imagem como meio principal ao estilo disquete/MS-DOS.
- Estado atual da implementação: ainda existe uma lacuna entre essa meta e o runtime. O stage2 consegue arrancar via BIOS/floppy, mas o kernel em protected mode ainda depende do backend ATA/IDE em [src/kernel/disk.c](src/kernel/disk.c).
- O layout atual de `minidos.img` é transitório: o build ainda gera uma imagem com MBR e partição FAT16 em LBA 2048. Esse formato deve ser tratado como etapa intermédia até existir suporte floppy-native completo ou um instalador que passe a justificar um fluxo HDD-first.
- Em VirtualBox, montar `minidos.img` apenas como `Floppy` ainda pode falhar por causa dessa lacuna de implementação. Isso é uma limitação atual, não a direção desejada do projeto.
- Próximos passos e decisões em [docs/DESIGN.md](docs/DESIGN.md).

## Troubleshooting

Consulte [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) e [docs/FIXES.md](docs/FIXES.md).
