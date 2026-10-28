# Adicionar plugins e expandir a interface

## Plugin de áudio independente

Um novo efeito, fonte ou consumidor usa o contrato CLAP existente do backend. Não se adiciona uma classe C++ da interface para cada efeito. O pacote fornece `plugin.json`, binário CLAP, `nodivu-package.json`, README e recursos permitidos. O autor compila somente seu plugin; depois instala pasta/ZIP pelo gerenciador ou publica pelo catálogo existente.

Leia os contratos canônicos na raiz: [PLUGIN-NATIVE.md](../../PLUGIN-NATIVE.md), [PLUGIN-PACKAGE.md](../../PLUGIN-PACKAGE.md), [PLUGIN-UI.md](../../PLUGIN-UI.md) e [PLUGIN-STORE.md](../../PLUGIN-STORE.md). Eles definem ABI, limites, versões, portas, parâmetros, empacotamento e publicação. O Qt consome os mesmos contratos, sem alterar manifesto ou DSP para adaptar um plugin existente.

1. O gerenciador verifica o pacote e o coloca no diretório de plugins do perfil. Atualizações de DLLs em uso ficam adiadas.
2. Ao reiniciar, o backend descobre os pacotes e publica seus descritores em `plugins.list`.
3. `NodeCatalog` transforma descritores em entradas da biblioteca, portas e controles do inspetor.
4. A biblioteca adiciona a instância por `plugin_id`; UUIDs e validação pertencem ao backend.
5. Controles enviam alterações por ID de parâmetro. Momentary usa `plugin.gesture`, sem persistir o estado de pressão.

`appearance.controls` permite sliders, números, toggles, selects, momentary, abas, grupos, ajuda, controles rápidos, avançados e readonly. IDs dos parâmetros permanecem estáveis. Sidechain, fonte/consumidor, player, captura e worker são capacidades do backend; o frontend não inventa capacidades ausentes. Plugins desconhecidos/desativados viram placeholders e preservam dados/fios/recursos.

A loja usa os mesmos contratos dos pacotes locais. Não há execução de JavaScript, HTML ou DLL de interface fornecida pela loja. README não pode carregar recursos locais arbitrários ou rede diretamente; imagens da loja passam pela verificação TUF.

## Novo tipo de controle visual

1. Defina semântica e compatibilidade no contrato de UI. Se precisar de nova API do motor, documente a mudança no contrato do backend antes de integrar o controle.
2. Acrescente a representação em `contracts/node_descriptor.*`, mantendo defaults para manifestos antigos.
3. Implemente o widget no módulo do inspetor ou em `ui/controls/` quando compartilhado. Ele emite uma intenção e apresenta snapshots; não acessa DLL ou dispositivo. Reutilize os seletores/checkboxes e a conversão/formatação de `control_presentation.*`.
4. Se houver controle rápido, acrescente sua apresentação/interação em `features/graph/node_content.*`; `GraphScene` coordena seleção, portas e ligação ao controlador. O inspetor organiza `page`, `group`, `advanced` e `layout` sem exigir uma classe por plugin.
5. Teste comportamento, limites e liberação de interações ao perder foco/remover o nó. Evite testes que apenas repitam a construção do widget.

## Nova funcionalidade do aplicativo

Crie um módulo em `features/<nome>/` com controlador e UI quando ambos forem necessários. Reutilize serviços explícitos com ownership por QObject/RAII. Não introduza singleton mutável, I/O na pintura, filas ilimitadas ou regras de roteamento duplicadas na GUI. Adicione os arquivos ao CMake e use a formatação de quatro espaços da pasta.

Operações lentas precisam de progresso/cancelamento e fronteira clara de commit. Não capture widgets em workers; devolva dados com contexto QObject. O encerramento deve liberar gestos, terminar operações de forma limitada e aguardar o processo de áudio antes de entregar um instalador ao SO.

Para ações indisponíveis, reutilize `ui/controls/action_availability.*`: informe propósito e motivo em português. O helper mantém tooltip também em controles desativados e descrição acessível; títulos de pacotes são tratados como texto. `studio_panel_style.*` compartilha a apresentação de Sistema, Roteamento e atualização. `SessionController::activityChanged` informa ocupação da fila sem publicar um snapshot antigo como mudança do documento.
