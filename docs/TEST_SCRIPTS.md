# MiniDOS Test Scripts

Permite enviar comandos automaticamente para o shell do MiniDOS e testar suas funcionalidades.

Os testes Python partilham o mesmo contrato de arranque e I/O através de `tests/qemu_harness.py`. Isso evita drift entre scripts e torna o comportamento mais previsível para agentes.
O boot normal entra diretamente na shell. Os harnesses ainda toleram uma GUI inicial se esse fluxo voltar a ser ativado, para evitar sleeps frágeis nos testes.
O `Makefile` agora gera dependências de headers (`.d`), por isso um `make` normal recompila automaticamente os objetos afetados quando a ABI interna do kernel muda.
As apps externas também podem ser divididas em vários `*.c` por diretório; o build de `STARTUI` compila todos os módulos irmãos em `external_apps/apps/win95_demo/`.

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

Se algum build voltar a arrancar `STARTUI` automaticamente, o harness fecha-a com `ESC` antes de continuar.
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
- instala `STARTUI.ELF` e os recursos companion em `A:\AIOS`;
- arranca o MiniDOS com QMP ativo;
- valida o marcador serial `[vid] fb=...w=...h=...p=...b=...` para garantir que o caminho gráfico inicializou com observabilidade suficiente;
- entra em `aios` e lança `startui` por teclado; se algum build já a tiver aberto, reutiliza a instância existente;
- espera `APPIN001`;
- espera `APPRET001` depois do clique ou do input de saída;
- injeta movimento relativo e clique esquerdo por `input-send-event`;
- rejeita `MOUSE201`–`MOUSE204`, que indicam que o kernel desativou o rato durante a inicialização;
- deriva a resolução ativa a partir do marcador `[vid] fb=...w=...h=...p=...b=...` e calcula o percurso do cursor a partir do centro do ecrã até ao botão `Fechar`, para o mesmo teste funcionar tanto no default `640x480` como nos fallbacks VESA maiores;
- rejeita qualquer debug extra da GUI na serial durante a execução;
- confirma que a app devolve controlo ao shell e que `ver` volta a ser aceite;
- move o rato dentro de `DOSSHELL` e `EDIT` e confirma que `q` / `ESC` continuam a sair das apps sem injetar teclas espúrias.

O teste assume que o cursor começa no centro do ecrã e move-se relativamente até ao botão `Fechar`, mas a geometria final é derivada da resolução realmente escolhida no boot.

**Uso:**
```bash
python3 tests/test_mouse_ui.py
```

**Atalho via Makefile:**
```bash
make test-mouse
```

Para validar o caminho negativo de inicialização:

```bash
make test-mouse-failure
```

Este alvo recompila o kernel com `MOUSE_TEST_FORCE_FAIL`, espera `MOUSE201` e `SHELL100`, e falha se o rato anunciar presença ou aceitar um pacote depois de ser desativado.

### 10. `tests/test_storage_failure.py` - Validação de degradação de storage
Valida o caminho de falha de storage onde `root_entries` inválido faz o kernel abortar com marcadores deterministas:
- copia o disco `minidos.img` para um temporário e zera os campos `root_entries` do BPB;
- arranca o MiniDOS contra essa imagem alterada;
- espera os marcadores `DISK021` e `STOP 0x00000004` para comprovar que o kernel detecta a ausência de um volume válido e entra numa parada controlada;
- o teste termina após o BSOD determinista, garantindo que não há reinvenção de `A:` nem scripts silenciosos.

**Uso:**
```bash
python3 tests/test_storage_failure.py
```

**Atalho via Makefile:**
```bash
make test-storage-failure
```

### 11. `tests/test_video_stress.py` - Teste de stress multithread da camada gráfica
Este script valida a nova infraestrutura multithread do vídeo:
- arranca o MiniDOS e aguarda a shell pronta
- envia o comando `videostress` ao shell com parâmetros configuráveis
- espera `PTVIDEO100` e uma mensagem `PTVIDEO200` por worker iniciado para confirmar arranque e conclusão
- aceita `PTVIDEO110` apenas quando os slots do scheduler limitam o número de workers
- garante que cada worker conclui as iterações e regista ticks para comparação

O contrato de conclusão é:

```text
PTVIDEO200 worker=0x........ ticks=0x........ iterations=0x........
```

Os três campos são valores hexadecimais de 32 bits, em maiúsculas e com oito dígitos. `ticks` mede a duração daquele worker; não existe um limite absoluto de performance neste teste porque a velocidade do host/QEMU varia.

**Uso:**
```bash
python3 tests/test_video_stress.py
```

**Notas:**
- não requer GUI
- dá visibilidade imediata nos logs serial dos tempos médios da pipeline gráfica

### 7. `tests/test_phase3.py` - Regressão crítica de disco/FAT16 (Fase 3)
Executa um cenário multi-disco/multi-partição crítico. O próprio teste:
- cria discos virtuais de dados;
- cria partições FAT16 com conteúdos distintos;
- mantém `minidos.img` no IDE `index=0` e liga o disco de dados gerado no `index=1` (as duas partições surgem como `B:` e `C:`);
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
- falhas fatais no bootstrap do scheduler passam a expor `STOP 0x00000006` ou `STOP 0x00000007` no serial antes do BSOD;
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

### 10. `tests/test_large_elf.py` - Regressão de ELF grande / slots dinâmicos
Valida que o loader já não está preso ao antigo slot user de 1 MiB:
- arranca a shell e entra em `A:\PTEST`;
- executa `PTBIG.ELF`, cuja `.bss` força um `PT_LOAD memsz` acima de 1 MiB;
- espera `PTBIG100` e `PTBIG190`, rejeitando `PTBIG900` / `PTBIG901`;
- exige `APPRET001` e confirma que o shell ainda aceita `ver` depois do retorno.

**Uso:**
```bash
python3 tests/test_large_elf.py
```

**Atalho via Makefile:**
```bash
make test-large-elf
```

### 11. `tests/test_multitask_elf.py` - Regressão de multitasking ELF
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

### 12. `tests/test_multitask_com.py` - Regressão de multitasking COM
Valida o runtime de `.COM` em background com threads-filho e kill por grupo:
- instala `CMCPU.COM`, `CMWAIT.COM` e `CMTHRD.COM` na imagem de teste;
- lança as três apps com `runbg` a partir da raiz de `A:`;
- espera o child thread da `CMTHRD` (`worker`) aparecer com `APPTH100`;
- confirma no `top` os PIDs, nomes e `EXE` com origem `.COM`;
- valida `kill <pid>` sobre a `CMTHRD` e confirma que líder + child desaparecem juntos;
- mata os restantes jobs e confirma que o shell ainda aceita `ver`.

**Uso:**
```bash
python3 tests/test_multitask_com.py
```

**Atalho via Makefile:**
```bash
make test-multitask-com
```

### 13. `tests/test_scheduler_fairness.py` - Regressão de fairness do scheduler

Executa duas rodadas com três cargas concorrentes em `PTEST`: uma app CPU-bound (`PTCPU`), uma app bloqueante por eventos (`PTWAIT`) e uma app com child thread (`PTTHRD`). O teste exige os marcadores de início e conclusão de todas as cargas dentro do mesmo prazo, confirma que o grupo termina sem tarefas residuais em `top` e valida que a shell continua responsiva.

**Uso:**
```bash
python3 tests/test_scheduler_fairness.py
```

**Atalho via Makefile:**
```bash
make test-scheduler-fairness
```

### 14. `tests/test_syscall_matrix.py` - Matriz de erros de syscall

Instala uma fixture em ELF e `.COM` que chama syscalls com ponteiros fora da janela user, ranges inválidos, formato gráfico não suportado, batch acima do limite e número de syscall desconhecido. Cada caso exige a resposta de erro prevista, sem page fault nem alteração de controlo, e depois confirma `ver` no shell.

**Uso:**
```bash
python3 tests/test_syscall_matrix.py
```

**Atalho via Makefile:**
```bash
make test-syscall-matrix
```

### 15. `tests/test_user_isolation.py` - Regressão de isolamento kernel/user
Valida o contrato mínimo de isolamento para apps ELF e `.COM` em ring3:
- instala `BADPTR.ELF`, `OLDMAP.ELF`, `USRFAULT.ELF`, `BADCOM.COM`, `OLDCOM.COM` e `USRFCOM.COM` na imagem;
- força `A:` e confirma listagem via `elfls`;
- executa `BADPTR` e `run BADCOM`, exigindo `BADP190`, para provar que um ponteiro de kernel foi rejeitado no `int 0x80` nos dois formatos;
- executa `OLDMAP` e `run OLDCOM`, exigindo `[paging] #PF detected`, `CR2=0x00200000`, `mode=user` e `APPFLT900`, para provar que a antiga janela virtual `0x00200000` deixou de estar acessível em ring3;
- executa `USRFAULT` e `USRFCOM`, exigindo `[paging] #PF detected`, `CR2=0x00010000`, `mode=user` e `APPFLT900`;
- confirma `APPRET001` e que o shell ainda aceita `ver` depois dos seis cenários negativos.

**Uso:**
```bash
python3 tests/test_user_isolation.py
```

**Atalho via Makefile:**
```bash
make test-user-isolation
```

### 16. `tests/test_user_cleanup.py` - Cleanup após fault de userland

Valida sete faults consecutivos em ELF e `.COM`, verificando que cada execução reutiliza o mesmo PID, base física e tamanho de slot. Também lança uma fixture que cria um child thread e faulta o líder, confirmando que o grupo inteiro deixa de aparecer no `top` e que o slot volta a ser reutilizável.

**Uso:**

```bash
make test-user-cleanup
```

### 17. `tests/test_graphics_perf.py` - Regressão de performance gráfica
Valida as optimizações de performance do subsistema de vídeo:
- **stride-align**: confirma que o kernel arranca e atinge o main loop sem erros de vídeo (o stride cacheline-aligned é um invariante de compilação para as resoluções standard).
- **stress-render**: envia `videostress 2 60`, exige dois `PTVIDEO200` completos com workers distintos e `iterations=0x0000003C`, e confirma que ambos terminam sem deadlock nem starvation.
- **shell-responsive**: após receber os dois marcadores completos, envia `ver` e confirma que o shell ainda responde — valida que o lock de vídeo não ficou preso.

O campo `ticks` é recolhido como métrica determinística do kernel, mas não é comparado a um teto fixo. O teste mede completude/schema e mantém os números disponíveis para comparações em ambientes equivalentes.

**Uso:**
```bash
python3 tests/test_graphics_perf.py
python3 tests/test_graphics_perf.py --test stride-align
python3 tests/test_graphics_perf.py --test stress-render
```

**Atalho via Makefile:**
```bash
make test-graphics
```

**Build flags de debug:**
- `DIRTY_RECT_DEBUG`: activa log serial de cada chamada a `video_note_dirty_locked` com coordenadas do rect marcado.

### `tests/test_ui_runtime.py` - Contratos host da runtime gráfica

Compila um harness C no host, sem arrancar QEMU, para validar o contrato entre `minidos_ui.h` e a ABI de aplicações:

- filas de 1..64 rectângulos, ordem, flush antes de operações gráficas sensíveis e fallback para syscall 8 quando syscall 35 falha;
- layout dos pixels packed little-endian, seleção explícita dos formatos de surface e fallback de linhas de listview para runs clipped quando o blit é rejeitado;
- uma syscall 36 por texto clipped genérico, incluindo o flag de fundo transparente e fallback para runs;
- decode BMP, ordem bottom-up, clips, wallpaper cache, fallback de ficheiro ausente e caminho de runs disponível quando a surface packed é rejeitada.

**Uso:**

```bash
python3 tests/test_ui_runtime.py
```

**Atalho via Makefile:**

```bash
make test-ui-runtime
```

### 15. `make verify-image` - Verificação estrutural da imagem
Valida a imagem `minidos.img` fora do runtime:
- tamanho da imagem (`128MB`);
- BPB FAT16;
- assinatura de boot;
- `kernel_sectors` patchado em `stage2.bin`;
- presença dos ficheiros esperados no volume FAT.

**Uso:**
```bash
make verify-image
```

### `make ci` - Gate Tier 3

Mudanças cross-cutting ou prontas para release executam o gate completo definido em `docs/DEVELOPMENT_PROTOCOL.md`:

```bash
make ci
```

Para mudanças gráficas, execute também `make test-ui-runtime`, `make test-graphics` e `make test-mouse`. O primeiro já integra `make ci`, mas os dois testes QEMU focados ainda não fazem parte desse alvo.

## Características

✅ **Captura de Output** - Todos os comandos capturam a saída completa
✅ **Timeout** - Proteção contra hang infinito (10-15 segundos)
✅ **Serial Debug** - Via COM1 a 38400 baud
✅ **Boot Logo** - O splash entra cedo no kernel; antes do logo fica preto com cursor a piscar, e depois anima o logo durante 5 segundos
✅ **Mouse IRQ12** - Cobertura QMP para movimento/clique na demo gráfica
✅ **Phase 3 Stress** - Cobertura de multi-disco/multi-volume com validações negativas
✅ **Phase 5 Guard Pages** - Cobertura positiva e negativa do runtime de scheduler com `#PF` controlado na guard page
✅ **User Isolation** - Cobertura de ring3 para ponteiros inválidos em syscall e faults de user mode com retorno ao shell
✅ **Large ELF Runtime** - Cobertura de slot user dinâmico para imagens ELF com `PT_LOAD memsz` acima de 1 MiB
✅ **Legacy COM Runtime** - Cobertura de `.COM` no mesmo runtime preemptivo de foreground/background usado por ELFs
✅ **Graphics ABI** - Cobertura host de batching/fallback, formatos packed e texto clipped, mais stress/GUI em QEMU
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
- `BOOT300` - Modo degradado: storage indisponível para processar `AUTOEXEC.AUT`, mas o shell continua a arrancar
- `[Stage2] Loading kernel...` - Kernel sendo carregado
- `[Stage2] Kernel loaded` - Kernel pronto
- `[Stage2] Entering PM...` - Transição para modo protegido
- `[kbd] scan set 1` / `[kbd] scan set 2` - Decoder do teclado alinhado com o modo do controlador PS/2 (set 1 = translation on, set 2 = translation off)
- `KBD200` - Modo degradado: uma tecla foi consumida pelo fallback de polling; `make test-keyboard` deve falhar se este marker aparecer
- `[mouse] PS/2 mouse enabled on IRQ12` - Porta auxiliar PS/2 ativa e reporting ligado
- `MOUSE201` / `MOUSE202` / `MOUSE203` / `MOUSE204` - Falha observável de inicialização do rato; `make test-mouse` deve rejeitar estes markers
- `[mouse] first packet received` - Primeiro pacote de rato observado em runtime
- `APPIN001` / `APPRET001` - Contrato serial de entrada e retorno de apps interativas
- `DISK021` - Nenhum volume de boot/partição válido foi detetado; o shell continua sem inventar `A:`
- `APPFLT900` - Fault de userland contido à app/grupo atual
- `PTBIG100` / `PTBIG190` / `PTBIG900` / `PTBIG901` - Regressão do loader ELF grande com slot user dinâmico
- `PTVIDEO100 start workers=<w> iterations=<i> started=<s>` - Pedido de stress gráfico e quantidade de workers iniciados
- `PTVIDEO110 limited by scheduler slots` - Scheduler não tinha slots para todos os workers pedidos
- `PTVIDEO200 worker=0x........ ticks=0x........ iterations=0x........` - Métrica hexadecimal de conclusão emitida uma vez por worker gráfico
- `[int] IDT active, PIC remapped, IRQ0/IRQ1/IRQ12 enabled` - Caminho de interrupções ativo
- `SCHED100` / `SCHED110` / `SCHED120` / `SCHED190` - Bootstrap e self-test positivo do scheduler/runtime
- `SCHED150` / `SCHED900` - Armamento do teste negativo e fault de guard page identificado
- `STOP 0x00000006` / `STOP 0x00000007` - Hard-fail auditavel do bootstrap do scheduler antes da shell
- `[sched] phase5 context-switch self-test OK` - Self-test de scheduler concluído
- `[paging] init` / `[paging] enabled` - Sequência de ativação de paging
- `paging self-test OK` - Self-test de mapeamento concluído
- `[paging] #PF detected` + `CR2=...` - Diagnóstico de page fault (teste negativo)
- `MiniDOS Shell Ready.` - Shell initialized and printed the ready prompt message.
- `[INFO][kernel] Entering main loop` - Scheduler-enabled interactive loop is now active.
- `SHELL100` - Deterministic marker that signals the shell is ready for scripted command injection.

**Nota:** O `stage2` já avança a janela `ES` ao carregar kernels acima de `64 KiB`, por isso o `kernel_sectors` pode ultrapassar `128` sem corromper o payload carregado.

**Nota:** Entre `BOOT100` e `BOOT110`, o placeholder visual é um ecrã preto com cursor a piscar. Depois de `BOOT110`, o logo passa a ocupar o ecrã e o shell só toma controlo após 5 segundos dessa janela visual. Compare `BOOT100`, `BOOT110`, `Initializing disk driver...`, `Detecting drives and partitions...`, `BOOT190` e `BOOT300` para separar tempo real de I/O da espera visual final e distinguir um boot normal de um arranque em modo degradado sem `AUTOEXEC.AUT`.

**Nota:** `ps` e `top` reportam tarefas visíveis ao scheduler. O campo `mem` ainda significa apenas reserva de stack do kernel mais guard page; não é RSS real e ainda não inclui o span user dinâmico mapeado para apps em ring3.

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
