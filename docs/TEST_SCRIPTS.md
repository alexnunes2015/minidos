# MiniDOS Test Scripts

Permite enviar comandos automaticamente para o shell do MiniDOS e testar suas funcionalidades.

Os testes Python partilham o mesmo contrato de arranque e I/O através de `tests/qemu_harness.py`. Isso evita drift entre scripts e torna o comportamento mais previsível para agentes.
O boot normal entra diretamente na shell. Os harnesses ainda toleram uma GUI inicial se esse fluxo voltar a ser ativado, para evitar sleeps frágeis nos testes.
O `Makefile` agora gera dependências de headers (`.d`), por isso um `make` normal recompila automaticamente os objetos afetados quando a ABI interna do kernel muda.

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
Valida entrada de teclado real via IRQ1 (sem enviar comando pela serial), injetando teclas pelo monitor QMP (`input-send-event`, com fallback para `sendkey`) e confirmando que:
- o shell aceita um comando inteiro por teclado;
- `DOSSHELL` recebe teclado e devolve controlo ao shell;
- `EDIT` recebe `ESC` por teclado e devolve controlo ao shell.

Se algum build voltar a arrancar `WIN95UI` automaticamente, o harness fecha-a com `ESC` antes de continuar.
O harness espera pelo marcador serial `APPIN001` antes de enviar teclas para apps gráficas, para não confundir latência de arranque com falha de teclado.
Depois espera `APPRET001` antes de validar que o shell retomou controlo, eliminando sleeps fixos no caminho de saída das apps.

**Uso:**
```bash
python3 tests/test_keyboard_irq.py
```

**Atalho via Makefile:**
```bash
make test-keyboard
```

### 6. `tests/test_mouse_ui.py` - Validação de rato PS/2 / IRQ12
Valida o caminho completo de rato para GUI:
- instala `WIN95UI.ELF` na imagem;
- arranca o MiniDOS com QMP ativo;
- lança `WIN95UI` por teclado; se algum build já a tiver aberto, reutiliza a instância existente;
- espera `APPIN001`;
- espera `APPRET001` depois do clique ou do input de saída;
- injeta movimento relativo e clique esquerdo por `input-send-event`;
- rejeita qualquer debug `[win95ui]` na serial durante a execução da GUI;
- confirma que a app devolve controlo ao shell e que `ver` volta a ser aceite;
- move o rato dentro de `DOSSHELL` e `EDIT` e confirma que `q` / `ESC` continuam a sair das apps sem injetar teclas espúrias.

O teste assume que o cursor começa no centro do ecrã e move-se relativamente até ao botão `Cancel`, o que evita depender de resolução fixa.

**Uso:**
```bash
python3 tests/test_mouse_ui.py
```

**Atalho via Makefile:**
```bash
make test-mouse
```

### 7. `tests/test_phase3.py` - Regressão crítica de disco/FAT16 (Fase 3)
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

### 8. `tests/test_phase4.py` - Regressão de apps ELF (Fase 4)
Valida o caminho `shell -> ELF -> retorno ao shell` com as apps externas de exemplo e com a app de stress:
- instala `HELLOELF.ELF`, `STATELF.ELF` e `STRESS.ELF` na imagem;
- espera o shell e tolera uma eventual GUI inicial antes de começar os comandos ELF;
- confirma listagem via `elfls`;
- executa `hello_elf`, `stat_elf` e `stress`;
- espera `STRS190` da app `STRESS` antes de exigir `APPRET001`;
- a app `STRESS` faz churn de listagem, leituras repetidas de ELF e `gfx_present`, sem assumir escrita estável no volume de boot;
- confirma que o shell volta a aceitar `ver` depois das execuções;
- mantém a cobertura de retorno ao shell para `DOSSHELL` e `EDIT`.

**Uso:**
```bash
python3 tests/test_phase4.py
```

**Atalho via Makefile:**
```bash
make test-phase4
```

### 9. `tests/test_phase5.py` - Regressão de scheduler/runtime (Fase 5)
Valida o runtime real do scheduler e a proteção de memória por guard page:
- confirma o boot do scheduler com `SCHED100`, `SCHED110`, `SCHED120` e `SCHED190`;
- exige que o self-test da fase termine antes do shell entrar no loop principal;
- no modo negativo (`--expect-guard`), recompila com `SCHED_TEST_GUARD` e espera `SCHED150`, `SCHED900` e `[paging] #PF detected`;
- garante que overflow/fuga para a guard page vira falha observável, em vez de corrupção silenciosa de stack.

**Uso:**
```bash
python3 tests/test_phase5.py
python3 tests/test_phase5.py --expect-guard
```

**Atalho via Makefile:**
```bash
make test-phase5
```

### 10. `make verify-image` - Verificação estrutural da imagem
### 10. `tests/test_multitask_elf.py` - Regressão de multitasking ELF
Valida o runtime de ELFs em background com threads-filho e kill por grupo:
- arranca a shell e entra em `A:\PTEST`;
- lança `PTCPU`, `PTWAIT` e `PTTHRD` com `runbg`;
- espera o child thread da `PTTHRD` (`worker`) aparecer com `APPTH100`;
- confirma no `top` os PIDs, nomes e `EXE` dos leaders e do child thread;
- valida `kill <pid>` sobre a `PTTHRD` e confirma que líder + child desaparecem juntos;
- mata os restantes jobs e confirma que o shell ainda aceita `ver`.

**Uso:**
```bash
python3 tests/test_multitask_elf.py
```

**Atalho via Makefile:**
```bash
make test-multitask
```

### 11. `make verify-image` - Verificação estrutural da imagem
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
✅ **Boot Logo** - O splash entra cedo no kernel; antes do logo fica preto com cursor a piscar, e depois anima o logo durante 5 segundos
✅ **Mouse IRQ12** - Cobertura QMP para movimento/clique na demo gráfica
✅ **Phase 3 Stress** - Cobertura de multi-disco/multi-volume com validações negativas
✅ **Phase 5 Guard Pages** - Cobertura positiva e negativa do runtime de scheduler com `#PF` controlado na guard page
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
- `ps` - Snapshot do scheduler com estado, ticks e reserva de stack do kernel
- `top [ms] [n]` - Amostra `%cpu` por ticks do scheduler e mostra a mesma reserva de stack
- `dir` - Lista diretório
- `type <file>` - Mostra conteúdo de arquivo
- `c:`, `d:`, `e:` - Muda de drive
- `mkdir`, `rmdir`, `copy`, `ren`, `del` - Gestão de ficheiros/diretórios FAT16

## Saída de Debug Serial

Todos os testes mostram a saída serial que inclui:
- `[Stage2] Started` - Stage2 bootloader carregado
- `BOOT100` - Splash do kernel ativado
- `BOOT110` - `BOOTLOGO.DAT` / `BOOTLOGO.PAL` carregados com sucesso
- `BOOT190` - Splash fechado e shell prestes a assumir o ecrã
- `[Stage2] Loading kernel...` - Kernel sendo carregado
- `[Stage2] Kernel loaded` - Kernel pronto
- `[Stage2] Entering PM...` - Transição para modo protegido
- `[kbd] scan set 1 selected (translation on)` / `[kbd] scan set 2 selected (translation off)` - Decoder do teclado alinhado com o modo do controlador PS/2
- `[mouse] PS/2 mouse enabled on IRQ12` - Porta auxiliar PS/2 ativa e reporting ligado
- `[mouse] first packet received` - Primeiro pacote de rato observado em runtime
- `APPIN001` / `APPRET001` - Contrato serial de entrada e retorno de apps interativas
- `[int] IDT active, PIC remapped, IRQ0/IRQ1/IRQ12 enabled` - Caminho de interrupções ativo
- `SCHED100` / `SCHED110` / `SCHED120` / `SCHED190` - Bootstrap e self-test positivo do scheduler/runtime
- `SCHED150` / `SCHED900` - Armamento do teste negativo e fault de guard page identificado
- `[sched] phase5 context-switch self-test OK` - Self-test de scheduler concluído
- `[paging] init` / `[paging] enabled` - Sequência de ativação de paging
- `paging self-test OK` - Self-test de mapeamento concluído
- `[paging] #PF detected` + `CR2=...` - Diagnóstico de page fault (teste negativo)

**Nota:** O `stage2` já avança a janela `ES` ao carregar kernels acima de `64 KiB`, por isso o `kernel_sectors` pode ultrapassar `128` sem corromper o payload carregado.

**Nota:** Entre `BOOT100` e `BOOT110`, o placeholder visual é um ecrã preto com cursor a piscar. Depois de `BOOT110`, o logo passa a ocupar o ecrã e o shell só toma controlo após 5 segundos dessa janela visual. Compare `BOOT100`, `BOOT110`, `Initializing disk driver...`, `Detecting drives and partitions...` e `BOOT190` para separar tempo real de I/O da espera visual final.

**Nota:** `ps` e `top` reportam tarefas visíveis ao scheduler. O campo `mem` ainda significa apenas reserva de stack do kernel mais guard page; não é RSS real por processo, porque o MiniDOS ainda não tem ring3 nem address spaces isolados por processo.

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
