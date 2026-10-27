# Nodivu

Mixer e roteador local de áudio com um editor de nós. O frontend em desenvolvimento usa **Qt Widgets/C++20**; o motor de áudio continua em Rust, sem alterações nesta migração. Código aberto, licença MIT, Windows x64.

## Compilar e executar

Requisitos: PowerShell 7, Rust conforme `rust-toolchain.toml`, Visual Studio 2022 com C++ x64/Windows SDK, CMake e Ninja. O kit Qt 6.12.0 e NSIS são baixados explicitamente com versões e SHA256 fixados. Node/npm não participam do build ou da execução Qt.

```powershell
./scripts/qt/Bootstrap-Dependencies.ps1 -Installer
./scripts/Build-Desktop.ps1 -Test
./scripts/Start-Desktop.ps1
```

O build completo compila o backend existente, os quatro plugins principais e o frontend. Para editar somente a interface, use `./scripts/Build-Desktop.ps1 -FrontendOnly -Test`. Não há arquivos `.ui` nem editor gráfico obrigatório: widgets, layout, tema e cena são definidos por código C++.

```powershell
./scripts/Build-Release.ps1 -SkipBuild
```

Gera ZIP portátil, instalador NSIS por usuário, licenças e inventário SHA256 em `.local/qt-packages`. Não publica nem instala automaticamente. O ZIP inclui `Nodivu Portable.cmd`, que usa perfil separado ao lado do executável. A instalação normal usa `%APPDATA%/Nodivu`; projetos ficam em Documentos/Nodivu/Projetos.

[Guia técnico e testes](apps/nodivu-desktop/README.md) · [Extensão de plugins e controles](apps/nodivu-desktop/EXTENDING.md) · [Atualizador](apps/nodivu-desktop/UPDATES.md) · [Matriz de aceite](apps/nodivu-desktop/ACCEPTANCE.md).

## Usar o Estúdio

Adicione blocos na biblioteca e conecte suas portas. Uma saída pode ramificar; cada porta de entrada recebe um fio. Use entradas distintas do Mixer para somar fontes. O inspetor apresenta controles definidos pelos descritores dos plugins.

- **Entrada, tom e captura de aplicativos:** fontes locais de áudio; a captura pode incluir subprocessos.
- **Player:** MP3, WAV e M4A/AAC até 10 minutos/256 MiB; reprodução, pausa e busca.
- **Ganho, Mixer, limpeza de voz e limitador:** processamento no backend, sem PCM passando pela interface.
- **Saída e Escuta:** dispositivos selecionados por IDs do backend, sem alterar padrões ou volumes do Windows.
- **Projetos:** grafo, posições, câmera e referências aos arquivos; recentes, recuperação e plugins ausentes preservados.
- **Gerenciar plugins:** importação de pasta/ZIP, versões, ativação, remoção adiada, catálogo autenticado e consulta offline.

Rotas válidas entram em operação automaticamente ao selecionar dispositivos e conectar os blocos. Abrir um projeto prepara os arquivos, mas não inicia a reprodução. Se um endpoint desaparecer, não há troca automática para outro. Para começar, conecte Player → Saída, escolha explicitamente um dispositivo e um arquivo, depois pressione Play. Prefira fones ao usar o microfone.

O painel **Roteamento** (`Ctrl+Shift+R`) permite navegar por blocos e conexões com controles nativos e teclado. O grafo oferece pan, zoom e enquadramento. O áudio e os plugins nativos ficam no processo Rust; a interface comunica intenções por JSONL limitado em stdin/stdout.

## Organização

```text
apps/nodivu-desktop/
  src/app/           Inicialização e composição
  src/backend/       Processo e transporte JSONL
  src/contracts/     Descritores e respostas da API
  src/features/      Grafo, inspetor, projetos, plugins, loja e atualizações
  src/platform/      Perfil, instalação e adaptadores Windows
  src/security/      JSON estrito e metadados autenticados
  src/ui/theme/      Cores, medidas e aparência
  tests/             Fronteiras, falhas e integração com Rust real
crates/              Backend, domínio, motor WASAPI e host CLAP existentes
plugins/             Player, captura, limpeza de voz e limitador
scripts/qt/          Dependências, build, pacote e ensaios nativos
third_party/         Fontes e licenças das dependências incorporadas
apps/nodivu-electron/ Referência da interface anterior durante o aceite
```

Plugins independentes usam CLAP/ABI C e manifestos declarativos. Um novo plugin compatível pode ser instalado sem recompilar o host. [Contrato nativo](PLUGIN-NATIVE.md), [pacotes](PLUGIN-PACKAGE.md), [loja](PLUGIN-STORE.md) e [UI declarativa](PLUGIN-UI.md). DLLs de terceiros executam no backend e ainda não possuem isolamento contra crashes.

## Plataformas e validação

Windows 10 x64 e Windows 11 x64 são os alvos obrigatórios. Qt 6.12 é mantido explicitamente por compatibilidade com Windows 10; futuras trocas de versão precisam revisar esse requisito. Linux/macOS exigirão adaptadores e validação próprios, inclusive de áudio, e não são plataformas entregues nesta etapa.

Os testes locais cobrem protocolo, persistência/recuperação, plugins, loja/TUF, player, atualizador e instalação transacional. Ensaios de áudio exigem endpoints explícitos. O backend continua sujeito aos seus gates existentes:

```powershell
cargo fmt --all -- --check
cargo check --workspace --all-targets --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo test --workspace --all-targets --locked
```

A [matriz de aceite](apps/nodivu-desktop/ACCEPTANCE.md) distingue implementação de validação. Faltam Windows 10, PC limpo, aceite visual/DPI/leitor de tela, medições completas de desempenho e operação do canal de atualização Qt. O frontend Electron permanece como referência até esses gates; isso não é uma dependência do pacote Qt.

## Release anterior

A [release pública 0.2.0](https://github.com/Warier/nodivu/releases/tag/v0.2.0) usa Electron. Seus binários e feed permanecem os originais. Para trabalhar especificamente nessa referência, consulte [o guia legado](apps/nodivu-electron/README.md). `Build-Release.ps1` agora gera o candidato Qt; `Build-ElectronRelease.ps1` preserva o empacotamento anterior.

## Licença

[MIT](LICENSE). Dependências mantêm suas próprias licenças: [avisos](THIRD-PARTY-NOTICES.md) e [distribuição Qt](apps/nodivu-desktop/THIRD-PARTY.md). Nenhum driver é instalado pelo build/pacote Qt. VB-CABLE é opcional, instalado separadamente pelo usuário; Nodivu não cria dispositivos virtuais.
