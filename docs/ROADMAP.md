# MiniDOS Roadmap

Este roadmap organiza os próximos passos do S.O. em fases incrementais, com foco em estabilidade, memória, I/O e execução de apps ELF.

Nota sobre status:
- `concluída` significa que o contrato mínimo da fase foi fechado com validação suficiente para a época.
- `baseline entregue` significa que existe base técnica ou protótipo validado, mas a fase continua aberta para hardening e cobertura real.

## Fase 0 - Baseline Estável (1 semana)

Status: concluída em 14/02/2026.

Objetivo: garantir que cada mudança parte de um estado reproduzível.

- Padronizar validação local com `make clean && make && make test-serial`.
- Evitar falso positivo de artefatos antigos no `build/`.
- Reduzir warnings críticos de acesso a endereços físicos fixos (`kernel.c`, `video.c`), mantendo comportamento atual.

Critério de pronto:
- Build limpo e teste serial passando de forma consistente em execuções repetidas.
- Logs seriais com sequência de boot e shell sem regressão.

Entregas concluídas:
- Novo alvo `make phase0-check` para executar baseline completo (`clean`, `build`, `test-serial`).
- `test-serial` atualizado para depender de `minidos.img`, reduzindo risco de artefato antigo.
- Leituras de memória física fixa ajustadas em `src/kernel/core/kernel.c` e `src/kernel/video/video.c` para eliminar warnings críticos de build.

## Fase 1 - Memória e Paging Mínimo (1 a 2 semanas)

Status: concluída em 14/02/2026.

Objetivo: introduzir proteção de memória sem quebrar o boot.

Plano de execução:
1. Implementar base de paging (`paging.h/.c`) com estruturas e flags.
2. Criar identity mapping mínimo para regiões críticas de boot/kernel.
3. Integrar sequência de enable (`CR3` + `CR0.PG`) com logs seriais.
4. Instalar handler de `#PF` com diagnóstico (`CR2`, error code).
5. Automatizar testes com `tests/test_paging.py` e alvo `make test-paging`.

Checklist de implementação:
- [x] `paging.h` e `paging.c` criados e integrados no build.
- [x] Page directory/tables alinhados em 4KB.
- [x] Identity map cobrindo kernel, vídeo, boot metadata e stack.
- [x] Ativação de paging após init de serial/log.
- [x] Handler de `#PF` ativo antes de `CR0.PG=1`.

Checklist de documentação:
- [x] Plano detalhado em `docs/PAGING.md`.
- [x] `docs/DESIGN.md` atualizado para "paging ativo" após merge técnico.
- [x] Notas de debug serial de paging adicionadas em `docs/TEST_SCRIPTS.md` (se necessário).

Checklist de testes:
- [x] `make phase0-check` verde antes de ativar paging.
- [x] `make test-serial` verde com paging ativo.
- [x] Self-test de mapeamento com log `paging self-test OK`.
- [x] Teste negativo de `#PF` com log de `CR2`.
- [x] Regressão de shell (`ver`, `drives`, `dir`) mantida.
- [x] `make test-paging` verde em build limpo.

Critério de pronto:
- Sistema sobe com paging ativo.
- Fault intencional controlado gera log de `#PF` em vez de travar silenciosamente.
- Documentação e testes automatizados atualizados para o estado final.

Referência de execução detalhada:
- `docs/PAGING.md`

## Fase 2 - Interrupções e Robustez de Kernel (1 a 2 semanas)

Status: concluída em 15/02/2026.

Objetivo: reduzir dependência de polling e melhorar resiliência.

- Consolidar IDT/ISR como caminho padrão de exceções.
- Migrar teclado para IRQ1 com fallback temporário por polling durante transição.
- Definir política de panic com dump mínimo de contexto no serial.

Critério de pronto:
- Entrada por teclado funcional via IRQ.
- Exceções comuns registradas com diagnóstico útil.

## Fase 3 - Disco e FAT16 com Escrita Confiável (1 a 2 semanas)

Objetivo: evoluir de MVP de disco para base utilizável.

Status: concluída em 28/02/2026.

- Expandir ATA além de `disk_id` 0.
- Validar leitura/escrita em múltiplos discos/canais suportados.
- Criar testes automatizados para operações FAT16 de escrita:
- Criar arquivo.
- Atualizar conteúdo.
- Remover arquivo/diretório.

Entregas iniciais:
- Driver ATA PIO atualizado para `disk_id` 0..3 (primário/segundário, master/slave) em `src/kernel/storage/disk.c`.
- Novo teste de regressão da fase (`tests/test_phase3.py`) com criação dinâmica de múltiplos discos/partições e attach automático no QEMU.
- Novo alvo `make test-phase3`.

Checklist crítico de validação (Fase 3):
- [x] Teste cria discos virtuais de dados durante a execução (sem depender de imagem secundária pré-pronta).
- [x] Teste valida enumeração de múltiplas letras de drive oriundas de múltiplas partições (`A:`..`C:` no cenário atual, com negativo em `D:`).
- [x] Teste executa operações de escrita FAT16 em mais de um volume (`copy`, `ren`, `mkdir`, `rmdir`, `del`).
- [x] Teste verifica isolamento entre volumes (arquivo criado em um drive não aparece em outro).
- [x] Teste cobre caso negativo de operação cross-drive inválida e drive inexistente.

Critério de pronto:
- Testes de escrita e leitura passando em sequência, sem corrupção detectável.

Validação de fechamento:
- `make test-phase3` verde em 28/02/2026, cobrindo multi-disco, isolamento entre volumes, operações FAT16 de escrita (`copy`, `ren`, `mkdir`, `rmdir`, `del`) e cenários negativos (cross-drive inválido e drive inexistente).

## Fase 4 - Userland e Execução ELF (2 semanas)

Objetivo: executar apps externas com contrato mínimo estável.

Status: concluída em 15/02/2026.

- Fechar ABI de syscalls (conjunto mínimo para I/O, arquivo e saída de processo).
- Integrar fluxo completo de build e carga ELF usando `external_apps/`.
- Adicionar comandos de shell para listar e executar binários ELF.

Entregas concluídas:
- ABI mínima para apps externas via `minidos_app_api_t.syscall` (`puts`, `get_char`, `file_size`).
- Loader ELF32 no shell (`PT_LOAD`) com validações básicas de cabeçalho e segmentos.
- Execução de apps ELF por comando direto (`hello_elf`) e por `run <app>`, com retorno ao kernel.
- Comando `elfls` para listar apps ELF no diretório atual.
- Fluxo `external_apps/add_app.sh` atualizado para instalar `.ELF`.
- Novo teste de regressão da fase: `tests/test_phase4.py` + alvo `make test-phase4`.

Critério de pronto:
- `hello_elf` e `stat_elf` executam pelo shell com retorno controlado ao kernel.

## Fase 5 - Preparação para Multitarefa (concluida)

Objetivo: fechar a transição de um baseline de kernel threads para um runtime com multitarefa preemptiva, ring3 e isolamento básico kernel/user.

Status: concluida em 19/03/2026.

Entregas finais:
- Estrutura de processo (`process_t`) com PID, estado, contexto salvo, stacks de kernel dedicadas e stack de user por app.
- Runtime do scheduler com bootstrap + `idle`, round-robin real por IRQ0, `sleep` por ticks e `yield` por software interrupt.
- Guard page não mapeada por stack de kernel, mais regressão negativa (`SCHED_TEST_GUARD`).
- Entrada real em ring3 para apps ELF e `.COM`, com TSS `esp0`, descritores user na GDT, `int 0x80` para syscalls e page directories por app.
- Foreground e background de apps ELF/COM unificados no scheduler, inclusive child threads de userland.
- Falhas de user mode agora terminam apenas a app/grupo ofensora, com retorno controlado ao shell.
- Regressão automatizada adicional de isolamento (`tests/test_user_isolation.py` / `make test-user-isolation`) cobrindo ponteiros inválidos em syscall e `#PF` de user mode em ambos os formatos.

Baseline anterior entregue:
- Estrutura de processo adicionada (`process_t`) com PID, estado e contexto mínimo salvo (`ESP`).
- Runtime inicial do scheduler agora inclui thread de bootstrap + idle thread e criação de kernel threads sobre frames reais de retorno de IRQ.
- Timer PIT configurado e IRQ0 habilitado para base de round-robin (coleta de ticks em `scheduler_on_timer_tick`).
- Quantum de preempção conectado ao retorno de IRQ0 e `yield` voluntário por software interrupt (`int 0x81`).
- Kernel threads do scheduler usam stacks dedicadas numa arena reservada, cada uma protegida por uma guard page não mapeada.
- Sleep/bloqueio por ticks integrado ao scheduler para permitir wakeup determinístico por IRQ0.
- Regressão automatizada da fase adicionada (`tests/test_phase5.py` / `make test-phase5`), incluindo cenário negativo de guard page.
- Documento técnico da fase criado em `docs/SCHEDULER_PHASE5.md`.

Checklist crítico de validação (Fase 5):
- [x] Estrutura de processo mínima (PID + estado + contexto) integrada ao kernel.
- [x] Self-test de troca de contexto executado no boot com confirmação em log serial.
- [x] IRQ0 habilitada com PIT configurado sem regressão de boot/shell.
- [x] Quantum de preempção conectado ao caminho de retorno de interrupção.
- [x] Stacks de kernel por thread com guard page e teste negativo de fault.
- [x] Apps ELF e `.COM` entram em ring3 com pilhas user/kernel distintas e syscall gate real.
- [x] Foreground/background de apps usam o scheduler como ciclo de vida comum.
- [x] Falhas de user mode e ponteiros inválidos em syscall ficam contidos ao processo/grupo.
- [x] Documentação e suites automatizadas refletem o estado final da fase.

Validação final:
- `make verify-image`
- `python3 tests/test_serial.py --disk build/minidos-serial.img "ver" "drives"`
- `python3 tests/test_paging.py --disk build/minidos-paging.img`
- `python3 tests/test_phase5.py --disk build/minidos-phase5.img`
- `python3 tests/test_phase4.py --disk build/minidos-phase4.img`
- `python3 tests/test_multitask_elf.py --disk build/minidos-multitask.img`
- `python3 tests/test_multitask_com.py --disk build/minidos-multitask-com.img`
- `python3 tests/test_user_isolation.py --disk build/minidos-useriso.img`
- builds negativos com `-DSCHED_TEST_GUARD` e `-DPAGING_TEST_PF` a passar

## Ordem Recomendada de Execução

1. Fase 0
2. Fase 1
3. Fase 2
4. Fase 3
5. Fase 4
6. Fase 5

## Riscos Principais

- Ativar paging sem tratamento de fault tende a gerar falhas difíceis de diagnosticar.
- Migrar para IRQ cedo demais pode mascarar bugs básicos de boot/memória.
- Evoluir FAT16 escrita sem suíte de regressão aumenta risco de corrupção silenciosa.
