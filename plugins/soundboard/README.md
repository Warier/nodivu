# Soundboard Nodivu

Dez sons em um único bloco, com reprodução simultânea e atalhos próprios. Incluído nas versões Qt e Electron do Nodivu (host API 3).

1. Adicione **Soundboard** e conecte sua saída ao Mixer ou à saída do projeto.
2. Abra as configurações do bloco. Escolha um MP3, WAV ou M4A em cada posição.
3. Clique no número para tocar. Tocar novamente reinicia apenas aquela posição; **Parar** interrompe somente esse som.
4. Use **Atalhos 1–0** para associar Ctrl+Shift+1 até Ctrl+Shift+0, ou personalize em Atalhos. O preset não sobrescreve conflitos de outros blocos. Números sem modificadores não são capturados globalmente, para não interferir na digitação.
5. Salve o projeto. Os caminhos são restaurados ao reabrir, sem autoplay; mantenha os arquivos disponíveis. Atalhos ficam no perfil local, associados ao UUID do bloco.

## Limites e consumo

Até 60 segundos e 32 MiB por arquivo. Decodificação assíncrona para estéreo float32/48 kHz, com cancelamento cooperativo. Dez arquivos de 60 s ocupam aproximadamente 220 MiB de PCM, além de temporários do decoder. Slots vazios não criam threads. Soma a 0 dB: vários sons altos podem saturar; use Ganho/Limitador após o bloco. Não acessa arquivos UNC/remotos automaticamente. Não inclui os arquivos de áudio ao salvar o projeto.

## Organização para desenvolvedores

- `audio/soundboard.h`: dez vozes, DSP limitado e soma sem normalização.
- `adapter/file_bank.h`: ABI C opcional `org.nodivu.file-bank/1`.
- `adapter/clap_plugin.cpp`: adaptação CLAP, portas e lifecycle.
- `../mp3-player/player/`: decoder Media Foundation e transporte compartilhados; o player original continua aceitando 10 minutos.
- `plugin.json`: aparência e topologia; `nodivu-package.json`: pacote/API mínima.
- `../../FILE-BANK.md`: contrato completo do host e persistência.

Build Windows: `pwsh -File scripts/Build-SoundboardPlugin.ps1` (MSVC x64, Windows SDK). O script compila somente a DLL e copia para o runtime local. MIT; cabeçalhos CLAP sob sua própria licença MIT.
