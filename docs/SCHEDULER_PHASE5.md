# Fase 5 - Preparacao para Multitarefa

## Objetivo
Estabelecer a base tecnica para um scheduler simples e habilitar o caminho inicial de preempcao via timer.

## Entregas desta fase
- Estrutura de processo (`process_t`) com PID, estado e contexto minimo (`esp` salvo).
- Prototipo de troca de contexto em modo kernel (`sched_context_switch`) com pilha por tarefa.
- Self-test de troca de contexto no boot (`scheduler_phase5_self_test`) alternando entre duas tarefas.
- Preparacao de timer para round-robin: configuracao do PIT e coleta de ticks em IRQ0.
- Quantum de preempcao ligado ao retorno de interrupcao: IRQ0 pode requisitar troca de contexto retornando novo `ESP` para o stub comum.
- Registro de processos de runtime fora do self-test (`scheduler_start_runtime_demo`), com duas tarefas kernel no round-robin.

## Escopo do prototipo validado
- Troca de contexto cooperativa validada no self-test.
- Caminho de preempcao por quantum habilitado no IRQ0 com fallback seguro quando nao ha outro processo pronto.
- Validacao por log serial:
- `[sched] phase5 context-switch self-test start`
- `[sched] task A step` / `[sched] task B step`
- `[sched] phase5 context-switch self-test OK`
- `[sched] preemption enabled`
- `[sched] runtime demo tasks registered`

## Proximos passos
- Separar stack de kernel por processo e stack de usuario para apps ELF.
- Evoluir ABI de syscalls para transicao user/kernel com isolamento de memoria.
- Generalizar API de criacao de processo para uso por userland ELF (alem das tarefas de demo em ring0).
