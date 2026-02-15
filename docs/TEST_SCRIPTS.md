# MiniDOS Test Scripts

Permite enviar comandos automaticamente para o shell do MiniDOS e testar suas funcionalidades.

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
Valida entrada de teclado real via IRQ1 (sem enviar comando pela serial), injetando teclas pelo monitor QMP (`sendkey`) e confirmando que o comando chega ao shell sem duplicação.

**Uso:**
```bash
python3 tests/test_keyboard_irq.py
```

**Atalho via Makefile:**
```bash
make test-keyboard
```

## Características

✅ **Captura de Output** - Todos os comandos capturam a saída completa
✅ **Timeout** - Proteção contra hang infinito (10-15 segundos)
✅ **Serial Debug** - Via COM1 a 38400 baud
✅ **Boot Logo** - Mostra boot logo VGA Mode 13h antes do shell

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
- `dir` - Lista diretório
- `type <file>` - Mostra conteúdo de arquivo
- `c:`, `d:`, `e:` - Muda de drive

## Saída de Debug Serial

Todos os testes mostram a saída serial que inclui:
- `[Stage2] Started` - Stage2 bootloader carregado
- `[Stage2] Loading kernel...` - Kernel sendo carregado
- `[Stage2] Kernel loaded` - Kernel pronto
- `[Stage2] Entering PM...` - Transição para modo protegido
- `[paging] init` / `[paging] enabled` - Sequência de ativação de paging
- `paging self-test OK` - Self-test de mapeamento concluído
- `[paging] #PF detected` + `CR2=...` - Diagnóstico de page fault (teste negativo)

**Nota:** O kernel está executando após a transição para Protected Mode nos testes seriais atuais. Se houver regressão, use os checkpoints PM no serial (`CLI`, `LGDT`, `CR0.PE`, `Before far jump`) para localizar onde o boot para.

## Troubleshooting

Se os scripts não funcionar:
1. Verificar que `minidos.img` existe: `ls -lh minidos.img`
2. Executar `make clean && make` para recompilação
3. Verificar permissões: `chmod +x tests/test*.sh`
4. Verificar se QEMU está instalado: `which qemu-system-i386`
