# Boot Logo

Esta pasta contém o sistema de logo de boot do MiniDOS.

## ⚠️ Limitação Atual

O bootloader MBR tem apenas **446 bytes** disponíveis (o resto é tabela de partições). Isso não é suficiente para incluir o código do boot logo.

## Soluções Possíveis

1. **Second-Stage Bootloader**: Criar um segundo estágio no setor 1 que carrega o logo antes do kernel
2. **Implementar no Kernel**: Adicionar modo V86 no kernel para usar INT 0x10
3. **VGA Direto**: Manipular registradores VGA diretamente em modo protegido (complexo)

## Especificações da Imagem

- **Resolução:** 320x200 pixels
- **Cores:** 256 cores (VGA Mode 13h)
- **Formato:** BMP de 8-bit (indexed color)

## Como Usar

### 1. Criar Logo de Demo
```bash
python3 create_demo_logo.py
```

### 2. Ou Criar Sua Própria Imagem

No GIMP:
1. Criar imagem 320x200 pixels
2. Image → Mode → Indexed (256 cores)
3. Exportar como BMP

No Photoshop:
1. Criar imagem 320x200 pixels
2. Image → Mode → Indexed Color (256 colors)
3. Salvar como BMP

### 3. Converter para Formato Raw

```bash
./convert_logo.sh boot_logo.bmp
```

Isso cria `logo.raw` (64000 bytes) pronto para o boot.

### 4. Build Automático

O `make` automaticamente:
1. Converte o logo se houver `boot_logo.bmp`
2. Adiciona `BOOTLOGO.DAT` à partição C:
3. Exibe por 5 segundos no boot

## Arquivos

- `boot_logo.bmp` - Imagem original (320x200, 256 cores)
- `logo.raw` - Imagem convertida (64000 bytes)
- `convert_logo.sh` - Script de conversão
- `create_demo_logo.py` - Gerador de logo demo

## Dicas

- Use cores vivas (azul, vermelho, verde)
- Evite gradientes muito suaves (palette limitada)
- Teste no QEMU para ver o resultado
- Inspiração: Windows 95, Windows 98, MS-DOS 6.22
