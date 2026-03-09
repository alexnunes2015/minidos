# HARD CRITICS

## Leitura obrigatoria

Este documento assume o pior cenario possivel: se a confiabilidade deste projeto importasse de verdade, o estado atual seria inaceitavel. O objetivo aqui nao e proteger egos. E expor falhas de engenharia, disciplina e criterio com o nivel de severidade que o repositorio merece.

## Veredito brutal

MiniDOS, no estado atual, parece menos um sistema operacional em consolidacao e mais uma colecao de demos tecnicas coladas com fita adesiva. Ha esforco real aqui, mas esforco nao substitui engenharia. O projeto vende maturidade demais, documenta conclusoes cedo demais e depende de fragilidade estrutural em quase todas as camadas criticas.

Se alguem tentasse chamar isto de base seria para um sistema serio, a resposta correta seria: ainda nao. O repositorio transmite entusiasmo, nao confiabilidade.

## 1. Arquitetura: acoplamento excessivo e fronteiras fracas

- A antiga estrutura plana de `src/kernel/` era um sintoma claro de falta de fronteiras. A organização por subsistema melhora navegação e ownership, e `src/kernel/storage/fat16.c` ja começou a ser dividido por responsabilidade, mas ainda restam modulos grandes a decompor de forma equivalente.
- `src/kernel/video/video.c`, `src/kernel/input/keyboard.c`, `src/kernel/memory/paging.c`, `src/kernel/storage/disk.c` e `src/boot/stage2.asm` ainda mostram o mesmo padrao: componentes criticos crescem por acumulacao, sem decomposicao clara.
- `src/kernel/core/kernel.c` continua a misturar boot UX, panic flow, leitura de input, script autorun e coordenacao de subsistemas. Isto nao e um "kernel main" enxuto; e um ponto de acoplamento que facilita regressao em cadeia.
- A presenca de helpers repetidos de baixo nivel (`inb`, `outb`, leitura fisica, parsing utilitario) espalhados por multiplos ficheiros sugere falta de uma camada minima de infraestrutura comum. Cada modulo parece resolver sozinho problemas que deveriam ter dono unico.

Conclusao: a base esta a crescer lateralmente, nao verticalmente. O resultado inevitavel disso e manutencao lenta, debugging caro e regressao escondida.

## 2. O projeto fala como sistema, mas entrega como prototipo

- `docs/DESIGN.md` e `docs/ROADMAP.md` usam linguagem de sistema estabilizado em varias areas, mas o codigo exposto ainda tem cheiro claro de fase experimental.
- A Fase 5 esta marcada como concluida, mas `src/kernel/process/process.c` tem 16 linhas e basicamente so converte estados para texto. Isso nao invalida o trabalho restante em `src/kernel/process/scheduler.c`, mas revela um problema de criterio: o projeto celebra marcos cedo demais.
- O scheduler atual trabalha com `SCHED_MAX_PROCS 3`, stacks fixas em arrays locais e um self-test interno. Isso pode ser um bom experimento. Nao pode ser vendido com linguagem que sugira preparacao robusta para multitarefa real.
- O proprio design admite "runtime process population is still minimal" e "scheduler coverage is currently driven mostly by self-tests". Traducao honesta: ainda nao existe base operacional madura, existe uma prova de conceito com logs convincentes.

Conclusao: o projeto sofre de inflacao narrativa. Quando a documentacao promete mais do que a implementacao sustenta, a engenharia perde credibilidade.

## 3. O caminho "floppy-first" esta mal resolvido

- O projeto afirma identidade "floppy-first", mas `src/kernel/storage/fat12.c` esta literalmente vazio com a nota "FAT12 support removed". Isso nao e um detalhe; e uma contradicao de produto.
- Falar em inspiracao MS-DOS e em media principal por floppy enquanto a stack real empurra ATA, BIOS thunk e FAT16 e uma confusao de direcao tecnica.
- Se o boot medio principal e a disquete, remover FAT12 e continuar a discursar como se isso fosse apenas um detalhe arquitetural e falta de honestidade tecnica.

Conclusao: ou o projeto assume que a disquete e apenas veiculo de boot de conveniencia, ou implementa a historia completa. O estado atual fica no pior dos dois mundos.

## 4. O processo de build cheira a improviso acumulado

- `scripts/build_disk.sh` repete quatro funcoes quase identicas para compilar apps (`GUESS100`, `DOSSHELL`, `EDIT`, `WIN95UI`). Isto nao e pipeline; e copy-paste operacionalizado.
- O script depende de `mkfs.vfat` e depois cai para `mtools` ou `sudo` passwordless para povoar a imagem. Isso pode ate funcionar na maquina do autor. Nao e uma cadeia de build robusta nem portavel.
- O patch do `kernel_sectors` em `stage2.bin` depende de parsing de listagem (`stage2.lst`) com `awk` e de um patch binario ad hoc. Funciona enquanto tudo continuar exatamente igual. Engenharia seria tenta reduzir esse tipo de fragilidade, nao normaliza-la.
- O Makefile concentra build, imagem, boot, suites e fluxos de debug num unico nivel. Isso e pratico no inicio, mas aqui ja virou sinal de que as responsabilidades de tooling nao foram tratadas como subsistema proprio.

Conclusao: o build nao inspira confianca. Inspira medo de mexer.

## 5. Os testes existem, mas ainda nao merecem confianca total

- O repositorio tem mais testes do que muitos projetos hobby. Isso e positivo. O problema e o contrato desses testes.
- Ha dependencia extensa de `time.sleep`, `timeout`, polling por stdout e esperas por marcadores de texto em `tests/test_serial.py`, `tests/test_phase3.py`, `tests/test_phase4.py`, `tests/test_mouse_ui.py`, `tests/test_shell.sh` e `tests/test_auto.sh`.
- Testes que precisam de atrasos arbitrarios e retries para "talvez" apanhar o estado certo nao sao so feios; sao um sintoma direto de observabilidade insuficiente e sincronizacao mal definida.
- O proprio protocolo do projeto diz para preferir marcadores deterministicos e evitar heuristicas por tempo. O codigo de teste contradiz essa regra varias vezes.
- A existencia de `tests/__pycache__/` e ficheiros `.pyc` no workspace mostra higiene fraca. Um repositorio que deixa lixo gerado misturado com a arvore de testes esta a comunicar desleixo basico.

Conclusao: a suite atual detecta algumas regressões, mas ainda nao define um contrato rigoroso do sistema. Ela observa comportamento; nao o controla com precisao suficiente.

## 6. A documentacao tem excesso de otimismo e falta de constrangimento

- `docs/ROADMAP.md` marca fases como concluidas com linguagem forte, mesmo quando a entrega real parece ser "base inicial", "self-test", "hook" ou "protótipo". Isto dilui o significado de "concluido".
- `docs/DESIGN.md` faz um bom trabalho a listar capacidades, mas por vezes apresenta limitacoes reais com tom de nota lateral, quando na verdade deveriam aparecer como riscos centrais.
- Um projeto serio usa documentacao para reduzir ilusao operacional. Aqui, em varios pontos, a documentacao ajuda a criar essa ilusao.

Conclusao: documentar progresso nao e o mesmo que inflar maturidade. Neste estado, a documentacao esta demasiado perto de marketing interno.

## 7. O desenho do kernel ainda esta demasiado dependente de "funciona aqui"

- Ha busy-waits, timeouts fixos e loops de polling em caminhos sensiveis de disco e inicializacao. Isso e toleravel em bring-up inicial. Quando começa a acumular, vira divida estrutural.
- A logica de panic/BSOD e reinicio depende de combinacoes de serial, teclado e reset por controlador. Num projeto serio, isso exigiria uma matriz clara de comportamento e falhas. Aqui parece mais uma composicao pragmatica sem contrato forte.
- Varias partes do sistema parecem validadas sobretudo via QEMU e logs seriais. Isso e normal no arranque de um OS. O erro e agir como se essa cobertura local equivalessse a robustez arquitetural.

Conclusao: ainda ha muito comportamento "best effort" em zonas que o projeto ja descreve como fundacao.

## 8. Falta rigor de produto

- O projeto ainda nao decidiu com firmeza o que quer ser: um OS educacional minimamente disciplinado, um laboratorio de features retro, ou uma vitrine crescente de demos ELF e UI.
- Sem essa decisao, cada nova feature corre o risco de aumentar superficie sem aumentar confiabilidade.
- Um kernel pequeno pode ser serio. Um kernel pequeno e vaidoso torna-se rapidamente uma colagem de features sem prioridade.

Conclusao: o problema nao e ambicao. E ambicao sem poda.

## O que teria de mudar imediatamente

1. Parar de declarar fases "concluidas" quando so existe self-test, hook parcial ou demo interna.
2. Quebrar `shell.c`, `fat16.c`, `video.c`, `keyboard.c` e `stage2.asm` em modulos menores com donos claros e fronteiras testaveis.
3. Definir um contrato honesto para "floppy-first". Se FAT12 saiu, a documentacao e a narrativa do produto precisam ser corrigidas no mesmo dia.
4. Remover dependencias de temporizacao arbitraria dos testes sempre que possivel e substituir por marcadores seriais deterministas.
5. Limpar a higiene do repositorio: nada de `__pycache__`, nada de artefactos gerados misturados com codigo fonte, nada de estado ambiguo.
6. Tratar tooling como subsistema e refatorar `build_disk.sh` antes que ele se torne intocavel.
7. Reescrever a linguagem dos docs para refletir maturidade real, nao entusiasmo acumulado.

## Sentenca final

Hoje, MiniDOS e um projeto tecnicamente interessante, mas operacionalmente complacente. Ha sinais de capacidade. Ainda nao ha sinais suficientes de severidade. E sem severidade, um sistema deste tipo degrada-se depressa: cresce em superficie, perde coerencia e passa a depender de sorte, timing e memoria tribal.

Se vidas dependessem disto, a recomendacao responsavel seria simples: nao confiar, nao promover e nao alargar o escopo ate a base atual ser drasticamente apertada.
