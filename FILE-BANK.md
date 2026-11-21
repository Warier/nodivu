# Banco de arquivos de áudio — host API 3

Contrato opcional implementado pelo **Soundboard**, incluído no Qt e no Electron a partir da versão de desenvolvimento 0.2.3-dev.1. O player individual e sua extensão file-player/1 não mudam; projetos sem `slot` continuam compatíveis.

## Transporte de áudio e lifecycle

O plugin implementa CLAP 1.2.2, Windows x64, fonte com zero entradas e uma saída estéreo float32 planar. O host fornece 48 kHz e blocos de 1 a 480 frames. As vozes são somadas dentro do plugin; uma só porta segue para o grafo. Nenhum PCM passa pelo frontend ou pelo protocolo de comandos. O soundboard não adiciona uma janela/fila de áudio entre os nós. Isso **não** significa latência ponta a ponta zero: buffers de dispositivo, escalonamento e latência dos demais plugins continuam existindo.

O host consulta `get_extension("org.nodivu.file-bank/1")`. A estrutura ABI está em [file_bank.h](plugins/soundboard/adapter/file_bank.h); layout, ordem e assinaturas precisam corresponder exatamente. Ponteiro/strings emprestados permanecem válidos durante a chamada, e a estrutura até destruir a instância. Implementações devem conter exceções/pânicos antes de voltar à ABI.

| Função | Contrato |
|---|---|
| `count(plugin)` | Quantidade constante, entre 1 e 10. Índices começam em zero. |
| `load(plugin, slot, utf8_path)` | Copia o caminho antes de retornar; inicia decode assíncrono. `NULL` limpa apenas esta posição. `true` aceita a solicitação, não confirma decode concluído. |
| `trigger(plugin, slot)` | Reinicia a voz desta posição no começo. Não altera as demais. Só aceita arquivo pronto/playing/ended/paused. |
| `stop(plugin, slot)` | Para e volta ao início. Não libera o arquivo. Vazio já parado é sucesso. |
| `status(plugin, slot)` | 0 vazio; 1 carregando; 2 pronto; 3 tocando; 4 terminou; 5 parado/pausado; negativo para erro de decode. |
| `position(plugin, slot)` / `duration(plugin, slot)` | Milissegundos, telemetria transitória; zero se indisponível. |

Essas funções são chamadas na thread owner/main CLAP, **nunca no callback DSP**. `load` pode alocar/controlar um worker, mas não espera pelo decode. A thread de áudio só usa ponteiros estáveis, scratch fixo e atomics; não faz I/O, logs, locks ou liberação de PCM. O player compartilhado usa hazard pointer e coleta no decoder. Limpar um slot não destrói seu `Player` enquanto o DSP pode acessá-lo; teardown vem após parar o áudio. `reset` interrompe todas as vozes sem apagar arquivos.

No plugin incluído: até 60 s/32 MiB por posição, MP3/WAV/M4A-AAC mono ou estéreo, decode/resampling Media Foundation. Dez sons máximos retêm aproximadamente 220 MiB PCM, mais buffers temporários. Os decoders são criados sob demanda por posição. O limite original do player individual continua 600 s/256 MiB. Cancelamento é cooperativo: uma chamada Media Foundation bloqueada não é abortada à força. A soma é linear a 0 dB; use Ganho/Limitador se a mistura saturar.

## Catálogo, comandos e estado

`plugins.list` anuncia `file_bank_slots: 10`. Pacotes com esta capacidade declaram `host_api: 3`; hosts 1/2 devem recusar instalação. O host 3 continua aceitando pacotes 1/2. A UI não executa código/HTML do plugin: usa o controle de banco nativo de cada frontend.

```json
{"command":"plugin.bank","params":{"node_id":"UUID do bloco","slot":0,"action":"load","path":"C:/Sons/aplauso.wav"}}
{"command":"plugin.bank","params":{"node_id":"UUID do bloco","slot":0,"action":"trigger"}}
{"command":"plugin.bank","params":{"node_id":"UUID do bloco","slot":0,"action":"stop"}}
{"command":"plugin.bank","params":{"node_id":"UUID do bloco","slot":0,"action":"clear"}}
```

Os exemplos omitem o envelope JSONL normal (`protocol_version`, `id`). Não há `expected_revision`: são comandos transitórios, sem alteração de topologia. Argumentos desconhecidos, slot fora do limite, instância ausente ou incapaz são erros. Só `load` aceita/requer `path`; o host exige arquivo local absoluto existente, extensão aceita e tamanho limitado. Caminhos UNC e de namespace Windows que começam com duas barras são recusados nesta capacidade. A verificação do arquivo e o decode acontecem fora do DSP. A fila de controle por instância é limitada a 32 comandos; lotação é erro recuperável.

`session.snapshot.plugin_states[UUID].bank` contém uma entrada por posição:

```json
{"slot":0,"assigned":true,"file":"C:/Sons/aplauso.wav","status":2,"position_ms":0,"duration_ms":2400,"error":""}
```

`assigned` distingue intenção recebida (incluindo clear com caminho vazio) de estado recém-criado que ainda pode usar a referência salva do projeto. A UI não ressuscita caminhos limpos. `error` reporta falha de comando; status negativo reporta falha do decoder. Falha D05 da instância também bloqueia todas as suas posições. A UI só habilita tocar com arquivo pronto, rota válida, saída ativa e sem silêncio/bypass.

## Projeto e atalhos

Cada referência usa o mesmo `node_id`, com um `slot` opcional:

```json
{"resources":[
  {"node_id":"UUID do bloco","slot":0,"path":"sons/aplauso.wav"},
  {"node_id":"UUID do bloco","slot":9,"path":"sons/risada.m4a"}
]}
```

A chave de unicidade é `(node_id, slot)`. `slot` precisa ser inteiro 0–9 e caber na capacidade do plugin instalado; referências sem slot pertencem ao player legado. O arquivo de projeto continua versão 1, com extensão opcional estrita: hosts anteriores não conseguem abrir projetos que usem esse recurso. Nada de PCM, cursores ou estado de reprodução é salvo. Paths relativos são resolvidos pelo frontend contra a pasta do projeto. Plugins ausentes preservam as referências. Abrir prepara todos os arquivos, sem tocar automaticamente. Arquivos não são copiados/embutidos no projeto.

Ações por posição: `UUID/org.nodivu.soundboard/bank.0.trigger` e `.stop`, até índice 9. Configurações ficam no perfil local `hotkeys.json`; o UUID salvo mantém a associação ao reabrir. Qt e Electron têm perfis distintos, portanto os atalhos precisam ser configurados em cada edição. O preset opcional associa Ctrl+Shift+1…0 e preserva atalhos alheios; conflitos são recusados antes de gravar. As teclas podem ser personalizadas. Macros podem enviar F13–F24. Não são registrados números simples globalmente.

Para teclas quase simultâneas, somente triggers de soundboard podem aguardar outro trigger: no máximo dez, prazo de 250 ms, sem acúmulo após diálogos ou travamentos. Cada comando é serializado no controle; as vozes resultantes tocam simultaneamente. Não é um agendador com alinhamento por amostra.

## Compilar e verificar

- Plugin: `./scripts/Build-SoundboardPlugin.ps1` (MSVC x64/Windows SDK).
- DSP/lifecycle: `./scripts/Test-Soundboard.ps1`; `-Asan` verifica os caminhos exercitados com AddressSanitizer.
- Backend/decoder: `node scripts/Test-SoundboardLive.cjs`. Com `--output "ID opaco"`, testa WASAPI; `--actor caminho/windows_audio_actor.exe` acrescenta receptor independente por processo.
- Electron: `npm test` em `apps/nodivu-electron`; pacote com `./scripts/Build-Electron.ps1`. Smoke opcional: defina `NODIVU_SOUNDBOARD_OUTPUT` e execute o pacote com `--smoke-test --test-soundboard` (perfil temporário, seleção de arquivo simulada, áudio real).
- Qt: `./scripts/Build-Desktop.ps1 -Test`. `hotkey_tests soundboardProjectSlotsPresetAndClear` cobre persistência/clear/preset com backend real. Com `NODIVU_SOUNDBOARD_OUTPUT` acrescenta dois atalhos com janela minimizada e saída real; sem essa variável não abre dispositivo.

Testes sintéticos de DSP não provam funcionamento em outros PCs. Teclas físicas dentro de jogos/chamadas, dez decoders sob carga prolongada e Win11 limpo permanecem validações adicionais.
