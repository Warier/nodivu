# Dependências do frontend nativo

O código original Nodivu é MIT. O pacote Qt não inclui Electron, Chromium, Node.js, WebEngine, QML ou bibliotecas JavaScript.

- Qt Base e Qt Image Formats **6.12.0**, vinculados dinamicamente. Os módulos usados são Core, Gui, Widgets, Network e Concurrent. A distribuição utiliza a opção LGPLv3 dos módulos, conforme os respectivos avisos; plugins e componentes de terceiros conservam suas licenças. Os textos integrais e os inventários SPDX/CycloneDX do SDK acompanham o aplicativo em `third-party/licenses/Qt` e `Qt-SBOM`.
- O usuário pode substituir as bibliotecas Qt por versões modificadas compatíveis. Não são aplicadas restrições a engenharia reversa necessária para depurar essas modificações. Fontes correspondentes originais: [Qt Base 6.12.0](https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/qtbase-everywhere-src-6.12.0.tar.xz), [Qt Image Formats 6.12.0](https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/qtimageformats-everywhere-src-6.12.0.tar.xz). Os projetos CMake e o código do Nodivu permanecem disponíveis sob MIT.
- **Monocypher 4.0.3**, CC0 ou BSD-2-Clause; a distribuição conserva as licenças e autores originais. O adaptador usa Ed25519 com SHA-512 da biblioteca, sem implementação própria de primitivas criptográficas.
- **miniz 3.1.2**, MIT; leitura de ZIP, sem executar seu conteúdo na GUI.
- CLAP 1.2.2, RNNoise 0.2, crates Rust e biblioteca padrão Rust pertencem ao backend/plugins existentes. Seus textos e inventários acompanham o pacote. O frontend não altera a ABI nem o DSP desses componentes.
- MSVC runtime: componentes redistribuíveis Microsoft, sob os termos que os acompanham. WASAPI e Media Foundation são componentes do Windows.

Proveniência e hashes dos componentes vendorizados constam em `third_party/*/SOURCE.json`. A distribuição do backend usa os mesmos binários verificados e inclui seus avisos independentemente da ausência de Node no novo pipeline.
