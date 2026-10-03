# Nodivu

> Conecte fontes de áudio, ajuste o som em blocos e escolha para onde ele vai.

**Código aberto · MIT · Electron + Rust · Windows x64 · Em desenvolvimento**

Nodivu é um mixer e roteador local de áudio com uma interface de nós. Use o microfone, arquivos ou áudio de aplicativos como fontes; combine e processe esses sinais e envie o resultado para uma saída do Windows.

O projeto está em fase experimental. Este repositório contém o código necessário para montar o aplicativo, seus plugins principais e testes. Ainda não representa uma versão estável para produção.

## Por que Nodivu?

- **Roteamento visual:** arraste fios entre blocos, ramifique uma fonte e some sinais com o Mixer.
- **Áudio contínuo:** uma rota válida com dispositivos selecionados começa a funcionar automaticamente. Ajustes de ganho e conexões não exigem um botão de iniciar a cada mudança.
- **Fontes diferentes:** microfone, tom de teste, player MP3/WAV/M4A-AAC e captura de aplicativos Windows.
- **Projetos reutilizáveis:** salve o grafo, posições, câmera e referências aos recursos; reabra pelo histórico de recentes.
- **Plugins separados:** os plugins nativos podem ser recompilados e substituídos sem recompilar o aplicativo. O catálogo oferece aparência e controles declarativos simples.
- **Processamento local:** o áudio permanece no motor nativo. O Electron troca comandos e estado com o backend, não amostras de áudio.

## Como funciona

```text
Microfone ────────→ Ganho ──┐
Player de áudio ────────────┼──→ Mixer ──→ Saída Windows
Áudio de aplicativo ───────┘                  │
                                             ├─ Fone / alto-falante
                                             └─ CABLE Input, se instalado
                                                  ↓
                                           CABLE Output em outro app
```

Cada fio representa áudio. Uma saída pode alimentar vários blocos. Cada porta de entrada aceita um fio; para combinar fontes, conecte-as a entradas distintas do Mixer. Ganhos em série se acumulam: dois ajustes de 50% resultam em aproximadamente 25% da amplitude original.

O backend Rust administra dispositivos, grafo, processamento e plugins. Um processo privado atende o Electron por JSONL em stdin/stdout. Ele faz parte do aplicativo; não é necessário iniciar um CLI ou servidor manualmente.

## Plataformas e requisitos

| Ambiente | Situação atual |
|---|---|
| Windows x64 | Aplicativo completo e áudio WASAPI. Validado localmente em Windows 10 22H2, build 19045.6466. |
| Windows 11 x64 | Alvo do projeto; a validação em uma instalação limpa ainda está pendente. |
| Linux | Edição e demonstração da interface, sem áudio real. Execução Linux nativa ainda precisa de validação. |
| macOS / ARM64 | Sem suporte validado nesta etapa. |

Para compilar o app completo no Windows, instale:

1. **Node.js 22.12 ou superior**, com npm.
2. **Rust via rustup**, usando o host `x86_64-pc-windows-msvc`. O arquivo `rust-toolchain.toml` fixa a toolchain 1.98.1 e seus componentes; Cargo sozinho não instala as ferramentas C++ necessárias.
3. **Visual Studio 2022 Build Tools** ou Visual Studio 2022 com a carga **Desenvolvimento para desktop com C++**, MSVC x64 e Windows SDK. A configuração usada nos testes é MSVC 14.44 e SDK 10.0.26100.0; a captura precisa de `audioclientactivationparams.h`.
4. **Windows PowerShell 5.1 ou PowerShell 7**, além de acesso à internet no primeiro build para baixar as dependências fixadas.

O player usa Media Foundation do Windows. Edições Windows N podem precisar do componente de mídia correspondente. Python é opcional, apenas para experimentar o worker de exemplo. Qt, CMake, FFmpeg e VB-CABLE não são requisitos para o build normal.

## Começar no Windows

Depois de clonar ou extrair o código, abra um terminal na raiz do projeto:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-Electron.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Start-Electron.ps1
```

O primeiro comando compila o player e a captura nativos, compila o backend Rust, instala as dependências com `npm ci`, executa os testes Node e monta o aplicativo em **`.local/electron/nodivu-electron.exe`**. O segundo abre esse aplicativo. O parâmetro de execução de scripts vale somente para esse processo do PowerShell; não altera a política global.

Não é preciso baixar DLLs do Nodivu, copiar uma pasta de outra máquina ou instalar o cabo virtual para começar. O runtime Electron e os avisos das dependências acompanham o app montado. Feche a cópia do Nodivu dessa pasta antes de reconstruí-la para evitar arquivos em uso. Execute sem overrides `CARGO_TARGET_DIR` ou `CARGO_BUILD_TARGET`, pois a interface usa `target/release`.

### Primeiro teste de áudio

1. Adicione **Player de áudio** e **Saída** pela lista de blocos.
2. Selecione seu fone na Saída. Escolha no player um MP3, WAV ou M4A/AAC de até **10 minutos e 256 MiB**.
3. Arraste um fio da saída do player até a entrada do bloco Saída. Pressione **Play**; teste pause e a barra de posição.
4. Insira um bloco **Ganho** entre ambos e altere seu valor. O medidor deve acompanhar o sinal. Para retirar um fio, clique nele com o botão direito.
5. Para testar a voz, use **Entrada** com seu microfone → Ganho → Saída. Prefira fones para evitar que o microfone recapture os alto-falantes.

Ao selecionar dispositivos e conectar uma rota válida, o áudio fica pronto automaticamente. Play controla apenas o arquivo. Sem som, confira o aviso do bloco e a faixa de estado, a seleção do dispositivo, os fios, mute e bypass. Se um dispositivo desaparecer, o app não troca silenciosamente por outro.

Para som de um jogo ou navegador, adicione **Áudio de aplicativos** e selecione o processo. É possível usar mais de uma instância e misturá-las. A captura copia o áudio: o programa original continua tocando em sua saída habitual. Navegadores podem incluir subprocessos; abas individuais não são separadas. O modo de áudio do PC com exclusão do Nodivu ainda é experimental.

### Usar o resultado como microfone em outro programa

Com o **VB-CABLE instalado separadamente**, escolha **CABLE Input** no bloco Saída do Nodivu. No programa que vai receber o áudio, escolha **CABLE Output** como microfone. Os nomes correspondem aos dois lados do cabo: Nodivu envia para o lado de reprodução, e o outro programa recebe pelo lado de gravação.

O Nodivu não instala drivers nem altera os dispositivos padrão do Windows. Sem um cabo/driver virtual instalado, uma saída de fone não se transforma em microfone virtual. A recepção por outro processo foi testada localmente; testes específicos em Discord, jogos e outros consumidores ainda são necessários.

### Projetos e navegação

Use **Salvar**, **Abrir**, **Recentes** e o botão de pasta. A pasta sugerida é `Documentos/Nodivu/Projetos`; o caminho real respeita a configuração de Documentos do Windows. O arquivo `.nodivu.json` guarda referências aos áudios, não os incorpora. Ao compartilhar um projeto, disponibilize também os arquivos usados e revise os dispositivos no outro computador.

Arraste uma área vazia ou use o botão do meio para mover a câmera. Use Ctrl + roda do mouse ou os botões `+`/`−` para zoom, e **Enquadrar** para localizar os blocos. Abrir um projeto retoma a rota válida, mas não começa a tocar um arquivo automaticamente.

## Desenvolver a interface, inclusive no Linux

A interface usa **Electron 44.2.0, JavaScript, HTML, CSS e SVG**, sem React, TypeScript ou bundler. `main.cjs` gerencia janelas e o backend; `preload.cjs` expõe a ponte limitada; `ui/renderer.js`, `ui/blocks/`, `ui/style.css` e `ui/viewport.js` implementam o editor.

```sh
cd apps/nodivu-electron
npm ci
npm run dev:ui
npm run test:ui
```

`dev:ui` abre uma demonstração explicitamente identificada com dispositivos simulados. Serve para editar layout, blocos e interações sem Rust, MSVC, Windows ou dispositivos reais; **não processa áudio**. No Linux é necessária uma sessão gráfica e as bibliotecas de sistema exigidas pelo Electron. Não execute como root nem desative o sandbox para contornar falhas de ambiente.

Para desenvolver com o backend real no Windows, rode o build completo uma vez e depois `npm start` nessa pasta. Reinicie o app após mudar os arquivos. Ao alterar Rust ou plugins, recompile a parte correspondente; o build completo também atualiza a cópia em `.local/electron`.

## Organização do código

```text
apps/nodivu-electron/       Interface, processo principal, demonstração e testes
crates/
  nodivu-app-backend/       Host privado do app, catálogo e controle de plugins
  nodivu-core/              Modelos, validação, protocolos e projetos
  nodivu-engine/            Dispositivos WASAPI e execução do grafo
  nodivu-ipc/               Transporte de comandos limitado por JSONL
  nodivu-block/             Contrato interno de processamento
  nodivu-plugin-gain/       Ganho interno
  nodivu-plugin-tone/       Gerador interno de tom
  nodivu-clap-host/         Host CLAP e ponte opcional para workers
plugins/
  mp3-player/              Player real: decoder, transporte e adaptador CLAP
  windows-audio/           Captura real: aquisição, fila e adaptador CLAP
examples/                 Plugins pequenos e worker Python para estudo/testes
scripts/                  Build, inicialização e ensaios de desenvolvimento
third_party/clap-1.2.2/    Cabeçalhos e licença do contrato CLAP
```

`Cargo.lock` e `package-lock.json` fazem parte do código versionado. `target/`, `.local/`, `.tools/` e `node_modules/` são gerados. Esta árvore não inclui Qt, o CLI interativo antigo, documentos internos de planejamento, binários do instalador, drivers, perfis pessoais ou projetos salvos.

### Trabalhar nos plugins

Para recompilar apenas um plugin, a partir da raiz:

```powershell
./scripts/Build-Mp3Plugin.ps1
./scripts/Build-WindowsAudioPlugin.ps1
```

Cada comando compila somente sua biblioteca e copia o manifesto e o binário para `.local/plugins/` e `.local/electron/plugins/`. Feche o app antes e reabra depois. O player mantém o ID `org.nodivu.mp3-player` por compatibilidade dos projetos.

Os plugins nativos usam CLAP/ABI C e são carregados dentro do processo do backend. Podem ser escritos em uma linguagem capaz de exportar essa ABI; são código de confiança e não têm isolamento contra falhas nativas. O suporte atual é um subconjunto de CLAP, não um host universal de plugins de áudio.

Para estudar um efeito pequeno, leia [examples/clap-gain/plugin.c](examples/clap-gain/plugin.c) e seu [plugin.json](examples/clap-gain/plugin.json). `Prepare-DemoPlugin.ps1` compila e adiciona esse exemplo. O manifesto define ID, biblioteca, título, cor, layout e controles; sua validação está em [plugin.rs](crates/nodivu-app-backend/src/plugin.rs), e a interface genérica em [plugin.js](apps/nodivu-electron/ui/blocks/plugin.js). Cada pacote ocupa uma pasta, com caminho da biblioteca relativo ao manifesto. A leitura do catálogo ocorre ao iniciar o backend.

No player, [player/](plugins/mp3-player/player/) contém a lógica de áudio e [adapter/](plugins/mp3-player/adapter/) a integração. Os headers `file_player.h` e `capture_source.h` nos adaptadores descrevem as extensões privadas utilizadas pelo app. Alterar a aparência básica e os parâmetros pelo manifesto não exige mudar o renderer; controles especializados, como o transporte do player, ainda têm integração própria na interface.

O [worker Python](examples/python-worker/) demonstra processamento fora do callback em outro processo, com filas limitadas e atraso explícito. Para prepará-lo, use `./scripts/Prepare-PythonWorker.ps1 -Python 'caminho/do/python.exe'` e reinicie o app. Ele é um exemplo de ganho, não um modelo de IA ou serviço de TTS pronto. A referência do protocolo implementado fica em [worker_process/](crates/nodivu-clap-host/src/worker_process/) e no próprio exemplo.

## Verificação

### Diagnóstico de falhas

Desde 0.1.2, microfones comuns não precisam estar configurados em 44,1/48 kHz: PCM de 8/16/24/32 bits e float32 têm conversão automática, incluindo 16 kHz. Taxas altas e mais canais usam o conversor do Windows. Há limites de segurança (8–384 kHz, até 32 canais); a negociação depende do driver. Erros incluem formato observado e etapa. Se somente o microfone falhar, o player e as demais fontes continuam; use **Reabrir dispositivos** para tentar a entrada novamente.

Desde a versão 0.1.1, **Abrir logs de diagnóstico** abre a pasta de registros locais. No aplicativo instalado ela fica em `%APPDATA%\Nodivu\logs`; em desenvolvimento, em `.local/electron-profile/logs`. Há dois arquivos rotativos, `nodivu.log` e `nodivu.previous.log`, de até 1 MiB cada. Eles registram versão do app/Windows, inicialização, falhas do backend e descoberta de dispositivos. Nenhum áudio é gravado ou enviado automaticamente. Mensagens do Windows podem conter nomes, IDs ou caminhos; revise os arquivos antes de compartilhá-los.

Uma falha na descoberta de áudio não impede editar/salvar o grafo. A faixa superior mostra a causa e oferece **Tentar detectar novamente**. Se acabou de instalar o VB-CABLE, reinicie o Windows antes de repetir o teste. Aparecer nas opções do sistema não garante que todas as consultas ao dispositivo já estejam prontas.

### Testes

Na raiz, com as ferramentas de desenvolvimento instaladas:

```powershell
cargo fmt --all -- --check
cargo check --workspace --all-targets --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo test --workspace --all-targets --locked
```

Depois de `Build-Electron.ps1`, execute:

```powershell
./scripts/Test-Electron.ps1 -Project -Viewport -Capture
cd apps/nodivu-electron
npm test
```

O smoke verifica a interface e sua integração com o backend; sem `-Audio`, não abre streams de dispositivos. Ensaios que reproduzem/capturam áudio são separados e exigem endpoints explícitos. Veja [o ensaio de captura](plugins/windows-audio/README.md). O teste de formatos e limite do player é `Test-MediaPlayer.ps1 -Ffmpeg 'caminho/do/ffmpeg.exe'`; FFmpeg é ferramenta opcional de teste e não acompanha o app.

## Limites atuais e contribuições

Ainda faltam validação ampla em outros PCs, estabilidade prolongada e medição de latência ponta a ponta. O modo global de captura precisa de mais testes de exclusão. Recuperação automática de projetos, undo/redo e um SDK de plugins mais completo são próximos passos; não são promessas de funcionalidades já disponíveis.

### Atualizações e releases

A partir de **0.1.3-beta.1**, o app instalado consulta o canal de testes no GitHub Releases ao abrir (após 15 segundos) e a cada seis horas. No painel **Atualizações**, você pode verificar, baixar e escolher **Reiniciar e atualizar**. O áudio só é interrompido após sua confirmação; salve as alterações do projeto antes. Fechar o app não instala automaticamente. Falhas de rede não impedem usar a versão instalada.

Quem está na **0.1.2 ou anterior precisa instalar esta versão manualmente uma única vez**. O novo instalador NSIS adota a pasta da instalação Inno anterior. Projetos em Documentos/Nodivu/Projetos, perfil em `%APPDATA%\Nodivu` e VB-CABLE são preservados. Plugins de terceiros passam para `%APPDATA%\Nodivu\plugins`; adicione novos pacotes nessa pasta. Os plugins internos `mp3` e `windows-audio` são gerenciados pelo aplicativo. Depois do reinício, abra seu projeto nos Recentes.

O instalador de testes ainda não tem assinatura Authenticode. O download usa HTTPS e a biblioteca verifica o SHA-512 publicado no feed; isso não substitui uma assinatura do editor. O controle de publicação da conta GitHub é parte da confiança da distribuição.

Para gerar o instalador, rode `./scripts/Build-Release.ps1`. Ele monta o app, preserva licenças e inclui o instalador original do VB-CABLE básico (checksum fixado). A instalação do cabo é opcional, pelo botão no app, com confirmação e permissões solicitadas pelo próprio fornecedor. Atualizações do Nodivu não reinstalam nem reconfiguram o driver.

O resultado em `.local/releases` inclui `.exe`, `.exe.blockmap` e `beta.yml`. Publique esses arquivos **da mesma execução de build** em uma prerelease com tag correspondente à versão do `package.json`; não edite hashes manualmente. `--publish never` é o padrão: compilar não publica. GitHub hospeda o feed e os downloads; não é necessário servidor próprio.

### Fluxo de desenvolvimento

`main` recebe mudanças verificadas. Funcionalidades são desenvolvidas em branches curtas (`feat/...`, `fix/...`) e integradas após revisão e testes. Commits comuns não geram releases nem alteram automaticamente a versão distribuída. Tags `v...` e GitHub Releases ficam reservados para entregas escolhidas aos testadores, com versão incrementada, notas, build e validação. Não manter uma branch `develop` permanente neste estágio reduz divergências desnecessárias.


O grafo atual admite uma entrada de dispositivo e uma saída de dispositivo, até oito ganhos e oito instâncias externas no total. O catálogo é limitado a oito tipos externos. Múltiplos players e capturas de aplicativos usam instâncias externas independentes. Processamento é limitado deliberadamente para manter trabalho previsível; não há garantia de latência zero.

Contribuições são bem-vindas. Descreva o problema, a mudança e os testes feitos. Preserve IDs dos plugins e compatibilidade dos projetos; alterações no callback de áudio precisam manter buffers limitados, sem acesso a disco/rede, alocações ou esperas por comandos. Não troque dispositivos, volumes globais ou padrões do Windows para fazer um teste passar. Testes com dispositivos simulados não demonstram funcionamento em hardware.

## Licença

[MIT](LICENSE): uso, modificação e distribuição, inclusive comercial, mantendo os avisos da licença. Consulte também os [avisos de dependências](THIRD-PARTY-NOTICES.md). CLAP, Electron e seus componentes mantêm suas licenças próprias; nenhum direito sobre marcas de terceiros é transferido.
