# HARD CRITICS

## Leitura obrigatoria

Este documento deve ser lido como se uma falha deste sistema pudesse matar uma pessoa. Essa e a barra correta. Nao existe tolerancia para comportamento "quase estavel", para testes que passam por sorte, para degradacao silenciosa ou para documentacao que exagera maturidade. Em software de sobrevivencia, falha intermitente e defeito fatal. Contrato implicito e defeito fatal. Recuperacao mal definida e defeito fatal.

O objetivo deste texto nao e insultar o projeto. E eliminar autoengano. A pergunta certa nao e "isto esta interessante?". A pergunta certa e "isto pode ser confiado quando o custo de falhar e irreversivel?". Hoje, ainda nao.

## Veredito atualizado

O repositorio melhorou de forma real em relacao a criticas antigas. Ja nao e correto descreve-lo como simples colagem plana de demos:

- ha reorganizacao por subsistemas em `src/kernel/`;
- `fat16.c` ja nao concentra sozinho toda a stack de storage;
- o scheduler deixou de ser um stub irrelevante;
- existe userland real com ELF, `.COM`, ring3, syscalls e testes dedicados;
- o build de apps bundled foi consolidado em torno de uma tabela unica em `scripts/build_disk.sh`.

Essas melhorias importam. Ignora-las seria critica preguiocosa.

Mas o problema mais grave mudou apenas de forma: antes faltava base; agora existe base, mas ainda com disciplina desigual, contratos pouco duros e zonas onde o sistema prefere continuar a funcionar de qualquer maneira em vez de falhar de forma estrita e auditavel. Para um sistema de vida-ou-morte, isso continua a ser reprovacao.

## 1. A base evoluiu, mas a severidade ainda nao acompanha a ambicao

- O projeto ja demonstra mais estrutura, mais cobertura e mais runtime real do que antes.
- O erro atual nao e ausencia de implementacao. E aceitacao de demasiada ambiguidade operacional depois de a implementacao crescer.
- Ainda ha demasiados caminhos onde o criterio parece ser "o sistema sobe e a demo corre" em vez de "o comportamento esta completamente definido, defendido e provado contra erro".

### Correcao exigida

1. Cada subsistema critico deve declarar invariantes, entradas invalidas, estados terminais e politica de falha.
2. O padrao de aceitacao tem de mudar de "funciona no QEMU" para "o contrato foi exercitado em condicao normal e de erro".
3. O repositorio deve parar de aceitar sucesso observacional como substituto de prova minima.

## 2. Arquitetura: houve decomposicao real, mas ainda existem blocos grandes demais e ownership confuso

As criticas antigas sobre estrutura plana ja nao sao totalmente validas. A divisao em `core`, `video`, `storage`, `input`, `time`, `memory`, `process` e `shell` e um avanco concreto. Tambem e factual que `fat16.c` foi repartido em ficheiros mais especializados.

O problema agora e outro:

- `src/kernel/shell/shell_apps.c` continua com mais de 2000 linhas;
- `src/kernel/shell/shell_builtin.c` continua acima de 1000 linhas;
- `src/kernel/storage/fat16_dir.c`, `src/kernel/process/scheduler.c`, `src/kernel/video/video.c`, `src/kernel/core/kernel.c` e `src/boot/stage2.asm` continuam grandes demais para zonas criticas;
- existem dois caminhos de teclado no tree, `src/kernel/keyboard.c` e `src/kernel/input/keyboard.c`, com headers duplicados, o que indica lixo residual ou autoridade tecnica duplicada sobre o mesmo dominio.

Num sistema critico, modulo gigante nao e problema de estilo. E problema de auditabilidade. Ficheiro grande em caminho sensivel aumenta custo de revisao, dificulta prova local de comportamento e esconde regressao.

### Correcao exigida

1. Partir `shell_apps.c`, `shell_builtin.c` e `fat16_dir.c` por responsabilidades funcionais testaveis.
2. Reduzir `kernel.c` a orquestracao declarativa de arranque, sem concentrar politica operacional.
3. Eliminar implementacoes residuais mortas ou duplicadas, especialmente no dominio de teclado.
4. Tornar ownership tecnico univoco por subsistema: um unico caminho ativo, uma unica API, uma unica fonte de verdade.

## 3. O maior defeito atual e cultural: a documentacao ainda vende robustez acima do que o codigo prova

Este ponto continua fortemente valido.

- `docs/ROADMAP.md` redefine `concluida` como "contrato minimo ... com validacao suficiente para a epoca". Isto e um criterio elastico demais para merecer a palavra "concluida".
- A documentacao principal reconhece limitacoes reais, mas a linguagem de marcos fechados continua demasiado confortavel para um sistema ainda em hardening.
- O proprio pipeline de build contem drift documental: no caminho `mtools`, o `PTEST/README.TXT` ainda afirma que o runtime ELF continua atribuido a thread da shell, o que contradiz a Fase 5 atual.
- O comando `ver` ainda imprime "boot floppy FAT12 + FAT16 runtime", formula que simplifica demasiado uma realidade mais assimetrica e mais fraca do que essa frase sugere.

Em sistema critico, documentacao otimista nao e defeito cosmetico. E defeito de decisao. Faz a proxima pessoa operar com premissas falsas.

### Correcao exigida

1. Proibir "concluido" sem criterio binario, rastreavel e ainda valido hoje.
2. Separar em cada doc principal: `entregue`, `validado`, `ainda fragil`, `nao coberto`.
3. Auditar strings de produto, README interno da imagem e texto de shell para remover qualquer narracao acima do que o sistema realmente suporta.

## 4. O tema "floppy-first" continua mal resolvido, mas a critica precisa de ser mais precisa

A versao antiga desta critica estava parcialmente simplificada demais. O estado real e pior num sentido mais subtil:

- `src/kernel/storage/fat12.c` esta vazio e `fat12.h` e apenas placeholder;
- ao mesmo tempo, `fat16.c` ja contem deteccao de FAT12 e leitura/escrita de entradas FAT12;
- `drive.c` expoe a boot floppy como volume inteiro BIOS-backed quando valida;
- `docs/DESIGN.md` admite que nao existe driver FAT12 standalone nem driver nativo de FDC.

Isto significa que o sistema nao tem ausencia total de caminho FAT12. Tem algo potencialmente mais perigoso: suporte assimetrico, ownership confuso e nomenclatura enganadora. Parte do comportamento FAT12 existe, mas o modulo dedicado a FAT12 esta vazio, e o contrato completo continua difuso.

### Correcao exigida

1. Decidir se FAT12 e um subsistema suportado ou apenas detalhe de compatibilidade de boot.
2. Se for suportado, criar ownership tecnico explicito e testes dedicados de FAT12, em vez de esconder essa logica dentro de `fat16.c`.
3. Se nao for suportado como primeira classe, corrigir nomenclatura, docs e strings para refletir o contrato real: boot floppy BIOS-backed, sem stack FAT12 completa e autonoma.

## 5. Build e tooling melhoraram, mas ainda nao merecem confianca de cadeia critica

Aqui tambem houve progresso real. O `build_disk.sh` deixou de ter quatro funcoes quase iguais para apps e passou a usar uma tabela de specs mais limpa. Essa critica antiga ja nao esta atualizada.

Mas continuam problemas graves:

- o patch de `kernel_sectors` em `stage2.bin` ainda depende de `awk` sobre listing e de um patch binario em Python;
- a populacao da imagem continua bifurcada entre `mtools` e `sudo`, o que aumenta variacao de ambiente;
- o caminho `mtools` ainda contem texto funcionalmente desatualizado;
- o workspace continua a acumular `build/`, `.venv/` e `tests/__pycache__/`, o que nao prova tracking indevido em git, mas prova ambiente operacional com ruido facil de confundir com estado real.

Num contexto critico, build tem de ser monotono, estrito e reproduzivel. Se o mesmo commit pode depender de ferramentas ou caminhos auxiliares diferentes para produzir a imagem final, a cadeia de confianca fica mais fraca.

### Correcao exigida

1. Reduzir o numero de caminhos de producao da imagem final.
2. Tornar o patch do stage2 menos fragil ou, idealmente, eliminavel por design.
3. Garantir que artefactos operacionais nao contaminam a leitura do estado do repositorio.
4. Validar no proprio pipeline o conteudo final gerado, incluindo ficheiros de payload e texto embarcado.

## 6. Testes: melhor do que antes, mas ainda aquem de um contrato de nao-falha

Tambem aqui a critica precisa de nuance. Ja existe mais orientacao por marcadores do que havia antes. `test_phase5.py` e varios testes de regressao procuram markers seriais especificos, o que e melhor do que simples observacao humana.

Mas a suite ainda nao merece confianca plena:

- `tests/test_serial.py` ainda usa `time.sleep`;
- `tests/qemu_harness.py` continua baseado em polling por stdout com `select`;
- `tests/test_keyboard_irq.py` e `tests/test_mouse_ui.py` ainda usam sleeps e atrasos de injecao;
- a prontidao do sistema continua inferida por banners, marcadores e output textual, nao por um protocolo de observabilidade explicitamente versionado.

Isto nao torna os testes inuteis. Torna-os insuficientes para o nivel de certeza exigido por um sistema critico.

### Correcao exigida

1. Definir um protocolo formal de marcadores seriais de boot, panic, scheduler, storage, shell e app runtime.
2. Substituir atrasos arbitrarios por sincronizacao de estados observaveis sempre que possivel.
3. Distinguir claramente smoke tests, regressao funcional e testes de contrato.
4. Tratar instabilidade de harness como bug do sistema de validacao, nao como custo aceitavel.

## 7. O codigo ainda aceita degradacao silenciosa em zonas onde devia falhar de forma dura

Este e um dos pontos mais graves encontrados na revisao atual.

- `kernel.c` salta `AUTOEXEC.AUT` quando o storage nao esta pronto e limita-se a log debug;
- `mouse.c` pode desistir do rato com log de "controller busy";
- o teclado ainda mantem fallback por polling;
- e, mais grave, `drive.c` cria um drive `A:` sintetico de teste quando nao encontra particoes nem boot floppy valida.

Esse ultimo ponto e especialmente inaceitavel para um sistema critico. Fabricar um volume plausivel quando a deteccao real falhou e a forma exata de esconder ausencia de hardware, erro de enumeracao ou corrupcao de media atras de uma aparencia de normalidade.

Em software de sobrevivencia, o sistema nunca pode inventar realidade operacional para parecer utilizavel.

### Correcao exigida

1. Remover qualquer criacao de drive sintetica fora de ambiente de teste explicitamente isolado.
2. Promover falhas de disponibilidade de storage a erros claros, ruidosos e nao ambiguos.
3. Definir onde o sistema pode degradar e onde deve falhar imediatamente.
4. Eliminar fallback residual de polling assim que o caminho por IRQ estiver suficientemente provado.

## 8. A preparacao para multitarefa existe, mas ainda ha demasiado "self-test" no centro da historia

A critica antiga que chamava `process.c` de stub ja nao basta para avaliar a Fase 5. Hoje o scheduler existe, o runtime existe e os testes existem. Essa parte precisa de ser reconhecida.

Mas tambem nao seria serio confundir isso com maturidade operacional alta:

- `process.c` continua quase trivial, com o peso real deslocado para `scheduler.c`;
- o scheduler ainda depende fortemente de marcadores de self-test como parte central da validacao;
- a narrativa de fase fechada continua demasiado apoiada em boot markers, regressao controlada e cenario QEMU, nao em diversidade de falha e prova de comportamento adversarial.

### Correcao exigida

1. Mover a confianca da fase de "self-test no boot" para suites de contrato mais amplas.
2. Aumentar cenarios negativos de scheduler, isolamento, starvation, cleanup e erro de syscall.
3. Tratar "passa no QEMU oficial" como baseline de laboratorio, nao como selo de robustez.

## Regra de ouro

Toda a decisao tecnica devia responder a esta pergunta:

> Se esta suposicao estiver errada em producao, uma pessoa pode morrer?

Se a resposta for "sim" ou "nao sabemos", entao:

- a suposicao nao pode ficar implicita;
- o comportamento tem de ser especificado;
- a falha tem de ser detectavel;
- a recuperacao tem de ser definida;
- o teste tem de ser repetivel;
- a documentacao nao pode exagerar o estado real.

## O que tem de mudar imediatamente

1. Atualizar docs e strings de produto para refletir o estado real, nao o estado desejado.
2. Remover criacao de drives sinteticos e outras degradacoes silenciosas fora de testes explicitamente controlados.
3. Quebrar modulos gigantes em unidades auditaveis com ownership claro.
4. Definir o contrato real de FAT12/floppy e parar a ambiguidade atual.
5. Endurecer o pipeline de build para reduzir caminhos alternativos e drift embarcado.
6. Formalizar um protocolo de marcadores/testes menos dependente de timing e polling.
7. Limpar codigo residual duplicado e qualquer fonte de autoridade paralela sobre o mesmo subsistema.

## Sentenca final

Se a vida de uma pessoa dependesse deste sistema hoje, a resposta responsavel continuaria a ser nao. Nao porque o projeto esteja vazio ou amador no mesmo sentido de antes. Pelo contrario: ele ja tem base suficiente para que as falhas atuais sejam mais serias, nao menos. Agora os riscos nao estao apenas na ausencia de features. Estao na combinacao de capacidade real com severidade ainda insuficiente.

Num sistema destes, parecer promissor nao vale nada. Arrancar nao vale nada. Ter varias fases "concluidas" nao vale nada. So contam contratos duros, falha explicita, testes reprodutiveis e documentacao que nao mente nem simplifica em excesso. Tudo o resto ainda esta abaixo da barra.
