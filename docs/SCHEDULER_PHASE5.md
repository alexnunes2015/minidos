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
- Regressao de isolamento (`tests/test_user_isolation.py`) cobrindo ponteiro invalido em syscall e page fault de user mode em ELFs e `.COM`, com retorno ao shell.

## Marcadores seriais
- `SCHED100` - runtime do scheduler inicializado.
- `SCHED110` - guard pages das stacks de kernel armadas.
- `SCHED120` - self-test da fase iniciado.
- `SCHED190` - self-test positivo concluido.
- `SCHED900` - page fault em guard page de stack identificado.
- `APPFLT900` - fault de app em ring3 identificado e contido ao grupo da app.

Os logs humanos continuam presentes:
- `[sched] phase5 context-switch self-test start`
- `[sched] task A step` / `[sched] task B step`
- `[sched] phase5 context-switch self-test OK`
- `[sched] preemption enabled`
- `[paging] #PF detected` + `mode=user` quando uma app viola o isolamento

## Proximos passos
- Evoluir de slots fixos de 1 MiB para gestao de memoria por processo mais flexivel.
- Decidir como migrar o shell/bootstrap para uma stack tambem protegida por guard page sem depender da thread bootstrap legacy.
- Evoluir da janela user fixa de 1 MiB para um layout virtual por processo mais flexivel.
