# Perfil nativo de plugins — host_api 2

Esta extensão requer host posterior à release original 0.2.0. Pacotes API 1 continuam aceitos;
pacotes novos declaram host_api 2 no envelope e catálogo, para hosts antigos recusarem a instalação.
CLAP continua na ABI 1.2.2 e o grafo no schema 2; não há API de JavaScript dentro do plugin.

## Áudio e portas

Windows x64, float32 planar estéreo, 48 kHz, 1..480 frames, até 32 parâmetros ao vivo.
Perfis: fonte (0 entrada/1 saída), efeito (1 entrada/1 saída), consumidor (1 entrada/0 saída),
e novo efeito com referência (2 entradas/1 saída). Não suporta múltiplas saídas externas ainda.

A entrada CLAP de índice 0 e a saída de índice 0 têm flag MAIN; a entrada de índice 1
é secundária, estéreo, ID distinto, sem par in-place. Todos os IDs CLAP são válidos.
IDs dos fios Nodivu são os índices 0/1, não os IDs CLAP. Catálogo anuncia inputs [0,1]
e rótulos Áudio/Referência. O backend cria `sidechain:true` no bloco e valida esse campo
contra as capacidades carregadas: um projeto não pode inventar portas de um plugin instalado.
Ausentes mantêm a declaração salva para preservar seus fios. Plugins legados omitem o campo.

Uma conexão por porta; saída pode alimentar vários blocos. Porta desconectada entrega zeros.
O executor mantém a segunda entrada separada, nunca a soma ao áudio principal. Apenas Mixer
soma sinais. Compensação de chegada usa o caminho mais lento das entradas; a latência declarada
pelo efeito soma ao resultado. Todos os buffers são limitados e preparados antes do áudio,
sem IPC, fila ou alocação adicional por conexão. O bus secundário é emprestado por chamada.
Efeitos com referência usam buffers separados; bypass retorna somente a entrada principal,
alinhada pela latência nativa, com rampa de 480 frames. Fontes/consumidores não têm referência.

Extensões necessárias: clap.audio-ports e clap.params para controles. clap.latency opcional;
latência constante, lida após activate, máximo 48.000 frames. Sem rescan dinâmico nesse perfil.
Não reter buffers/pointers, não escrever nos inputs separados. Não alocar, bloquear, registrar
logs, tocar disco/rede ou lançar exceções através da ABI em process. Instâncias independentes.
Owner main-thread controla lifecycle; processamento exclusivo emprestado ao worker de áudio.

## Comando momentâneo e interface

Além de slider/number/toggle/select, o manifesto aceita widget **momentary**.
Deve apontar para parâmetro CLAP gravável, stepped, min=0, max=1, default=0.
Campo quick permite mostrar o botão no bloco; group/page/help organizam o inspetor.

`plugin.gesture {node_id, parameter, pressed}` é transitório. Não altera revisão nem documento.
O host aceita apenas UUID existente e parâmetro declarado momentary. pressed=true renova
uma concessão de 350 ms, false libera. O frontend renova a cada 100 ms enquanto pressionado.
No próximo processamento, o host envia 1 enquanto válida, 0 depois. Somam-se o período do
callback e a rampa do plugin: não é promessa de desligamento instantâneo de hardware.

O renderer libera em soltar/cancelar ponteiro, perda de captura, blur, ocultar ou remover
controle. Se travar, o prazo no backend fecha sozinho. O valor de documento deve ser 0:
um graph.apply/project.open que tente persistir o gesto ativo é rejeitado. Reuso de slot
limpa a concessão; ela nunca é serializada. Não confundir com toggle, que é persistente.
O botão usa mouse ou Espaço/Enter quando focado. **Atalhos globais não estão implementados**.

O host não oferece garantia de privacidade se houver bypass, rota paralela ou outro aplicativo
capturando o microfone. Autores devem explicar esse comportamento no README do plugin.

## Distribuição e exemplos completos

[Pacote](PLUGIN-PACKAGE.md), [UI](PLUGIN-UI.md), [Loja](PLUGIN-STORE.md).
Repos independentes, sem rebuild do Nodivu para próximas alterações compatíveis:

- https://github.com/Warier/nodivu-ducking — algoritmo, CLAP, duas entradas, manifestos e testes.
- https://github.com/Warier/nodivu-talk-button — três modos e gesto de fala transitório.

Ambos incluem README renderizável pela loja, CONTRACT.md, licença MIT, CLAP MIT e script
Build.ps1 que gera ZIP Windows x64. Envelope + catálogo devem declarar host_api 2.
Os exemplos não acompanham o instalador: são entregues pela loja/importação local.

## Verificação

`cargo test --workspace --all-targets --locked` inclui isolamento entre portas e silêncio
da referência desconectada. `utility_probe` carrega as duas DLLs reais e verifica redução,
bypass, reset e eventos com quantums variáveis. Depois de compilar os dois repos irmãos:

```powershell
cargo run -p nodivu-clap-host --example utility_probe --release --locked -- ..
node scripts/check-utility-plugins.cjs
```

O segundo comando é opt-in com WASAPI real na saída padrão (volume baixo), testa o watchdog,
recusa gesto salvo, verifica amplitude e mantém a geração de áudio. Não muda os padrões do Windows.
A suíte Node testa o controlador de gestos e o frontend; smoke-utility-store instala pelo
catálogo HTTPS e confere os pacotes no gerenciador. Não equivale a teste em todos os dispositivos.
