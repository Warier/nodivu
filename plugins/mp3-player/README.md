# Player de áudio

Escolha um arquivo MP3, WAV ou M4A/AAC no inspetor. Conecte a saída do bloco ao Mixer ou à saída de áudio. Use play/pause e a barra para procurar um trecho.

Até 10 minutos e 256 MiB por arquivo. O arquivo permanece no seu computador; cada bloco tem reprodução independente. O decoder usa Windows Media Foundation. Não precisa de GPU. O uso de memória depende da duração do áudio decodificado.

## Instalação

Este bloco acompanha o desenvolvimento do Nodivu. Pacotes compilados podem ser importados por pasta ou ZIP no gerenciador de plugins. Alterações entram em vigor ao fechar e abrir o app. Salve o projeto antes.

A versão do pacote é independente da versão do aplicativo. Metadados locais não concedem selo oficial. Consumo ainda sem classificação medida.

## Troca de arquivo e cancelamento — 1.0.1

Escolher outro arquivo ou remover o bloco invalida a carga anterior. O decoder
verifica essa invalidação antes de abrir o arquivo, entre leituras e durante a
conversão de PCM. Resultados e erros de uma carga antiga não substituem a atual.
Ao começar a nova carga, o PCM anterior é liberado fora da thread de áudio, depois
que o processamento deixa de usá-lo. Uma falha no novo arquivo não restaura o antigo.

O cancelamento é cooperativo: uma chamada já bloqueada em Media Foundation/driver
não pode ser interrompida por essa flag. Não há promessa de prazo máximo nesse caso
nem encerramento forçado de threads. O limite agregado de memória e streaming são
evoluções separadas; o arquivo completo ainda é decodificado em memória.

## Testes para desenvolvimento

Na raiz do repositório do Nodivu, `scripts/Test-PlayerLifecycle.ps1` exercita troca,
remoção, resultados antigos e encerramento com um decoder controlado de teste.
`-Asan` ativa AddressSanitizer. O teste tem timeout externo de 30 segundos e falha
se precisar encerrar seu próprio processo. Ele não comprova cancelamento de uma
chamada Windows bloqueada.

`scripts/Test-MediaPlayer.ps1 -Ffmpeg <caminho-do-ffmpeg>` usa o decoder real com
MP3/WAV/M4A sintéticos, transporte e limites de 600/601 segundos. FFmpeg é apenas
ferramenta de teste e não acompanha o plugin. A ABI de player/transporte foi preservada.

## Licença

Código do plugin: MIT. Veja LICENSE incluída no pacote e os avisos do CLAP.
