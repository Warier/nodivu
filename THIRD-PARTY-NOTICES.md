# Dependências e avisos

O código original do Nodivu, inclusive seus plugins e exemplos, está sob a [licença MIT](LICENSE). Dependências mantêm suas próprias licenças; a licença do projeto não concede direitos sobre marcas de terceiros.

- **Electron 44.2.0**, Chromium, Node.js e bibliotecas incorporadas: o build preserva `LICENSE`, `LICENSES.chromium.html` e os demais avisos do runtime. Versões resolvidas em `apps/nodivu-electron/package-lock.json`.
- **CLAP 1.2.2**: cabeçalhos sob MIT, copyright Alexandre Bique. Texto integral em [third_party/clap-1.2.2/LICENSE](third_party/clap-1.2.2/LICENSE), com origem e hash no README dessa pasta.
- **Dependências Rust**: versões registradas em `Cargo.lock`. Ao montar o app, `scripts/package-licenses.cjs` inclui os textos LICENSE/COPYING/NOTICE dos crates resolvidos, um inventário de versões/licenças e os avisos da biblioteca padrão Rust em `third-party/licenses/` do resultado.
- **Windows SDK, WASAPI e Media Foundation**: componentes da plataforma/ferramentas Microsoft. Nenhum SDK Windows é incorporado ao repositório.
- **VB-CABLE**: integração opcional com um dispositivo instalado separadamente. O build de desenvolvimento não instala drivers. O build de release baixa o pacote básico original com checksum fixado e o inclui sem alterações, como instalação opcional. Sua licença é independente da MIT do Nodivu: https://vb-audio.com/Services/licensing.htm . Não alterar/rebatizar o pacote nem estender essa permissão aos produtos pagos.

OBS/win-capture-audio serviram como referências de pesquisa. Não há código GPL desses projetos incorporado ao coletor do Nodivu; ele usa as interfaces públicas Windows.

- **electron-updater 6.8.9** e dependências de execução: licenças e inventário exato copiados para `third-party/licenses/npm` no instalador. `lazy-val 1.0.5` declara MIT no package.json e atribui autoria a Vladimir Krivosheev; o upstream não fornece arquivo LICENSE separado, por isso preservamos sua declaração e atribuição originais.
- **electron-builder 26.15.3**: ferramenta de build, não distribuída como dependência de execução. Auditoria nesta entrega: zero alertas em dependências de execução; a cadeia de build tem o aviso GHSA-ch52-4w7c-c8xp em http-cache-semantics 4.2.0, sem correção disponível no registro consultado.
