# Cursor Bitmap Assets

Esta pasta contém o kit de preparação do cursor bitmap usado por `ui_draw_cursor()`.

## Formato de trabalho

- Arquivo fonte preferido no fluxo automático: `cursor.png`
- Fallback compatível: `cursor.bmp`
- Tamanho final: preserva o tamanho original da imagem (ex.: `14x23`)
- Saída gerada: `external_apps/runtime/minidos_cursor_bitmap.h`
- Hotspot atual: canto superior esquerdo (`0,0`)

## Convenção de cores

O conversor reduz a imagem para 3 estados:

- transparente: alpha menor que 50% ou vermelho `#FF0000`
- outline: pixels escuros
- fill: pixels claros

Para transparência real, use `cursor.png` com canal alpha.

Para BMP sem alpha, use vermelho `#FF0000` como fundo transparente.

Esse valor também fica comentado no header gerado para referência.

## Fluxo rápido

1. Crie ou gere `cursor.png` nesta pasta.
2. Rode `make`.
3. O build prepara automaticamente `minidos_cursor_bitmap.h` antes de compilar as apps.

Se `cursor.png` não existir, o build usa `cursor.bmp` como fallback.

## Conversão manual

```bash
cd assets/cursor
chmod +x convert_cursor.sh
./convert_cursor.sh cursor.png
```

Ou gere para outro destino:

```bash
./convert_cursor.sh cursor.png ../../external_apps/runtime/minidos_cursor_bitmap.h
```

## Gerar um cursor de exemplo

```bash
python3 create_demo_cursor.py
```

Isso gera um `cursor.png` de exemplo com alpha, contorno preto e preenchimento branco.
