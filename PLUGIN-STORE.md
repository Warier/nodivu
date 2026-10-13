# Loja de plugins: cliente, catálogo e publicação

Implementação de desenvolvimento, 2026-10-09. O cliente da loja e o gerenciador local estão implementados; **o catálogo público ainda não foi publicado**. A instalação comum mostra isso em Explorar. Nenhum servidor, chave de produção ou release foi criado nesta entrega.

## O que o app já faz

- Abas Instalados, Explorar e Atualizações, busca, nome/autor, logo, tamanho de download, classificação de consumo e selo de publicador oficial.
- Catálogo TUF verificado automaticamente ao abrir a loja (intervalo mínimo de 15 minutos), após iniciar o app e a cada seis horas quando o gerenciador não estiver ocupado. Atualizar lista força uma consulta.
- README Markdown e imagens vinculados à versão do catálogo, com a mesma sanitização dos pacotes locais. Conteúdo do plugin não recebe HTML livre ou acesso às funções do app.
- Download sob comando do usuário, verificação de tamanho/hash contra metadados assinados, extração limitada e confirmação de confiança. Ativação apenas na próxima abertura; atualização/rollback aproveitam o gerenciador local.
- Lista e README já consultado ficam disponíveis offline. Offline não autoriza instalar/atualizar. Falha de rede, assinatura ou expiração preserva a lista anterior com aviso, sem reiniciar áudio.
- Pacotes e catálogo mudam sem reconstruir o Nodivu. Uma nova origem/raiz inicial de confiança precisa ser distribuída pelo mantenedor; rotação posterior usa o protocolo TUF.

O app não instala atualizações de plugins automaticamente. Uma DLL nativa verificada continua executando com os direitos do backend. Assinatura comprova autorização pela origem configurada e integridade, não ausência de bugs nem sandbox.

## Modelo de confiança

Usamos [tuf-js](https://github.com/theupdateframework/tuf-js), versão 5.0.1 fixada, compatível com o Node mínimo do projeto. Metadados root/timestamp/snapshot/targets, limiares de assinatura, expiração, hashes e proteção contra versões antigas são tratados pela implementação TUF. O Nodivu acrescenta limites do produto e validação do envelope; não cria um protocolo criptográfico alternativo.

O bootstrap da loja é `apps/nodivu-electron/store/source.json`, incluído pelo build. Sua ausência é deliberada até preparar a operação de publicação. Formato:

```json
{
  "schema_version": 1,
  "metadata_url": "https://exemplo.github.io/nodivu-plugins/metadata/",
  "target_url": "https://exemplo.github.io/nodivu-plugins/targets/",
  "root": "CONTEUDO_JSON_DA_RAIZ_PUBLICA_TUF_COMO_STRING",
  "official_publishers": ["nodivu"]
}
```

As URLs acima são exemplos, não um serviço existente. Caminhos terminam em `/`, sem credenciais, fragmento ou query. Use pastas separadas para metadados e targets. Nesta fatia, HTTP é proibido fora do ensaio loopback e redirecionamentos HTTP são recusados; links de GitHub Releases que redirecionam para outro host não funcionam diretamente. Um espelho estático em GitHub Pages ou outra hospedagem HTTPS funciona sem backend dedicado, observados os limites do provedor. Suporte a redirecionamentos de assets Releases com origens controladas é evolução pendente.

Nunca colocar chaves privadas no app, repositório público, ZIP ou perfil do usuário. A raiz inicial chega com o app; não é obtida de uma URL indicada por um plugin. O cache de confiança é separado por origem/bootstrap; raízes rotacionadas são mantidas pelo cliente TUF. O modelo assume que um invasor não altera os arquivos locais do próprio aplicativo/perfil.

Em desenvolvimento, `NODIVU_STORE_CONFIG` aceita um caminho de configuração explícito. Um app instalado ignora essa variável fora do ensaio `--smoke-test --test-store`, que usa perfil temporário. Não existe caixa de URL da loja no renderer, nem instalação acionada por links dentro de README.

## Índice assinado

`catalog.json` é um target TUF, não um JSON avulso confiado por estar em HTTPS. Até 1 MiB e 500 itens. Uma versão por ID nesta primeira entrega:

```json
{
  "schema_version": 1,
  "plugins": [{
    "id": "com.example.ganho",
    "version": "1.0.0",
    "title": "Meu ganho",
    "author": "Autor",
    "publisher_id": "autor-cadastrado",
    "description": "Ajusta a amplitude do sinal.",
    "archive": "com.example.ganho/1.0.0/plugin.zip",
    "download_bytes": 123456,
    "readme": "com.example.ganho/1.0.0/README.md",
    "logo": "com.example.ganho/1.0.0/logo.png",
    "images": {"images/exemplo.png": "com.example.ganho/1.0.0/exemplo.png"},
    "platform": "win32-x64",
    "host_api": 1,
    "resource_tier": "unknown",
    "repository": "https://github.com/autor/projeto"
  }]
}
```

`logo`, `images` e `repository` são opcionais. Os demais campos do exemplo são obrigatórios. Todos os caminhos de arquivos são relativos à raiz de targets e precisam constar nos metadados assinados. O tamanho do ZIP no índice precisa coincidir com o tamanho autenticado por TUF. ID e versão internos são conferidos depois de extrair, antes de instalar. Fonte, nomes e descrições têm limites; nomes duplicados de ID e caminhos ambíguos são recusados.

O publicador do catálogo extrai README/logo/imagens da mesma revisão imutável do pacote. Não apontar README para `main`: ele deve documentar a versão ofertada. Mapeie no campo `images` os caminhos relativos usados pelo README; os arquivos mapeados também são targets assinados. Até 12 imagens, de 2 MiB cada; README 256 KiB. Não há busca automática de conteúdo arbitrário no GitHub por ID do repositório.

O selo **Oficial Nodivu** exige publisher_id autorizado pela configuração confiável e um índice autenticado. Um campo `official` no pacote ou no item não concede o selo. O índice é curado: quem o assina responde pela associação de IDs aos publicadores. `resource_tier` é `light`/verde, `moderate`/amarelo, `heavy`/vermelho ou `unknown`/cinza. É declaração do publicador, ainda sem classificação medida ou detecção de GPU. Tamanho de download não representa uso de CPU/RAM.

## Fluxo para autores

1. Produzir o pacote conforme [PLUGIN-PACKAGE](PLUGIN-PACKAGE.md), compilado para o contrato/plataforma, com README, licenças e versão nova para bytes novos.
2. Publicar fontes e artefatos imutáveis no repositório do autor. O mantenedor do catálogo revisa identidade, ID, contrato, avisos e requisitos; inscrição pública automatizada ainda não está aberta.
3. Copiar artefatos aprovados para a área de targets, gerar o índice e metadados TUF com ferramentas de publicação mantidas. Publicar targets/metadados versionados antes de timestamp. Manter as versões e metadados necessários à rotação.
4. O cliente consulta a nova revisão, apresenta a atualização e pede instalação. A troca efetiva ocorre ao reabrir o Nodivu; não interrompe uma chamada por atualização silenciosa.

Para lançar a origem pública ainda falta definir custódia/backup das chaves, prazo de validade/renovação, limiares e responsáveis de assinatura, ferramenta de publicação, rotação testada e política de revogação. O emissor dos testes usa chaves efêmeras com limiar 1; **não é ferramenta de produção nem escolha de política para a loja pública**. Use a [documentação TUF](https://theupdateframework.io/docs/) para a operação do repositório.

## Ensaio reproduzível

Após `Build-Electron.ps1`, que prepara backend, player e dependências:

```powershell
node scripts/Test-PluginStore.cjs
```

O script abre um servidor somente em `127.0.0.1`, cria chaves efêmeras em memória e inicia Electron com perfil isolado. Confirma busca, selo, README e instalação de pacote inerte pela UI; a confirmação é simulada exclusivamente nesse ensaio. Não carrega os arquivos fictícios como DLL e não publica na internet. Logs e captura ficam em `.local/store-test.*.log` e `.local/electron/plugin-store.png`.

`npm test` em `apps/nodivu-electron` também verifica: duas versões no mesmo executável, troca agendada, leitura offline, SHA inválido, assinatura inválida, metadados expirados, tentativa de voltar metadados, limites e um ZIP assinado com o player CLAP real carregado no backend. Esse teste requer o player compilado; ausência é falha explícita. A suíte não usa rede externa. Os testes de áudio reais continuam separados.

## Pendências

Catálogo público e operação de assinatura; redirecionamentos Releases; revogação/quarentena; submissões de autores; medição de consumo e requisitos detalhados de hardware; compatibilidade de versões em projetos; carga sob demanda e ativação dinâmica; workers/modelos maiores que 256 MiB. O limite de oito tipos ativos permanece. RNNoise e limitador acompanham o app como pacotes nativos a partir da 0.2.0.

## Operações recuperáveis — 0.2.0

Downloads mostram arquivo/bytes e permitem cancelamento. Cancelar interrompe a transferência e impede publicar um pacote parcial; a etapa final de cópia/registro não pode ser cancelada. A origem e os bytes continuam sujeitos à verificação TUF. Metadados expirados/offline permitem consultar o cache, mas não instalar.

**Limpar downloads** remove targets em cache, README baixado e staging de atualizações canceladas, preservando pacotes instalados, staging referenciado por atualização pendente, arquivos de rollback, projetos e metadados TUF (inclusive histórico de versões contra rollback). O próximo download exige nova verificação do catálogo. Um journal inválido impede a limpeza de staging até ser recuperado.
