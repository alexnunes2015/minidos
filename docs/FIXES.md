# Correções Aplicadas ao MiniDOS

## Problema de Arranque

O sistema apresentava problemas de inicialização devido a três questões principais:

### 1. Endereço Incorreto do GDT (Global Descriptor Table)
**Problema**: O descritor da GDT estava usando `gdt_start + 0x7C00`, mas como o código usa `ORG 0x7C00`, o label `gdt_start` já inclui o offset 0x7C00. Isso resultava em um endereço duplicado incorreto.

**Solução**: Removido o `+ 0x7C00` extra e o `align 16` desnecessário.

```nasm
; ANTES (incorreto)
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start + 0x7C00

; DEPOIS (correto)
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
```

### 2. Kernel Não Estava Sendo Carregado do Local Correto
**Problema**: O bootloader tentava ler o kernel do setor 33, mas o `Makefile` usava `mcopy` para copiar o kernel para o sistema de arquivos FAT12, colocando-o em clusters aleatórios.

**Solução**: Modificado o `Makefile` para escrever o kernel diretamente no setor 33 usando `dd`:

```makefile
# ANTES
mcopy -i $@ $(BUILD_DIR)/kernel.bin ::KERNEL.BIN

# DEPOIS
dd if=$(BUILD_DIR)/kernel.bin of=$@ bs=512 seek=33 conv=notrunc
```

### 3. Jump para o Kernel Melhorado
**Problema**: O jump para o kernel usava um `jmp eax` indireto, que pode ter comportamento imprevisível.

**Solução**: Substituído por um `call` direto, o que é mais limpo e permite retorno (com halt) caso o kernel retorne:

```nasm
; ANTES
mov eax, 0x10000
jmp eax

; DEPOIS
call 0x10000
cli
hlt
```

## Como Testar

1. Compilar:
```bash
make clean && make
```

2. Executar no QEMU:
```bash
./scripts/run_test.sh
# ou
qemu-system-i386 -fda minidos.img -boot a
```

3. Esperar ver:
   - Mensagem "MiniDOS v0.1 loading..." com pontos
   - "OK"
   - Mensagem do kernel "MiniDOS v0.1 Kernel Started"
   - Prompt "A:>"

## Verificação Técnica

Para verificar se o kernel está no lugar certo:
```bash
dd if=minidos.img bs=512 skip=33 count=1 2>/dev/null | hexdump -C | head -5
```

Deve começar com bytes do kernel (não zeros).

Para verificar o endereço da GDT:
```bash
hexdump -C build/boot.bin | grep -A1 "17 00"
```

Deve mostrar `10 7d` (0x7D10 em little-endian).
