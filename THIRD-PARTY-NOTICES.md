# Dependências e avisos

O código original do Nodivu, inclusive seus plugins e exemplos, está sob a [licença MIT](LICENSE). Dependências mantêm suas próprias licenças; a licença do projeto não concede direitos sobre marcas de terceiros.

## Frontend Qt/C++ em desenvolvimento

Qt 6.12.0 (link dinâmico), Monocypher 4.0.3 e miniz 3.1.2 substituem as dependências web no novo frontend. Consulte [licenças, origem e condições de distribuição](apps/nodivu-desktop/THIRD-PARTY.md). `scripts/qt/Package-Licenses.ps1` inclui os textos completos Qt, Rust, CLAP, RNNoise, bibliotecas C e runtime MSVC no pacote, sem Node/npm. O pacote Qt não inclui Electron, Chromium, Node, dependências npm nem o instalador VB-CABLE.

Os avisos abaixo registram também a referência Electron e a distribuição pública anterior. As ferramentas legadas mantêm seus avisos próprios; não implicam dependência desses componentes no runtime Qt.

## Distribuição anterior Electron

- **Electron 44.2.0**, Chromium, Node.js e bibliotecas incorporadas: o build preserva `LICENSE`, `LICENSES.chromium.html` e os demais avisos do runtime. Versões resolvidas em `apps/nodivu-electron/package-lock.json`.
- **CLAP 1.2.2**: cabeçalhos sob MIT, copyright Alexandre Bique. Texto integral em [third_party/clap-1.2.2/LICENSE](third_party/clap-1.2.2/LICENSE), com origem e hash no README dessa pasta.
- **Dependências Rust**: versões registradas em `Cargo.lock`. Ao montar o app, `scripts/package-licenses.cjs` inclui os textos LICENSE/COPYING/NOTICE dos crates resolvidos, um inventário de versões/licenças e os avisos da biblioteca padrão Rust em `third-party/licenses/` do resultado.
- **Windows SDK, WASAPI e Media Foundation**: componentes da plataforma/ferramentas Microsoft. Nenhum SDK Windows é incorporado ao repositório.
- **VB-CABLE**: integração opcional com um dispositivo instalado separadamente. O build de desenvolvimento não instala drivers. O build de release baixa o pacote básico original com checksum fixado e o inclui sem alterações, como instalação opcional. Sua licença é independente da MIT do Nodivu: https://vb-audio.com/Services/licensing.htm . Não alterar/rebatizar o pacote nem estender essa permissão aos produtos pagos.

OBS/win-capture-audio serviram como referências de pesquisa. Não há código GPL desses projetos incorporado ao coletor do Nodivu; ele usa as interfaces públicas Windows.

- **yauzl 3.4.0** e **pend** (versão no lockfile): MIT; leitura limitada de pacotes ZIP.
- **Marked 18.1.0**: MIT; interpretação de Markdown do README.
- **DOMPurify 3.4.16**: distribuição sob a opção Apache-2.0 da licença dual Apache-2.0/MPL-2.0; sanitização do README. Textos originais acompanham as bibliotecas e o diretório de avisos. Auditoria de execução em 2026-10-09: zero alertas; auditoria total inclui nove avisos na cadeia de desenvolvimento/build, que não foram corrigidos com atualizações automáticas incompatíveis.

- **electron-updater 6.8.9** e dependências de execução: licenças e inventário exato copiados para `third-party/licenses/npm` no instalador. `lazy-val 1.0.5` declara MIT no package.json e atribui autoria a Vladimir Krivosheev; o upstream não fornece arquivo LICENSE separado, por isso preservamos sua declaração e atribuição originais.
- **electron-builder 26.15.3**: ferramenta de build, não distribuída como dependência de execução. Auditoria nesta entrega: zero alertas em dependências de execução; a cadeia de build tem o aviso GHSA-ch52-4w7c-c8xp em http-cache-semantics 4.2.0, sem correção disponível no registro consultado.


### Catálogo de plugins (2026-10-09)

Cliente TUF `tuf-js` 5.0.1 (MIT), `@tufjs/models` 4.1.1 e `@tufjs/canonical-json` 2.0.0 (MIT), mantidos em https://github.com/theupdateframework/tuf-js. A versão 5 é compatível com o Node mínimo deste projeto; a série 6 exige um Node mais recente. Validação de assinaturas, versões, expiração e hashes usa essa implementação, não protocolo criptográfico próprio. Dependências transitivas e versões exatas constam no package-lock.json e no inventário npm incluído no pacote local; todos os avisos são copiados pelo build.

Auditoria npm de produção após TUF: zero vulnerabilidades reportadas. Auditoria completa: nove alertas da cadeia de desenvolvimento (oito moderados, um alto); não foi aplicado upgrade forçado das ferramentas de empacotamento nesta mudança.

## Limpeza de voz (0.2.0)

**RNNoise 0.2**, incluindo o modelo padrão: BSD, textos originais em [COPYING](third_party/rnnoise-0.2/COPYING) e cabeçalhos. Fonte oficial, commit/hash e arquivos reproduzíveis em [UPSTREAM.md](third_party/rnnoise-0.2/UPSTREAM.md). O adaptador CLAP/DSP auxiliar original do Nodivu é MIT. O build inclui `RNNOISE-LICENSE` nos pacotes e `RNNoise-BSD.txt` nos avisos do aplicativo. Nenhum modelo é baixado durante o uso.
