# MiniDOS Phase 1 - Paging Plan

Este documento define a execução completa da Fase 1: implementação, documentação e testes até conclusão.

## Objetivo da Fase 1

Ativar paging mínimo no kernel sem regressão de boot/shell, com diagnóstico confiável de page fault.

## Escopo Técnico

- Paging 32-bit com páginas de 4KB.
- Identity mapping inicial para manter compatibilidade durante transição.
- Handler de `#PF` com logs seriais para depuração.
- Sem isolamento completo kernel/user nesta fase.

## Implementação (ordem recomendada)

1. Base de código de paging
- Criar `src/kernel/memory/paging.h` e `src/kernel/memory/paging.c`.
- Definir flags de entrada (`PRESENT`, `RW`, `USER`).
- Alocar/definir page directory e page tables alinhados em 4KB.

2. Mapeamento inicial (identity map)
- Mapear região baixa usada no boot/runtime inicial.
- Garantir mapeamento de:
- Kernel carregado.
- Memória de vídeo texto/FB usada no runtime.
- Metadados de boot lidos pelo kernel (ex.: área `0x0500+`).
- Stack ativa do kernel.

3. Integração no boot do kernel
- Inicializar estruturas e carregar `CR3`.
- Ativar `CR0.PG` somente após serial/log ativos.
- Registrar logs de progresso (`paging init`, `paging enabled`).

4. Tratamento de page fault
- Instalar handler de vetor 14 (`#PF`).
- Logar `CR2`, error code e endereço de retorno.
- Encerrar em panic controlado (loop de halt), evitando falha silenciosa.

## Estratégia de Documentação

Durante a implementação:
- Atualizar `docs/ROADMAP.md` com estado de cada subetapa.
- Atualizar `docs/DESIGN.md` quando paging passar a estado ativo.

Ao concluir:
- Este arquivo (`docs/PAGING.md`) deve refletir o design final realmente implementado.
- Registrar limitações remanescentes para Fase 2.

## Estratégia de Testes

## 1) Baseline antes de ativar paging
- `make phase0-check`
- Objetivo: garantir base estável antes da mudança.

## 2) Smoke com paging ativo
- `make clean && make && make test-serial`
- Esperado: boot completo, shell operacional, logs de paging presentes.

## 3) Self-test de mapeamento
- Validar leitura/escrita em endereços identity-mapped críticos.
- Log esperado: `paging self-test OK`.

## 4) Teste negativo controlado (#PF)
- Acesso intencional a endereço não mapeado em modo de teste.
- Esperado: log de `#PF` com `CR2` correspondente.
- Critério: sistema entra em panic controlado, sem reset silencioso.

## 5) Regressão funcional de shell
- Confirmar `ver`, `drives` e `dir` após paging ativo.
- Mesmo comportamento observado na Fase 0.

## 6) Automação
- Criar `tests/test_paging.py` para validar padrões de log.
- Adicionar alvo `make test-paging`.

## Critério de Conclusão (Definition of Done)

- Paging ativo por padrão no fluxo de boot atual.
- Handler de `#PF` funcional com diagnóstico serial.
- `make test-serial` e `make test-paging` passando em build limpo.
- Shell funcional após ativação de paging.
- `docs/ROADMAP.md`, `docs/DESIGN.md` e `docs/PAGING.md` atualizados e consistentes.

## Riscos e Mitigações

- Risco: ativar `CR0.PG` com mapeamento incompleto.
- Mitigação: identity map conservador no início e logs por etapa.

- Risco: fault sem diagnóstico.
- Mitigação: instalar `#PF` antes da ativação final.

- Risco: regressão no shell após paging.
- Mitigação: manter testes seriais de regressão em cada mudança.
