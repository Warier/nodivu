# Atualizações do aplicativo

O atualizador usa TUF com Ed25519 e uma autoridade separada da loja de plugins. O canal público do aplicativo ainda não está configurado neste repositório. Sem a configuração, a interface informa o motivo e desativa a verificação.

Atualizações ficam em Sistema → Aplicativo. O usuário solicita a verificação, escolhe baixar e confirma a instalação. Abrir o painel não inicia consultas, downloads ou instalações. Operações do projeto precisam terminar antes da instalação.

## Configuração do canal

Inclua `resources/updates/source.json` antes de gerar o pacote; o CMake instala esse arquivo quando ele existe. O bootstrap tem esta estrutura:

```json
{
  "schema_version": 1,
  "official_publishers": [],
  "metadata_url": "https://updates.example.org/metadata/",
  "target_url": "https://updates.example.org/targets/",
  "root": "<JSON serializado da root pública assinada do aplicativo>"
}
```

O exemplo é ilustrativo. A root contém chaves públicas e thresholds próprios para root, targets, snapshot e timestamp. Não reutilize as chaves da loja nem trate o feed YAML do Electron como autoridade confiável. Chaves privadas ficam fora do repositório, pacote, perfil e GUI. Publicação, rotação e custódia pertencem à operação de release.

O target assinado `windows-x64/stable.json` ou `windows-x64/beta.json` contém:

```json
{
  "schema_version": 1,
  "platform": "win32-x64",
  "channel": "stable",
  "version": "0.3.0",
  "installer": "windows-x64/Nodivu-0.3.0-setup.exe",
  "bytes": 14000000,
  "notes": "Descrição da versão."
}
```

O instalador também deve ser um target TUF com comprimento e SHA256 assinados. São aceitos até 256 MiB, caminhos relativos seguros terminados em `-setup.exe`, notas de até 8192 caracteres e versão posterior à atual. Stable não aceita prerelease. As versões aceitas são `MAJOR.MINOR.PATCH`, `-beta.N` e `-dev.N`.

## Download e instalação

Consulta e download usam HTTPS, validade e versões dos metadados, rotação de root e verificação de hashes e comprimentos. A confiança fica em `profile/updates/trust`, separada dos plugins. O arquivo é gravado atomicamente, e a cópia em `updates/downloads` é conferida novamente. Arquivos incompletos não são executados.

Ao confirmar a instalação, o aplicativo trata alterações não salvas e encerra o backend. O helper Win32 aguarda o PID original terminar, recalcula o SHA256 e abre o instalador interativo por usuário. Não encerra processos à força, não eleva privilégios e não recebe comandos de shell ou URLs remotas.

As suítes de atualização, helper e instalação cobrem ofertas inválidas, downgrade, caminhos, limites, hashes e recuperação de transações. A distribuição pelo canal público e o upgrade entre versões Qt publicadas ainda exigem validação operacional.
