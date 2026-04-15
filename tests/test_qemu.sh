#!/bin/bash
# Script para testar MiniDOS com QEMU (mais confiável que VirtualBox para debug)

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "=== Testando MiniDOS com QEMU ==="
echo ""
echo "Comandos disponíveis:"
echo "  help - lista comandos"
echo "  ver  - versão do sistema"
echo "  cls  - limpar tela"
echo "  dir  - listar arquivos"
echo ""
echo "Para sair: Ctrl+A depois X"
echo "Para sair (GUI): Ctrl+Alt+Q"
echo ""
echo "Iniciando em 2 segundos..."
sleep 2

qemu-system-i386 -drive file="$ROOT_DIR/minidos.img",format=raw,if=ide,index=0 -boot c
