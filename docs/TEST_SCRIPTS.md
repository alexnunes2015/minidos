# MiniDOS Test Scripts

Permite enviar comandos automaticamente para o shell do MiniDOS e testar suas funcionalidades.

Os testes Python partilham o mesmo contrato de arranque e I/O através de `tests/qemu_harness.py`. Isso evita drift entre scripts e torna o comportamento mais previsível para agentes.

## Scripts Disponíveis

### 1. `tests/test_runner.sh` - Test Runner Flexível
Permite enviar qualquer sequência de comandos para o shell.

**Uso:**
```bash
./tests/test_runner.sh "cmd1" "cmd2" "cmd3"
```

**Exemplos:**
```bash
# Testar comando help
./tests/test_runner.sh "help"

# Testar múltiplos comandos
./tests/test_runner.sh "ver" "drives" "help"

# Navegar e listar diretórios
./tests/test_runner.sh "c:" "dir"

# Ver conteúdo de arquivo
./tests/test_runner.sh "c:" "type hello.txt"
```

### 2. `tests/test.sh` - Testes Pré-definidos
Oferece testes pré-configurados para funcionalidades comuns.

**Uso:**
```bash
./tests/test.sh [test_name]
```

**Testes Disponíveis:**
- `help` - Testa comando help
- `ver` - Testa versão do sistema
- `drives` - Testa detecção de drives
- `dir` - Testa listagem de diretório
- `type` - Testa visualização de arquivo
- `shell` - Testa múltiplos comandos

**Exemplos:**
```bash
./tests/test.sh help
./tests/test.sh drives
./tests/test.sh dir
```

### 3. `tests/test_auto.sh` - Script Automático Simples
Alternativa mais simples para testes rápidos.

```bash
./tests/test_auto.sh "comando"
```

### 4. `tests/test_paging.py` - Validação de Paging
Valida o boot com paging ativo e o teste negativo de `#PF`.

**Uso:**
```bash
python3 tests/test_paging.py
python3 tests/test_paging.py --expect-fault
```

**Atalho via Makefile:**
```bash
make test-paging
```

### 5. `tests/test_keyboard_irq.py` - Validação de teclado por IRQ1
Valida entrada de teclado real via IRQ1 (sem enviar comando pela serial), injetando teclas pelo monitor QMP (`sendkey`) e confirmando que o comando chega ao shell sem duplicação.

**Uso:**
```bash
python3 tests/test_keyboard_irq.py
```

**Atalho via Makefile:**
```bash
make test-keyboard
```

### 6. `tests/test_phase3.py` - Regressão crítica de disco/FAT16 (Fase 3)
Executa um cenário multi-disco/multi-partição crítico. O próprio teste:
- cria discos virtuais de dados;
- cria partições FAT16 com conteúdos distintos;
- faz attach de vários discos IDE no QEMU;
- valida enumeração de múltiplas letras de drive;
- valida operações de gestão de ficheiros e isolamento entre volumes.

**Uso:**
```bash
python3 tests/test_phase3.py
```

**Atalho via Makefile:**
```bash
make test-phase3
```

**Dependências adicionais do host:**
- `sfdisk`
- `mtools` (`mformat`, `mcopy`)

### 7. `make verify-image` - Verificação estrutural da imagem
Valida a imagem `minidos.img` fora do runtime:
- tamanho da floppy (`1.44MB`);
- BPB FAT12;
- assinatura de boot;
- `kernel_sectors` patchado em `stage2.bin`;
- presença dos ficheiros esperados no volume FAT.

**Uso:**
```bash
make verify-image
```

## Características

✅ **Captura de Output** - Todos os comandos capturam a saída completa
✅ **Timeout** - Proteção contra hang infinito (10-15 segundos)
✅ **Serial Debug** - Via COM1 a 38400 baud
✅ **Boot Logo** - Mostra boot logo VGA Mode 13h antes do shell
✅ **Phase 3 Stress** - Cobertura de multi-disco/multi-volume com validações negativas
✅ **Shared Harness** - Arranque/QEMU/timeouts centralizados em `tests/qemu_harness.py`

## Exemplo de Teste Completo

```bash
$ ./tests/test_runner.sh "ver" "drives" "c:" "dir"

╔════════════════════════════════════════╗
║      MiniDOS Automated Test Suite      ║
╚════════════════════════════════════════╝

Commands to execute:
  • ver
  • drives
  • c:
  • dir

Starting MiniDOS...
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

[Stage2] Started
[Stage2] Loading kernel...
[Stage2] Kernel loaded
[Stage2] Entering PM...

MiniDOS v0.1
...

Test completed!
```

## Comandos Suportados

- `help` - Mostra ajuda
- `ver` - Mostra versão
- `cls` - Limpa tela
- `drives` - Lista drives
- `dir` - Lista diretório
- `type <file>` - Mostra conteúdo de arquivo
- `c:`, `d:`, `e:` - Muda de drive
- `mkdir`, `rmdir`, `copy`, `ren`, `del` - Gestão de ficheiros/diretórios FAT16

## Saída de Debug Serial

Todos os testes mostram a saída serial que inclui:
- `[Stage2] Started` - Stage2 bootloader carregado
- `[Stage2] Displaying boot logo...` / `[Stage2] Logo displayed` - Sequência de splash no stage2
- `[Stage2] Loading kernel...` - Kernel sendo carregado
- `[Stage2] Kernel loaded` - Kernel pronto
- `[Stage2] Entering PM...` - Transição para modo protegido
- `[int] IDT active, PIC remapped, IRQ0/IRQ1 enabled` - Caminho de interrupções ativo
- `[sched] phase5 context-switch self-test OK` - Self-test de scheduler concluído
- `[paging] init` / `[paging] enabled` - Sequência de ativação de paging
- `paging self-test OK` - Self-test de mapeamento concluído
- `[paging] #PF detected` + `CR2=...` - Diagnóstico de page fault (teste negativo)

**Nota:** O kernel está executando após a transição para Protected Mode nos testes seriais atuais. Se houver regressão, use os checkpoints PM no serial (`CLI`, `LGDT`, `CR0.PE`, `Before far jump`) para localizar onde o boot para.

## Troubleshooting

Se os scripts não funcionar:
1. Verificar que `minidos.img` existe: `ls -lh minidos.img`
2. Executar `make verify-image`
3. Executar `make clean && make` para recompilação
4. Executar `make phase0-check` para confirmar o baseline
5. Verificar permissões: `chmod +x tests/test*.sh`
6. Verificar se QEMU está instalado: `which qemu-system-i386`
7. Para `test_phase3.py`, verificar `sfdisk`, `mformat` e `mcopy` no `PATH`
8. Para debug detalhado, consultar [docs/DEBUGGING.md](docs/DEBUGGING.md)
