# MiniDOS Roadmap

Este roadmap organiza os próximos passos do S.O. em fases incrementais, com foco em estabilidade, memória, I/O e execução de apps ELF.

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
- Leituras de memória física fixa ajustadas em `src/kernel/kernel.c` e `src/kernel/video.c` para eliminar warnings críticos de build.

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

Objetivo: reduzir dependência de polling e melhorar resiliência.

- Consolidar IDT/ISR como caminho padrão de exceções.
- Migrar teclado para IRQ1 com fallback temporário por polling durante transição.
- Definir política de panic com dump mínimo de contexto no serial.

Critério de pronto:
- Entrada por teclado funcional via IRQ.
- Exceções comuns registradas com diagnóstico útil.

## Fase 3 - Disco e FAT16 com Escrita Confiável (1 a 2 semanas)

Objetivo: evoluir de MVP de disco para base utilizável.

- Expandir ATA além de `disk_id` 0.
- Validar leitura/escrita em múltiplos discos/canais suportados.
- Criar testes automatizados para operações FAT16 de escrita:
- Criar arquivo.
- Atualizar conteúdo.
- Remover arquivo/diretório.

Critério de pronto:
- Testes de escrita e leitura passando em sequência, sem corrupção detectável.

## Fase 4 - Userland e Execução ELF (2 semanas)

Objetivo: executar apps externas com contrato mínimo estável.

- Fechar ABI de syscalls (conjunto mínimo para I/O, arquivo e saída de processo).
- Integrar fluxo completo de build e carga ELF usando `external_apps/`.
- Adicionar comandos de shell para listar e executar binários ELF.

Critério de pronto:
- `hello_elf` e `stat_elf` executam pelo shell com retorno controlado ao kernel.

## Fase 5 - Preparação para Multitarefa (futuro)

Objetivo: preparar base técnica para scheduler simples.

- Isolar melhor contexto kernel/user.
- Definir estrutura de processo (PID, estado, stack user/kernel).
- Planejar round-robin inicial com timer.

Critério de pronto:
- Documento de design técnico fechado e protótipo de troca de contexto validado.

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
