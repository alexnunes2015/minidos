## Context

`WIN95UI` volta a ficar lento quando o wallpaper BMP está ativo porque `ui_draw_bitmap` percorre o ficheiro e emite um `app_gfx_rect` por pixel. O utilizador, no entanto, precisa de continuar a poder largar um `.bmp` normal sem etapa manual de conversão, e o projeto continua limitado pelo orçamento de tamanho do kernel/floppy image.

O runtime já sabe carregar BMPs para memória userland, enquanto o kernel já sabe desenhar rects, texto e apresentar o backbuffer. O desenho novo deve reutilizar essas duas realidades: decodificar uma vez no runtime e transferir pixels já decodificados para o kernel com granularidade de surface/blit.

## Goals / Non-Goals

**Goals:**
- Manter `.bmp` cru como formato externo para wallpapers e futuros fundos definidos pelo utilizador.
- Remover o custo de parse + syscall por pixel do caminho de redraw do `WIN95UI`.
- Reativar o background do `WIN95UI` com boa performance em full redraw e dirty-rect restore.
- Acrescentar a capacidade de blit de surfaces sem rebentar o orçamento atual do `kernel.bin`.

**Non-Goals:**
- Criar um compositor de janelas global fora da app.
- Adicionar aceleração por hardware ou alpha blending genérico.
- Suportar formatos extra além dos BMPs já aceites hoje pelo runtime.
- Resolver nesta change todos os gargalos do toolkit gráfico além do wallpaper/base desktop restore.

## Decisions

### 1. Decodificar BMP no runtime e não no kernel

O runtime continuará a aceitar ficheiros BMP do utilizador e fará o parse uma única vez para uma surface própria (por exemplo `XRGB8888`). O kernel não receberá BMP bruto; receberá um buffer já decodificado via um novo blit syscall.

- **Why:** preserva o contrato externo com BMP cru sem meter parser/scaler BMP no `kernel.bin`, que já está perto do limite da floppy.
- **Alternative considered:** parser BMP no kernel. Rejeitado porque acrescenta código exatamente na zona onde o orçamento de tamanho é mais apertado.

### 2. Introduzir um syscall de surface blit com clipping

O runtime gráfico ganhará um descritor de blit para copiar um buffer de pixels já decodificado para o backbuffer corrente, incluindo largura, altura, stride, formato simples e clip rect opcional.

- **Why:** reduz milhares de syscalls 1x1 a uma única operação lógica e permite dirty-rect restore barato.
- **Alternative considered:** continuar a usar `app_gfx_rect` em batches por linha/cor. Rejeitado porque melhora pouco para wallpapers fotográficos e não resolve o custo estrutural do bridge app/kernel.

### 3. Separar `wallpaper_surface` de `desktop_surface`

O `WIN95UI` terá duas caches:
- `wallpaper_surface`: pixels decodificados do BMP do utilizador
- `desktop_surface`: base do desktop já composta (wallpaper + taskbar base + elementos estáticos)

Os redraws parciais restauram primeiro a região de `desktop_surface` e só depois redesenham janela/menu/cursor.

- **Why:** evita redesenhar o wallpaper inteiro ou voltar ao BMP em cada movimento do rato.
- **Alternative considered:** cachear apenas o wallpaper. Rejeitado porque continuaria a obrigar a recompor sempre a base estática do desktop.

### 4. Manter o caminho de fallback atual

Se o BMP não puder ser aberto, parseado ou decodificado, o runtime cai para o desktop sólido atual sem bloquear a app nem corromper o frame.

- **Why:** o contrato com ficheiros do utilizador exige falha previsível e sem regressão funcional.
- **Alternative considered:** falhar a app quando o wallpaper é inválido. Rejeitado porque o wallpaper é decorativo, não requisito de arranque.

## Risks / Trade-offs

- **[Risco]** O novo blit syscall pode fazer o kernel voltar a ultrapassar o limite da floppy. → **Mitigação:** manter o kernel “format-agnostic”, com apenas blit de buffers já decodificados, e medir `kernel_sectors` em cada iteração.
- **[Risco]** A cache de surfaces aumenta o consumo de memória userland. → **Mitigação:** usar formato único simples, libertar/reutilizar buffers quando possível e limitar a feature ao runtime gráfico.
- **[Risco]** Dirty rect restore incorreto pode deixar artefactos visuais. → **Mitigação:** desenhar sempre a partir de `desktop_surface` antes de overlays dinâmicos e cobrir restore parcial com testes.
- **[Trade-off]** O primeiro carregamento do wallpaper continua a pagar parse + decode. → **Aceite** porque esse custo passa a acontecer uma vez por load em vez de em cada redraw.

## Migration Plan

1. Adicionar o descritor/ABI de surface blit ao runtime e ao dispatcher de syscalls.
2. Implementar o blit kernel-side direto para o backbuffer com clip rect.
3. Estender o runtime UI com surface cache derivada de BMP.
4. Atualizar o `WIN95UI` para recompor o desktop a partir da cache e reativar o background.
5. Atualizar documentação e regressões de runtime/UI.

Rollback: desligar a utilização do surface blit no `WIN95UI` e voltar ao desktop sólido sem remover o novo syscall, caso a integração inicial encontre problemas.

## Open Questions

- O formato do buffer de blit deve ser fixo (`XRGB8888`) ou aceitar um conjunto pequeno de formatos (`RGB888`/`XRGB8888`) desde a primeira versão?
- O `desktop_surface` deve incluir o relógio/taskbar dinâmicos ou apenas a base estática, deixando overlays dinâmicos fora da cache?
- Vale a pena reservar uma API explícita de invalidation/restore no runtime agora, ou basta começar com helpers locais ao `WIN95UI` e generalizar depois?
