# Fase 5 - Runtime de Scheduler

## Objetivo
Sair do prototipo de troca de contexto por `ret` e estabelecer um runtime de kernel threads que use o caminho real de interrupcao, com stacks protegidas por guard page.

## Entregas desta fase
- Estrutura de processo (`process_t`) com PID, estado e contexto minimo (`esp` salvo).
- Bootstrap do scheduler com thread de arranque (`kernel`) + `idle`.
- Kernel threads preparadas como frames completos de retorno de IRQ, em vez de stacks artificiais dependentes de `ret`.
- Yield voluntario via software interrupt (`int 0x81`) reutilizando o mesmo dispatcher de interrupcoes.
- Sleep/bloqueio por ticks com wakeup em IRQ0.
- Guard page nao mapeada por stack de kernel gerida pelo scheduler, na arena `0x00600000`.
- Self-test de runtime no boot (`scheduler_phase5_self_test`) com round-robin real entre duas tarefas.
- Teste negativo opcional (`SCHED_TEST_GUARD`) que toca a guard page e valida o `#PF`.

## Marcadores seriais
- `SCHED100` - runtime do scheduler inicializado.
- `SCHED110` - guard pages das stacks de kernel armadas.
- `SCHED120` - self-test da fase iniciado.
- `SCHED190` - self-test positivo concluido.
- `SCHED900` - page fault em guard page de stack identificado.

Os logs humanos continuam presentes:
- `[sched] phase5 context-switch self-test start`
- `[sched] task A step` / `[sched] task B step`
- `[sched] phase5 context-switch self-test OK`
- `[sched] preemption enabled`

## Proximos passos
- Mover apps ELF para processos do scheduler em vez de executa-las dentro da thread bootstrap do kernel.
- Introduzir ring3, pilha de user, permissao `U/S` em paging e uma transicao syscall/user->kernel real.
- Decidir como migrar o shell/bootstrap para uma stack tambem protegida por guard page.
