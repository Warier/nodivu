# Fonte de áudio de processos — base em desenvolvimento

Esta pasta contém aquisição nativa, fila/relógio, adapter CLAP e ensaios. O bloco já aparece no Electron. O contrato implementado está no header [capture_source.h](adapter/capture_source.h); a aquisição fica em [process_capture.h](capture/process_capture.h).

Compile o plugin com `./scripts/Build-WindowsAudioPlugin.ps1`; isso atualiza somente sua DLL e manifesto em `.local/plugins/windows-audio` e no pacote local. Reinicie o app para carregar. `capture/` implementa aquisição e fila; `adapter/` integra o contrato CLAP; `plugin.json` apresenta o bloco.

Ensaio do aplicativo: `node scripts/check-process-source.cjs "ID de saída dos emissores" "ID de CABLE Input" "ID de CABLE Output"`. Precisa de duas saídas independentes; os endpoints básico e 16ch do mesmo VB-CABLE disputaram o driver neste PC. Não mudar padrões/volumes para fazer passar. O ensaio abaixo verifica o coletor isoladamente.

| Caminho | Responsabilidade |
|---|---|
| `capture/process_capture.h/.cpp` | Identidade, ativação WASAPI, leitura limitada, estados, diagnóstico e cleanup |
| `tests/isolation.cpp` | Emissores sintéticos em processos separados, medição e critérios de aceite |
| `../../scripts/Build-ProcessCapture.ps1` | Compila somente o ensaio, sem recompilar/empacotar Nodivu |
| `../../scripts/Test-ProcessCapture.ps1` | Compila e executa com endpoint explícito |

Pré-requisitos: Windows com process loopback, MSVC x64/C++17 e SDK recente que inclua `audioclientactivationparams.h`. Usa WRL do SDK; nenhuma biblioteca de terceiros nova. O script encontra Visual Studio por `vswhere` e altera somente seu ambiente local de compilação. Binário gerado: `.local/process-capture/process-capture-test.exe`.

Da raiz do repositório, listar os dispositivos:

```powershell
node scripts/check-vb-cable.cjs --list
```

Escolher um **ID de reprodução ativo** e executar:

```powershell
./scripts/Test-ProcessCapture.ps1 -RenderId 'ID opaco da saída escolhida'
```

Preferir uma saída virtual já instalada para que os tons não sejam reproduzidos nos fones. O teste emite tons de amplitude 0,04 durante poucos segundos, somente no endpoint informado; nunca escolhe outra saída automaticamente. O cabo é conveniência do ensaio, **não dependência da captura por processo**.

Para exigir também aprovação da exclusão global:

```powershell
./scripts/Test-ProcessCapture.ps1 -RenderId 'ID opaco da saída escolhida' -VerifyExclusion
```

O teste lê áudio global nos modos de exclusão, mas não grava PCM: imprime somente métricas agregadas. Outros programas tocando/reemitindo áudio podem interferir na análise espectral global. Resultado `INCONCLUSIVE` não é aprovação. O parâmetro estrito retorna erro nesse caso; o teste básico continua exigindo os aceites de isolamento e lifecycle.

Não muda volume, padrão, mute, driver ou configuração de outro programa. Emissores entram em um Job Object antes de começar e encerram junto ao teste, inclusive em falhas. Têm ainda limite próprio de 30 segundos. A escolha de endpoint fica explícita para que o ensaio não capture um PID pessoal escolhido por aproximação.

Não foi incorporado código GPL do OBS/bozbez; a implementação usa as interfaces públicas Windows. Isolamento de dois processos, Mixer e recepção por outro backend foram verificados no Windows 10 22H2 local. Exclusão global e compatibilidade em outros computadores continuam pendentes; consulte os limites no [README principal](../../README.md).
