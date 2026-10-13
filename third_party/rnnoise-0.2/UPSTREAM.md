# RNNoise 0.2 — código e modelo fixados

Fonte: https://github.com/xiph/rnnoise/releases/download/v0.2/rnnoise-0.2.tar.gz
SHA-256 do tarball: `90fce4b00b9ff24c08dbfe31b82ffd43bae383d85c5535676d28b0a2b11c0d37`.
Tag v0.2, commit `904a876dce1f9ab8860c0a5000ed151f9f6eef58`.
Arquivos de processamento, cabeçalhos e modelo padrão copiados sem alterações; ferramentas de treinamento/build upstream omitidas. `SHA256SUMS.json` permite conferir este subconjunto. Licença BSD, conforme COPYING e cabeçalhos individuais. Modelo embarcado vem do mesmo tarball; não há download na execução.

Compilação Nodivu: C11/MSVC x64, backend portátil, sem AVX/RTCD obrigatório, sem treinamento e sem carregar modelos externos. O adaptador em plugins/audio-cleanup é MIT. Estado/modelo padrão inicializados fora do callback; reset copia um estado inicial com referências somente aos pesos estáticos. Processamento RNNoise 0.2 não aloca: FFT/tabelas estáticas, estados/janelas fixos. Não usar as funções de arquivo/modelo no callback.
