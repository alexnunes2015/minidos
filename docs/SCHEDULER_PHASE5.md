# Fase 5 - Runtime de Scheduler

## Objetivo
Sair do prototipo de troca de contexto por `ret` e fechar um runtime com scheduler real, stacks protegidas, ring3 e transicao kernel/user controlada para apps ELF e COM.

## Entregas desta fase
- Estrutura de processo (`process_t`) com PID, estado, contexto salvo e metadados de stack user/kernel.
- Bootstrap do scheduler com thread de arranque (`kernel`) + `idle`.
- Kernel threads preparadas como frames completos de retorno de IRQ, em vez de stacks artificiais dependentes de `ret`.
- Yield voluntario via software interrupt (`int 0x81`) reutilizando o mesmo dispatcher de interrupcoes.
- Sleep/bloqueio por ticks com wakeup em IRQ0.
- Guard page nao mapeada por stack de kernel gerida pelo scheduler, na arena `0x00600000`.
- Self-test de runtime no boot (`scheduler_phase5_self_test`) com round-robin real entre duas tarefas.
- Teste negativo opcional (`SCHED_TEST_GUARD`) que toca a guard page e valida o `#PF`.
- GDT/TSS com seletores user, `esp0` por task e handoff `iret` para ring3.
- `int 0x80` como gate de syscall para apps foreground/background, incluindo ELFs e `.COM`.
- Page directories por app com janela user dedicada fora da identity map de low memory, stacks de user por PID e fault containment para user mode.
- Arena fisica dinamica de userland em paginas (`0x00700000..0x00B00000`) com preflight de ELF/COM, substituindo os slots fixos de 1 MiB.
- Regressao de isolamento (`tests/test_user_isolation.py`) cobrindo ponteiro invalido em syscall e page fault de user mode em ELFs e `.COM`, com retorno ao shell.
- Regressao de ELF grande (`tests/test_large_elf.py`) cobrindo uma imagem mapeada acima de 1 MiB com marcadores `PTBIG100` / `PTBIG190`.

## Marcadores seriais
- `SCHED100` - runtime do scheduler inicializado.
- `SCHED110` - guard pages das stacks de kernel armadas.
- `SCHED120` - self-test da fase iniciado.
- `SCHED190` - self-test positivo concluido.
- `SCHED900` - page fault em guard page de stack identificado.
- `STOP 0x00000006` - bootstrap do runtime do scheduler falhou antes da shell.
- `STOP 0x00000007` - self-test obrigatorio da fase 5 falhou antes da shell.
- `APPFLT900` - fault de app em ring3 identificado e contido ao grupo da app.
- `PTBIG100` / `PTBIG190` / `PTBIG900` / `PTBIG901` - regressao de ELF grande iniciado, concluido, ou falhado.

Os logs humanos continuam presentes:
- `[sched] phase5 context-switch self-test start`
- `[sched] task A step` / `[sched] task B step`
- `[sched] phase5 context-switch self-test OK`
- `[sched] preemption enabled`
- `[paging] #PF detected` + `mode=user` quando uma app viola o isolamento

## Proximos passos
- Decidir como migrar o shell/bootstrap para uma stack tambem protegida por guard page sem depender da thread bootstrap legacy.
- Evoluir da janela user contigua por grupo para mapeamento por segmento e accounting real de memoria por processo.
