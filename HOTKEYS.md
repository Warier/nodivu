# Atalhos de teclado — primeira versão Windows

Disponível no desenvolvimento **0.2.1-dev.2**, posterior à release 0.2.0.
Nenhuma combinação fica ativa por padrão. Não altera volumes nem atalhos do Windows.

## Configurar e testar

1. Abra seu projeto, selecione saída, conecte o Player e escolha um MP3, WAV ou M4A válido.
   Confirme que Play funciona normalmente e salve o projeto para preservar os UUIDs.
2. Abra **Configurações → Atalhos de teclado…** no menu superior, ou **Sistema → Atalhos**.
   No inspetor, **Atalhos deste bloco** filtra a lista pelo bloco selecionado.
3. Escolha `Player de áudio [identidade] · play / pause`, pressione **Ctrl+Shift+F8**
   no campo Combinação e selecione **Global**. Clique em **Adicionar atalho**.
4. Feche a configuração. Minimize o Nodivu, abra outro programa e toque a combinação.
   O primeiro acionamento reproduz; o segundo pausa. Reabra a configuração para consultar
   o estado e testar a ação individualmente. Um conflito aparece também no botão em Sistema.
5. Para confirmar persistência, salve o projeto, feche/reabra o app e abra o mesmo projeto.
   As combinações são registradas quando os blocos correspondentes ficam disponíveis.

**Testar** executa a ação, não simula uma tecla física nem prova compatibilidade com um jogo.
Durante a configuração os atalhos ficam suspensos para não disparar enquanto você os grava.
Fechar a janela de configuração retoma os habilitados. O Nodivu precisa continuar aberto;
fechar o aplicativo encerra áudio e remove os registros. Não há bandeja/execução após fechar.

## Ações disponíveis

| Destino | Ações |
|---|---|
| Aplicativo | Mostrar/restaurar janela |
| Áudio | Alternar silêncio geral, silenciar, restaurar som, alternar Escuta local |
| Player | Reproduzir, play/pause, pausar, recomeçar, parar e voltar ao início |
| Ganho | Definir dB, somar/subtrair dB, alternar bypass |
| Tom | Alternar geração |
| Plugins | Alternar controles booleanos/bypass, definir valores/opções, incrementar controles numéricos |

Escuta requer que os fones já tenham sido escolhidos na interface. O atalho não escolhe
outro dispositivo. Player sem arquivo, desconectado, em bypass, silenciado ou com saída
indisponível mostra a causa; não abre um seletor de arquivo sobre seu jogo. Pausar/parar
continua disponível para uma reprodução existente. Alterar ganho/parâmetro usa o mesmo
validador dos controles visuais e marca o projeto como alterado; transporte é transitório.
Incrementos respeitam os limites do controle. Valores usam a unidade original (dB, ms etc.).

No **Botão de fala**, configure o modo **Alternar** e associe o controle **Aberto (alternar)**.
O modo Segurar continua sendo local ao botão focado. **Não há push-to-talk global por soltura
de tecla nesta versão**; não é seguro inferir a soltura a partir de callbacks de pressionamento.

## Combinações, mouse e conflitos

- Escopo **Global**: registro `Electron.globalShortcut` no processo principal, sem depender
  de foco do canvas. Escopo **No Nodivu**: `before-input-event` da janela principal, sem
  registro global. Mesma combinação não pode ser associada duas vezes, mesmo em escopos distintos.
- Teclas A–Z/0–9 e navegação precisam de Ctrl ou Alt. F1–F24 (exceto F12) e teclas de mídia
  podem ser usadas sem modificador. Ctrl/Control são equivalentes; ordem/capitalização normalizadas.
- Recusamos Win/Meta, F12, Alt+F4, Alt+Tab, Alt+Esc, Alt+Space, Ctrl+Esc, Ctrl+Shift+Esc,
  Ctrl+Alt+Delete e combinações comuns de edição/menus, incluindo Ctrl+S/O/C/V/X/Z/A.
  Isso é uma política conservadora do Nodivu; não é uma lista completa do que cada outro app usa.
- Registro recusado fica visível como conflito; não tomamos o atalho de outro aplicativo.
  Feche o conflito ou escolha outra combinação, depois feche a configuração para tentar novamente.
- Ctrl+Alt pode equivaler a **AltGr**. Prefira Ctrl+Shift+F8 ou F13–F24 para não interferir
  em digitação de caracteres. A tecla Fn depende do teclado/firmware e não é capturada separadamente.
- Macro de mouse: no software do fabricante, mapeie o botão para F13, F14 etc. e associe
  essa tecla no Nodivu. **Mouse4/Mouse5 e roda não são capturados diretamente.** Não instalamos
  hooks globais de teclado/mouse nem drivers, e não elevamos privilégios para contornar jogos.
- Jogos com captura exclusiva/anti-cheat, sessões remotas, telas protegidas e processos com
  outros privilégios precisam de teste específico. Não há promessa de funcionamento em todos.

## Persistência e falhas

`%APPDATA%/Nodivu/hotkeys.json` na execução empacotada; desenvolvimento usa seu próprio perfil.
Versão 1, até 64 associações/64 KiB. Configuração é preferência deste computador, separada
do arquivo de projeto. Compartilhar um projeto não instala atalhos globais em outro PC.

Identidade da ação de bloco: **UUID / plugin_id (ou kind interno) / controle-operação**.
Não há associação pelo nome nem pela ordem. Abrir outro projeto, remover/desativar plugin
ou perder o backend inativa os atalhos correspondentes e libera registros. Reabrir o projeto
original permite reaproveitá-los; duplicar um bloco produz uma identidade nova.

Gravação temporária seguida de substituição; erro de escrita preserva a configuração ativa.
JSON corrompido não é executado e permanece no disco. Ao salvar uma substituição explícita,
criamos uma cópia `.preserved-<tempo>` antes. Desativar tudo ou uma associação é reversível.
Exclusão remove apenas a associação, nunca bloco/arquivo de áudio/projeto.

Uma ação em andamento por vez, sem fila de acionamentos atrasados. Ações durante operação
de projeto/diálogo são recusadas com causa. Resposta do renderer tem prazo de 7 s; backend
mantém seus próprios prazos. Filtramos repetição local e callbacks com menos de 250 ms.
Não existe transporte PCM no IPC de atalhos; o caminho de áudio e sua geração são preservados.

## Contrato para autores de plugins

Não é necessário modificar/recompilar o plugin para controles declarativos já existentes.
O host descobre capacidades a partir de `plugins.list`, controles validados e grafo atual:

- `toggle` gravável vira alternância; `slider`/`number` vira definir/incrementar;
  `select` vira definir um valor permitido, respeitando limites e stepped.
- Controles read-only e `momentary` são excluídos. Campos de dispositivo/arquivo não viram
  ações que mudem destino ou leiam caminhos arbitrários.
- `file_player` oferece Play; `file_transport` acrescenta pause/seek para as ações compostas.
- IDs de plugin/parâmetro devem permanecer estáveis em atualizações. Ação removida fica
  indisponível; valores persistidos incompatíveis são rejeitados, nunca reinterpretados.
- Pacotes não registram suas próprias teclas e não contêm código JS do host. Somente o usuário
  cria associações. Não há execução de shell, script, URL ou comando arbitrário de um manifesto.

Implementação: `hotkeys.cjs` (registro/persistência), `app-menu.cjs` (menu nativo),
`ui/hotkeys.js` (configuração), `ui/hotkey-actions.mjs` (catálogo/valores), renderer (aplicação
pelos comandos existentes). ABI CLAP, schema do grafo e DSP não mudaram nesta entrega.

## Validação e próximos passos

`npm test` inclui normalização/reservas, duplicatas, conflitos, persistência, arquivo corrompido,
falha de escrita, suspensão, bloco ausente, concorrência e valores dos controles.
Pacote local: `nodivu-electron.exe --smoke-test --test-hotkeys` usa perfil temporário, áudio
WASAPI/player reais, registro global Windows e disparo explícito pelo gerenciador com janela
minimizada; também envia uma tecla local ao Electron. Não é simulação de teclado global do SO.

Teste manual restante: tecla física fora do app, duas instâncias do player com atalhos distintos,
repetição/soltura, chamadas/jogos e seu mouse. Evoluções: PTT global com key-up/lease seguro,
botões de mouse diretos, presets/importação explícita e gestão de contextos mais rica.

Referências de implementação:
[Electron globalShortcut](https://www.electronjs.org/docs/latest/api/global-shortcut/),
[Accelerators](https://www.electronjs.org/docs/latest/api/accelerator),
[Microsoft RegisterHotKey](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey).
