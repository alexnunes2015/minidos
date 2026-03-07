# Resolução de Problemas - MiniDOS

## Problema: Ecrã Preto no VirtualBox ao Arrancar como Disquete

### Sintoma
No VirtualBox, a VM parece arrancar, muda para um ecrã preto e não chega à shell. No QEMU, a mesma `minidos.img` pode arrancar normalmente.

### Causa
O objetivo do projeto é `floppy-first`, mas a implementação atual ainda não completou essa transição. O stage2 consegue arrancar via BIOS a partir de `DL=0x00`, mas depois o kernel entra em protected mode e passa a usar o backend ATA/IDE em `src/kernel/disk.c`.

Ao mesmo tempo, o build de `minidos.img` cria:
- um MBR;
- uma partição FAT16;
- o volume principal começando em LBA 2048.

Isto corresponde ao modelo transitório de disco usado hoje no QEMU, não ao modelo final desejado de uma disquete MS-DOS clássica.

### Como Confirmar
- Verifique no VirtualBox se a imagem está anexada só ao controlador `Floppy`.
- Veja em `src/kernel/kernel.c` que o kernel valida o disco principal com `disk_read_lba(0, ...)` logo após `disk_init()`.
- Veja em `src/kernel/disk.c` que o driver atual só fala ATA PIO (`0x1F0`, `0x170`) e não o controlador de floppy/FDC.

### Estado Atual Suportado
- Boot por BIOS/floppy: parcialmente possível no stage2.
- Disco principal em runtime: ainda dependente de ATA/IDE.
- Floppy como meio principal de leitura/escrita em runtime: objetivo do projeto, mas ainda não suportado de ponta a ponta.

### Se Quiseres Mesmo o Modelo "Disquete MS-DOS"
Será preciso uma mudança de arquitetura, não apenas de configuração da VM:
1. adicionar um backend de floppy/FDC ou um thunk BIOS para acesso a disco após o boot;
2. introduzir uma abstração de "boot media" no kernel em vez de assumir ATA;
3. provavelmente gerar uma imagem realmente compatível com floppy (por exemplo 1.44 MB/FAT12), se a intenção for emular uma disquete clássica de ponta a ponta.

### Prioridade de Produto
O alvo correto é:
1. floppy como meio principal por omissão;
2. HDD-style apenas mais tarde, quando existir instalador e esse fluxo passar a fazer sentido.

## Problema: Sistema Reiniciando Continuamente

### Causa Identificada
O sistema estava entrando em loop de reinicialização devido a problemas na criação da imagem do disco.

### Diagnóstico
O problema ocorria porque:
1. O `mformat` criava o sistema de arquivos FAT
2. Depois escrevíamos o bootloader com `dd`
3. O `mformat` com opção `-B` tentava preservar o BPB, mas havia incompatibilidades

### Solução Aplicada
Invertemos a ordem das operações no Makefile:

```makefile
# ORDEM CORRETA:
1. Criar imagem vazia
2. Formatar com mformat (cria FAT com bootloader genérico)
3. Copiar arquivos para FAT (mcopy)
4. Escrever kernel no setor 33
5. Sobrescrever bootloader completo (512 bytes)
```

Isso garante que:
- O BPB no bootloader é compatível com a estrutura FAT criada
- O código do bootloader sobrescreve o boot genérico do mformat
- O kernel está no setor correto (33)

### Melhorias Adicionadas
1. **Mensagem de debug**: Adicionado "Entering Protected Mode..." para identificar onde o boot para
2. **Teste direto de vídeo no kernel**: Primeiro comando escreve diretamente na memória de vídeo
3. **Ordem de build otimizada**: Garantias de que o BPB é preservado

### Como Testar

```bash
make clean && make
make run
```

### O Que Deve Aparecer

Se funcionar corretamente:
```
MiniDOS v0.1 loading................. OK
Entering Protected Mode...
KERNEL LOADED!
MiniDOS v0.1 Kernel Started
Welcome to your minimalist 16/32-bit OS.

MiniDOS Shell Ready.
Type 'help' for commands.
A:>
```

### Se Ainda Reiniciar

Execute passo a passo para diagnosticar:

```bash
# Verificar tamanho do bootloader
ls -lh build/boot.bin  # Deve ser exatamente 512 bytes

# Verificar assinatura de boot
hexdump -C minidos.img -n 512 | tail -1  # Deve terminar com 55 aa

# Verificar kernel no setor 33
dd if=minidos.img bs=512 skip=33 count=1 2>/dev/null | hexdump -C | head -5

# Testar com output serial (se disponível)
qemu-system-i386 -fda minidos.img -boot a -serial stdio
```

### Problemas Comuns

1. **Triple Fault**: Geralmente causado por GDT inválida ou jump incorreto
2. **Disco não lido**: BIOS INT 13h falhando - verificar parâmetros CHS
3. **Kernel não executa**: Verificar se está no setor 33 e se é código de 32-bit válido

### Debug Adicional

Para adicionar mais debug ao bootloader, edite `src/boot/boot.asm` e adicione mensagens antes de cada etapa crítica usando:

```nasm
mov si, msg_debug
call print_string
```

E defina a mensagem antes das assinaturas:
```nasm
msg_debug: db 'Debug Point X', 0x0D, 0x0A, 0
```
