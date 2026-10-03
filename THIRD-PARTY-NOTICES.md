# Dependências e avisos

O código original do Nodivu, inclusive seus plugins e exemplos, está sob a [licença MIT](LICENSE). Dependências mantêm suas próprias licenças; a licença do projeto não concede direitos sobre marcas de terceiros.

- **Electron 44.2.0**, Chromium, Node.js e bibliotecas incorporadas: o build preserva `LICENSE`, `LICENSES.chromium.html` e os demais avisos do runtime. Versões resolvidas em `apps/nodivu-electron/package-lock.json`.
- **CLAP 1.2.2**: cabeçalhos sob MIT, copyright Alexandre Bique. Texto integral em [third_party/clap-1.2.2/LICENSE](third_party/clap-1.2.2/LICENSE), com origem e hash no README dessa pasta.
- **Dependências Rust**: versões registradas em `Cargo.lock`. Ao montar o app, `scripts/package-licenses.cjs` inclui os textos LICENSE/COPYING/NOTICE dos crates resolvidos, um inventário de versões/licenças e os avisos da biblioteca padrão Rust em `third-party/licenses/` do resultado.
- **Windows SDK, WASAPI e Media Foundation**: componentes da plataforma/ferramentas Microsoft. Nenhum SDK Windows é incorporado ao repositório.
- **VB-CABLE**: integração opcional com um dispositivo instalado separadamente. Este repositório e seu build de desenvolvimento **não incluem nem instalam** o driver da VB-Audio. Sua licença é independente da MIT do Nodivu; consulte o fornecedor antes de redistribuir qualquer pacote dele.

OBS/win-capture-audio serviram como referências de pesquisa. Não há código GPL desses projetos incorporado ao coletor do Nodivu; ele usa as interfaces públicas Windows.
