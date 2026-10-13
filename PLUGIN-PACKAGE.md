# Pacotes de plugins Nodivu

Contrato de distribuição local implementado em 2026-10-09. Não substitui a ABI CLAP nem o [manifesto visual](PLUGIN-UI.md). O áudio continua no backend; o gerenciador não executa scripts de instalação nem compila fontes.

## Estrutura mínima

```text
meu-plugin/
  plugin.json
  nodivu-package.json
  meu-plugin.clap
  README.md
  LICENSE
  images/logo.png
```

`plugin.json` aponta para a biblioteca dentro desta pasta e declara o ID do bloco e seus controles. Compile somente o plugin para Windows x64; envie o binário, não apenas fontes. O app atual aceita um plugin CLAP por pacote. O ID deve coincidir com o descriptor CLAP e permanecer estável em atualizações.

Envelope opcional para legado, obrigatório para atualizar o mesmo ID:

```json
{
  "schema_version": 1,
  "plugin_id": "com.example.meu-plugin",
  "version": "1.0.0",
  "author": "Nome do autor",
  "host_api": 1,
  "platform": "win32-x64",
  "repository": "https://github.com/autor/meu-plugin",
  "resource_tier": "unknown",
  "license": "MIT"
}
```

Obrigatórios: schema 1, ID igual ao manifesto, versão numérica `x.y.z` e autor de até 128 caracteres. Sem pré-release neste primeiro formato; cada parte da versão tem até seis dígitos, sem zero inicial. Se informados, `host_api` deve ser 1 e `platform` precisa corresponder ao sistema/arquitetura atual. Repositório exige HTTPS sem credenciais. `resource_tier` aceita `light`, `moderate`, `heavy` ou `unknown`; é declaração do autor, não benchmark ou aprovação. Ausência vira não medido. Licença é informativa: inclua o texto e avisos das dependências no pacote. Não há selo oficial obtido por campos locais.

## Importação e atualização

Em Sistema → Plugins instalados, importe pasta ou ZIP/`.nodivu-plugin`. ZIP admite os arquivos na raiz ou uma única pasta externa. Limites: 256 MiB comprimidos e expandidos, 2048 entradas, 12 níveis de subpastas, manifesto 64 KiB, envelope 16 KiB. Recusa links, dispositivos especiais, caminhos que escapam, nomes inválidos no Windows, duplicação sem distinguir maiúsculas/minúsculas, conteúdo criptografado e CRC incorreto.

A cópia é validada novamente antes de publicar. Instalar exige confirmar confiança no autor: DLL executa com os direitos do backend, sem sandbox. A validação estrutural não garante que código nativo seja seguro. Não inclua scripts de instalação, drivers, dependências globais ou credenciais.

Novo ID: fica disponível depois de fechar e abrir o app. Até oito tipos habilitados; o nono fica desativado. Mesmo ID: ambos os pacotes precisam de envelope, e a versão nova deve ser maior. A troca é agendada, sem sobrescrever DLL em uso. Antes da ativação, ID e versão são conferidos outra vez. A versão anterior é arquivada, e sua desativação é preservada. Cancelamento e retorno a versão arquivada ficam no gerenciador. Atualizar pacote não atualiza nem migra projetos automaticamente.

O perfil contém `plugins`, `plugin-staging`, `plugin-updates` e `plugin-archive`. Não são pastas para publicar no Git. Registros interrompidos são retomados no início; erro na troca tenta restaurar a versão anterior e exibe aviso. Arquivos arquivados e staging cancelado não têm limpeza automática nesta entrega. Não mova esses arquivos com o app aberto.

## README e imagens

Inclua README.md de até 256 KiB com as instruções da versão distribuída. Títulos, tabelas, listas e blocos de código são suportados; HTML passa por sanitização e não pode acessar Node ou executar ações. Links absolutos HTTPS abrem no navegador. Outros documentos relativos ainda não são navegáveis.

Até 12 imagens locais PNG/JPEG/WebP/GIF de 2 MiB cada. Use caminhos relativos dentro do pacote. Imagens externas não são baixadas. Prefira copiar para o pacote as imagens necessárias e manter os créditos/licenças. SVG ativo, scripts, eventos, estilos e iframes não fazem parte do contrato.

## Compilar os exemplos

Da raiz do checkout, com ferramentas descritas no README:

```powershell
./scripts/Build-Mp3Plugin.ps1
./scripts/Build-WindowsAudioPlugin.ps1
./scripts/Prepare-DemoPlugin.ps1
./scripts/Build-AudioCleanup.ps1
```

Os scripts copiam DLL, manifesto, envelope, README e licenças para as pastas locais de execução. O código do player está em `plugins/mp3-player/player`; o adaptador CLAP em `plugins/mp3-player/adapter`; apresentação e distribuição nos dois JSONs. Alterar o plugin não exige recompilar o app. Ao mudar um pacote distribuído, incremente sua versão; não publique bytes diferentes sob a mesma versão.

## Limites atuais

O [cliente da loja](PLUGIN-STORE.md) já verifica catálogo e download por TUF em origem configurada. Ainda pendentes: publicação da origem pública, ativação sem reinício, carga por instância sob demanda, resolução de versões no projeto, runtimes/modelos de workers. Falhas recuperáveis de pacote são separadas no início; crash/hang de DLL no mesmo processo continua podendo afetar o backend. Não é um host CLAP universal: portas, parâmetros, latência e extensões precisam seguir o perfil implementado pelo Nodivu.

## Latência e plugins ausentes — 0.2.0

A latência CLAP é consultada após `activate`, inclusive nas instâncias clonadas, e precisa ser constante durante a ativação e igual entre instâncias do mesmo tipo. Até 48.000 frames nativos em 48 kHz; a soma do caminho e o orçamento total de compensação também são limitados a 48.000. Mudança dinâmica de latência não é negociada durante o fluxo. O grafo usa a latência declarada; bypass de efeito atrasa o original pelo mesmo valor, com transição de 480 frames. Buffers são reservados antes de processar áudio. Tempo de CPU não substitui a declaração do atraso algorítmico.

Exemplos completos em `plugins/audio-cleanup`: RNNoise 0.2 (1440 frames/30 ms) e limitador (240 frames/5 ms). `dsp.h` contém os algoritmos, `clap_plugin.cpp` adapta o contrato e as subpastas contêm apresentação/envelope/README. `Test-AudioCleanup.ps1` mede alinhamento/atenuação e verifica picos/reset; `check-cleanup.cjs` confere o Mixer/WASAPI real. A licença upstream do RNNoise é BSD, separada da MIT do adaptador.

Um pacote ausente/desativado não impede abrir e salvar o projeto. O documento mantém UUID, parâmetros, portas, fios e recursos. A projeção de execução exclui o nó e seus fios, sem criar bypass implícito; outras ramificações continuam funcionando. O canvas mostra um bloco indisponível, sem controles numéricos inventados. Reinstalar/reativar e reabrir restaura a validação normal. Mudança incompatível de parâmetros de um plugin instalado continua sendo erro explícito.
