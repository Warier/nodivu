# Interface de plugins no Estúdio

**Atualização 2026-10-13:** catálogo experimental em [nodivu-plugins](https://github.com/Warier/nodivu-plugins), com Ducking e Botão de fala separados. O host de desenvolvimento aceita API 1 e 2; a release original 0.2.0 aceita apenas API 1 e não contém este bootstrap. [Contrato API 2](PLUGIN-NATIVE.md): segunda entrada CLAP e widget momentary. Renovação/custódia documentadas no repo do catálogo.

O Nodivu desenha o bloco compacto e seu painel de configurações a partir do manifesto. O autor descreve a apresentação; o processamento continua no plugin CLAP ou no perfil de worker suportado. Alterar uma configuração envia o valor ao mesmo parâmetro e UUID usados pelo backend, sem recriar o dispositivo de áudio.

Esta extensão é implementada em outubro de 2026. Manifestos anteriores continuam aceitos. Para usar os campos novos, é necessário um host que inclua esta atualização: versões antigas rejeitam campos desconhecidos. `schema_version: 1` e a ABI CLAP permanecem iguais; não há uma nova ABI visual.

## Uso no aplicativo

- Arraste um cartão da biblioteca para o projeto ou clique para adicionar. As categorias podem ser recolhidas; a borda da biblioteca ajusta sua largura de 184 a 320 px. O botão ☰ recolhe para uma faixa de ícones.
- Clique no título ou em **⋯** para configurar o bloco. O painel pode ser arrastado pelo cabeçalho, recolhido, fechado e reaberto em **Inspetor**. **⌖** o posiciona no canto. Selecionar e mover o painel não modifica o grafo.
- Controles rápidos ficam no bloco; o painel contém todos os parâmetros, inclusive os não declarados visualmente, mais os controles de arquivo/captura fornecidos pelo host.
- Clique num fio para ver origem e destino, incluindo o número da entrada do Mixer. Botão direito abre **Remover conexão**. Nenhuma conexão é excluída somente por abrir o menu.
- **Sistema** reúne dispositivos, VB-CABLE, recuperação, atualização e logs. **Escuta** abre a configuração da cópia do áudio final nos fones.

Posição dos blocos e câmera continuam no documento do projeto. Seleção, aba aberta e posição do painel são estado temporário da interface. Valores de parâmetros são persistidos pelo contrato existente do projeto; o layout não altera a identidade de parâmetros, nós ou portas.

## Manifesto nativo

Exemplo de manifesto completo de **um plugin hipotético**. A biblioteca deve realmente expor os IDs 7, 8 e 9, com os limites indicados abaixo; adicionar campos ao JSON não cria parâmetros no DSP.

```json
{
  "schema_version": 1,
  "library": "meu-efeito.clap",
  "plugin_id": "org.exemplo.meu-efeito",
  "title": "Meu efeito",
  "description": "Processamento de exemplo, com ajustes separados do bloco.",
  "accent": "#e6aa68",
  "layout": "grid",
  "controls": [
    {
      "parameter": 7,
      "label": "Intensidade",
      "widget": "slider",
      "unit": "×",
      "page": "Som",
      "group": "Sinal",
      "help": "1× mantém a amplitude; 2× dobra a amplitude.",
      "quick": true
    },
    {
      "parameter": 8,
      "label": "Modo",
      "widget": "select",
      "unit": "",
      "page": "Opções",
      "group": "Comportamento",
      "options": [
        { "value": 0, "label": "Natural" },
        { "value": 1, "label": "Suave" }
      ]
    },
    {
      "parameter": 9,
      "label": "Tempo",
      "widget": "number",
      "unit": "ms",
      "page": "Som",
      "group": "Detalhes",
      "help": "Ajuste avançado deste efeito.",
      "advanced": true
    }
  ]
}
```

O exemplo pressupõe: parâmetro 7 contínuo 0..4, padrão 1; parâmetro 8 inteiro 0..1, padrão 0; parâmetro 9 contínuo 0..100, padrão 20. Esses metadados vêm de `clap.params`, não do manifesto. O exemplo compilável [Studio Gain](examples/clap-gain/plugin.json) usa somente o parâmetro 7, com aba, grupo, ajuda e controle rápido. Compilá-lo não exige mudar o aplicativo:

```powershell
./scripts/Prepare-DemoPlugin.ps1
```

Feche o app antes de substituir uma DLL já carregada e reabra depois. Mudar apenas a aparência exige editar o manifesto e reiniciar o app; não recompilar Rust, Electron ou a DLL. Alterar processamento exige recompilar somente o plugin. O empacotamento/local de plugins segue o [README](README.md#trabalhar-nos-plugins).

## Campos e validação

O manifesto UTF-8 tem limite de **64 KiB**. Objetos rejeitam campos desconhecidos. `library`, `plugin_id`, `title`, `description`, `accent`, `layout`, `controls` e `schema_version` mantêm as regras anteriores. Título: 1–64 caracteres; descrição: 1–240; ID: 1–128; cor: `#RRGGBB`; layout: `stack` ou `grid`. A biblioteca deve ser `.clap` dentro da pasta do pacote; o ID precisa corresponder ao plugin carregado.

Cada controle é um objeto:

| Campo | Obrigatório / padrão | Regra e comportamento |
|---|---|---|
| `parameter` | Obrigatório | ID numérico CLAP existente, único nos controles. Nunca posição do parâmetro na lista. |
| `label` | Obrigatório | 1–64 caracteres, texto exibido como rótulo acessível. |
| `widget` | Obrigatório | `slider`, `number`, `toggle` ou `select`. |
| `unit` | Obrigatório, pode ser `""` | Até 16 bytes UTF-8. Apenas sufixo visual; não converte o valor. |
| `group` | Obrigatório, pode ser `""` | Até 64 bytes UTF-8. Agrupa controles na mesma página; vazio usa “Controles”. |
| `page` | Opcional, `""` | Até 48 caracteres. Vazio usa “Ajustes”; a ordem das páginas é a primeira ocorrência. |
| `help` | Opcional, `""` | Até 320 caracteres. Explicação no painel; texto simples. |
| `quick` | Opcional, `false` | `true` mostra o controle também no bloco; até três controles rápidos declarados. |
| `advanced` | Opcional, `false` | Coloca o controle na seção recolhível “Avançado” do grupo. Não pode ser `true` junto com `quick`. |
| `options` | Opcional, `[]` | Exclusivo de `select`: de 2 a 32 objetos `{value, label}`. |

Há até 32 controles declarados e oito valores distintos de `page` por manifesto. O painel acrescenta os controles do host, como bypass. Parâmetros CLAP não declarados recebem um campo numérico genérico, para não desaparecerem da configuração.

Se nenhum controle declarar `quick: true`, o primeiro controle não avançado na posição inicial é mostrado como opção rápida por compatibilidade. Os demais ficam no painel. `grid` organiza os grupos em duas colunas; sliders, listas e detalhes avançados ocupam a largura disponível. O autor não define larguras absolutas, posição de portas ou tamanho arbitrário do bloco.

`min`, `max`, `default`, `readonly` e `stepped` vêm exclusivamente do CLAP. Um `toggle` exige parâmetro inteiro com intervalo exatamente 0..1. `select` permite opções numéricas finitas, distintas, dentro do intervalo; parâmetros inteiros exigem valores inteiros. Cada rótulo de opção tem 1–64 caracteres. Se um projeto contém um valor válido do parâmetro que não aparece na lista, o host preserva esse valor; não escolhe outra opção automaticamente. Prefira opções que cubram o domínio enumerado do parâmetro.

O passo dos campos numéricos/sliders é 1 para inteiros, ou `max((max-min)/1000, 0.000001)` para contínuos. Não há conversão implícita para dB, segundos, percentuais ou frequência. Campos somente leitura ficam desabilitados; telemetria extra não é inventada pelo frontend. Valores editados passam pelo validador existente antes de chegar ao áudio.

## Workers Python

O manifesto de worker também aceita `controls`, opcional, com os mesmos campos de apresentação. Sem ele, o campo original “Intensidade” é mantido. O perfil atual `effect-gain-v1` continua limitado ao parâmetro **7**, contínuo 0..4, e a no máximo um controle declarado. Adicionar páginas ou campos não expande o protocolo de processamento, não elimina seus buffers e não implementa um novo perfil de IA.

## Fronteira de segurança e responsabilidades

O pacote não fornece HTML, JavaScript, CSS, URLs, eventos, comandos de shell ou scripts de interface. Rótulos e ajuda são inseridos como texto. Widgets são implementados pelo host e seguem sua acessibilidade. Isto não torna uma DLL segura: plugins nativos continuam executando código nativo com a confiança e os requisitos de tempo real do contrato CLAP existente.

Selecionar arquivo, play/pause/seek e selecionar aplicativo são interfaces de capacidades específicas já suportadas, não campos arbitrários do manifesto. Texto livre para TTS, upload, chamadas de API, botões de ação próprios e editores gráficos exigem contratos adicionais; não devem ser simulados como parâmetros numéricos. Essa fronteira evita dar permissões de sistema a um rótulo visual.

## Testar o layout

`npm run dev:ui`, em `apps/nodivu-electron`, oferece **Controles de exemplo**, exclusivo da demonstração sem áudio. Use-o para testar abas Som/Opções/Ajustes, slider, seletor e campo avançado somente leitura no Linux ou Windows. O catálogo de demonstração não é um plugin DSP nem é carregado pelo backend real.

Os testes de integração do Electron verificam atualização bidirecional bloco/painel, posição de drop com zoom, painel contido na janela, biblioteca, menu de fios e preservação da geração de áudio. Os testes Rust verificam compatibilidade com manifestos antigos, opções inválidas e correspondência com limites do parâmetro. Ensaios reais do player e dos dispositivos são separados da demonstração.

## Gerenciamento local de pacotes

Gerenciador implementado em 2026-10-09: **Sistema → Plugins instalados** ou **Gerenciar plugins** na biblioteca. Explorar e Atualizações também oferecem o cliente do [catálogo assinado](PLUGIN-STORE.md); a origem pública ainda não está publicada.

**Adicionar pasta local** copia um pacote CLAP existente, com `plugin.json` na raiz, biblioteca `.clap` interna e arquivos auxiliares. Não executa seu código durante a cópia. Há confirmação de confiança antes da instalação; a validação é estrutural, não auditoria de código nem garantia de ABI. O backend continua responsável pelas verificações nativas ao reiniciar.

Limites iniciais: 256 MiB, 2048 entradas, profundidade de 12 subpastas e manifesto de 64 KiB. Links/arquivos especiais e referências fora do pacote são recusados. ZIP e `.nodivu-plugin` aceitam o manifesto na raiz ou em uma única pasta; verificação de CRC, tamanho descompactado, duplicação de caminhos e nomes inválidos ocorre antes de importar. Não importa fonte não compilada ou `worker.json`; workers já presentes podem ser listados e desativados. Importação não executa compiladores, scripts ou instaladores.

Até oito tipos ficam habilitados; um nono pacote é copiado desativado. Alterações são aplicadas na próxima inicialização do backend. Salve o projeto e feche/abra o Nodivu; o gerenciador não reinicia o áudio automaticamente. Desativação afeta futuras aberturas dos projetos que usam o pacote. Blocos ausentes são preservados no canvas/documento e seus caminhos ficam silenciosos; reative/reinstale para restaurar o processamento. Migrações incompatíveis de parâmetros ainda não são automáticas. Arquivos de projeto não são alterados pelo gerenciador.

Remover agenda a operação; **Cancelar remoção** desfaz antes de reiniciar. Na abertura seguinte, os arquivos saem de `plugins` e ficam em `plugin-archive` no perfil do app. Falha de movimentação mantém o pacote inativo e mostra aviso. Não há exclusão automática dessa cópia. No app distribuído, perfil em `%APPDATA%/Nodivu`; desenvolvimento usa `.local/electron-profile`. Pacotes embarcados `mp3`, `windows-audio`, `voice-cleanup`, `peak-limiter` e manifesto legado da raiz são protegidos de remoção, mas podem ser desativados. O rótulo de pacote do app não é verificação criptográfica de autoria.

Marcadores `.nodivu-disabled` e `.nodivu-removed` são reconhecidos pelo backend antes de ler o manifesto; isso permite desativar um manifesto quebrado pela UI mesmo com backend desconectado. Falhas recuperáveis de leitura/carga/metadados/preparação são registradas por pacote, sem impedir os demais tipos de iniciar. Isso não isola travamentos ou corrupção de memória dentro de DLLs nativas. Criação de instâncias sob demanda e isolamento de processos continuam pendentes. Não sobrescrevemos DLL em uso e não modificamos contratos DSP nesta fatia.

`NODIVU_PLUGIN_MANIFEST` explícito desabilita o gerenciador, para preservar ambientes de testes/desenvolvimento administrados externamente. A demonstração usa perfil isolado sem áudio. Testes cobrem importação, duplicação, caminhos inválidos, limite de tipos, remoção cancelável, arquivo preservado, falha de arquivamento e exclusão de pacotes desativados na API real. O smoke Electron abre o gerenciador e alterna um pacote no perfil temporário, verificando que a geração de áudio não mudou.


### Versões e documentação do pacote

O arquivo separado `nodivu-package.json` é opcional para pacotes legados, obrigatório para atualizar um ID já instalado. Não coloque seus campos no `plugin.json` de processamento. [Contrato completo e exemplo](PLUGIN-PACKAGE.md).

A versão nova é copiada para staging, conferida e agendada. Apenas na próxima abertura o app arquiva a versão anterior e ativa a nova, preservando a desativação. O registro permite retomar uma troca interrompida; erros mantêm um aviso e tentam restaurar a versão anterior. **Cancelar atualização** cancela antes da troca. **Voltar para…** agenda uma versão arquivada. Projetos não são migrados automaticamente entre contratos incompatíveis. Atualizações de pacotes embarcados gerenciadas por versão não são sobrescritas pela cópia do app.

**Detalhes** mostra README Markdown com títulos, listas, tabelas, código, links HTTPS e até 12 imagens locais de até 2 MiB cada. README limitado a 256 KiB. HTML é sanitizado; scripts, iframes, estilos, atributos de eventos e imagens externas não têm acesso ao renderer. Links relativos para outros documentos ainda aparecem sem navegação. README não pode acessar arquivos externos ao pacote nem executar ações do app.

Verificação: testes de ZIP hostil/CRC, atualização, cancelamento, retorno à versão anterior, interrupção de troca, versão alterada no staging e preservação de pacotes atualizados. Smoke Electron com README hostil/tabela/imagem local e áudio real. Não equivale a auditoria do código dos plugins importados.


## Atalhos personalizados

Controles declarativos graváveis podem receber atalhos locais/globais pelo usuário, por UUID do bloco. Read-only e momentary não entram nessa lista. Consulte o [contrato de ações e atalhos](HOTKEYS.md#contrato-para-autores-de-plugins). Nenhum pacote pode instalar teclas automaticamente.
