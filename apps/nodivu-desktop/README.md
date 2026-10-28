# Nodivu Desktop

Frontend principal do Nodivu, escrito em C++20 e Qt Widgets. O código em [`apps/nodivu-electron`](../nodivu-electron/README.md) permanece disponível para comparação. O motor de áudio é Rust e se comunica com a interface por JSONL em stdin/stdout. O pacote Qt não depende de Node, Electron ou Chromium.

## Compilar e executar

Requisitos para Windows x64: PowerShell 7, Rust conforme [`rust-toolchain.toml`](../../rust-toolchain.toml), MSVC 2022 com ferramentas C++ x64 e Windows SDK, CMake 3.25+ e Ninja. Qt 6.12.0 usa Core, Gui, Widgets, Network e Concurrent; Qt Test e OpenGLWidgets são usados apenas no desenvolvimento e nos ensaios.

Execute na raiz do repositório:

```powershell
./scripts/qt/Bootstrap-Dependencies.ps1 -Installer
./scripts/Build-Desktop.ps1 -Test
./scripts/Start-Desktop.ps1
```

O bootstrap baixa dependências explicitamente, com versões e SHA256 definidos em [`dependencies.json`](../../scripts/qt/dependencies.json). O CMake não baixa dependências. O build completo compila o backend, os plugins principais e a interface; para recompilar somente o frontend, use `./scripts/Build-Desktop.ps1 -FrontendOnly -Test`.

O kit Qt fica em `.tools/Qt` e o build em `.local/qt-build/release`. Os testes de integração precisam do backend em `target/release/nodivu-app-backend.exe` e dos pacotes compilados em `.local/plugins/{mp3,windows-audio,voice-cleanup,peak-limiter}`. Usam perfis temporários e falham explicitamente se essas dependências estiverem ausentes.

## Perfis e projetos

O perfil de desenvolvimento fica em `.local/qt-profile`. A instalação normal usa `%APPDATA%/Nodivu`, e os projetos ficam em Documentos/Nodivu/Projetos. A versão portátil usa `portable-profile` ao lado do executável quando iniciada por `Nodivu Portable.cmd` ou `nodivu.exe --portable`.

Qt e Electron compartilham o formato do perfil instalado. Para compará-los, use perfis separados; não execute ambos sobre o mesmo perfil. O Qt recusa a abertura instalada quando detecta o Electron do mesmo usuário em execução.

Projetos preservam grafo, parâmetros, posições, câmera e referências a recursos, inclusive plugins ausentes. Nomes personalizados das instâncias ficam no arquivo auxiliar `.ui.json`; copie esse arquivo junto com o projeto. Nenhum projeto ou dispositivo é escolhido automaticamente na inicialização. Abrir um projeto retoma suas seleções e prepara os arquivos, sem iniciar a reprodução do Player. A perda de um dispositivo não redireciona áudio para outro.

## Trabalhar no Estúdio

Adicione blocos pela biblioteca ou pelo menu de contexto do canvas e conecte saída → entrada. Uma saída pode ramificar; cada entrada recebe um fio. Para somar fontes, use entradas distintas do Mixer. Dispositivos e arquivos são selecionados nos controles dos blocos; opções adicionais ficam no inspetor. O painel Roteamento oferece uma alternativa com controles nativos e teclado.

| Ação | Atalho ou gesto |
|---|---|
| Salvar / Salvar como | Ctrl+S / Ctrl+Shift+S |
| Desfazer / Refazer | Ctrl+Z / Ctrl+Shift+Z |
| Buscar tipos na biblioteca | Ctrl+F |
| Localizar instâncias no projeto | Ctrl+Shift+F |
| Buscar comandos dos menus | Ctrl+Shift+P |
| Abrir Roteamento | Ctrl+Shift+R |
| Renomear instância | F2 |
| Selecionar vários blocos | Ctrl+clique ou Shift+arraste |
| Duplicar seleção no canvas | Ctrl+D |
| Navegar pelo canvas | Espaço+arraste |
| Ajustar zoom | Ctrl+roda |

Editar reúne organização em linha/coluna e remoção em grupo. Arquivo → Modelos de cadeias reutiliza conjuntos de blocos; dispositivos, aplicativos e arquivos precisam ser escolhidos novamente. Exibir reúne minimapa, blocos compactos, alto contraste e escala da interface. Sistema reúne configurações de Áudio, Projeto e Aplicativo.

## Organização do código

| Diretório | Responsabilidade |
|---|---|
| `src/app/` | Inicialização, composição e ferramentas de diagnóstico |
| `src/backend/` | Processo Rust e transporte JSONL assíncrono e limitado |
| `src/contracts/` | Descritores, respostas e representação de estado |
| `src/features/` | Workspace, grafo, biblioteca, inspetor, projetos, plugins, loja e atualizações |
| `src/platform/` | Caminhos, perfil, instalação e adaptadores Windows |
| `src/security/` | JSON estrito e verificação de metadados assinados |
| `src/ui/` | Tema, controles e diálogos compartilhados |
| `tests/` | Testes de fronteira, falhas e integração com o backend |

Widgets apresentam estado e emitem intenções; o backend valida o roteamento e processa áudio. A GUI não carrega DLLs de plugins nem recebe PCM. Disco, rede e extração usam workers com limites; pintura não faz I/O. APIs Win32 ficam nos adaptadores de plataforma.

Plugins e controles declarativos seguem o [guia de extensão](EXTENDING.md). A configuração do canal de atualização está em [UPDATES.md](UPDATES.md). A versão do aplicativo vem de [`VERSION`](VERSION), e as dependências e licenças estão em [THIRD-PARTY.md](THIRD-PARTY.md).

## Empacotar e verificar

```powershell
./scripts/Build-Release.ps1 -SkipBuild
```

Após o build, gera staging, ZIP portátil, instalador NSIS por usuário, licenças e inventário SHA256 em `.local/qt-packages`. Qt e CRT acompanham o aplicativo. Qt Test e ferramentas de diagnóstico não entram no pacote. O comando não publica nem instala automaticamente.

O instalador verifica os arquivos antes de trocar a instalação, preserva dados desconhecidos e plugins locais e permite recuperar uma transação interrompida. Recusa a operação com o aplicativo em uso. Ao substituir uma instalação Electron, arquiva o runtime anterior; isso não remove o código Electron do repositório. Backups `.nodivu-backup-*` ficam ao lado da instalação e precisam de limpeza manual após conferência.

`Build-Desktop.ps1 -Test` executa as suítes CTest de protocolo, workspace, projetos, lifecycle, segurança, pacotes, loja, reprodução, atualização e instalação. [`Test-PackagedDesktop.ps1`](../../scripts/qt/Test-PackagedDesktop.ps1) verifica o staging e a inicialização sem SDK no PATH. [`Test-WindowsAcceptance.ps1`](../../scripts/qt/Test-WindowsAcceptance.ps1) verifica um pacote extraído usando perfil temporário e o SHA256 esperado de `distribution.json`.

Windows 10 x64 e Windows 11 x64 são os alvos do projeto. Há validação local em Windows 11; a validação em Windows 10 e em instalação limpa permanece pendente. Leitor de tela, DPI físico, áudio com dispositivos distintos e estabilidade prolongada exigem ensaios próprios. O aceite da migração para Qt não representa cobertura completa dessas combinações. O canal público de atualização do aplicativo ainda precisa ser configurado.
