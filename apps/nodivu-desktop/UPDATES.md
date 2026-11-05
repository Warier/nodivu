# Atualizações da edição Qt

A edição alternativa usa **Nodivu Qt**, perfil `%APPDATA%/NodivuQt`, instalação `%LOCALAPPDATA%/Programs/Nodivu Qt` e projetos em `Documentos/Nodivu/Projetos-Qt`. Não instala sobre o Electron por padrão. Não há migração automática de perfil. Abra uma cópia do projeto para comparar as edições.

## Canal público

- Releases Qt: tag `v<VERSÃO>-qt.1`, marcadas como **prerelease**, sem alterar `latest` do Electron. Versão interna: `MAJOR.MINOR.PATCH`.
- Metadados: branch `qt-updates` de `Warier/nodivu`, servidos via HTTPS por `raw.githubusercontent.com`. Não precisa servidor dedicado.
- Bootstrap público: `resources/updates/source.json`, embutido no pacote. Raiz Ed25519 própria; independente da loja de plugins.
- O Qt consulta `windows-x64/stable.json` 15 segundos após abrir e a cada seis horas. Aqui `stable` é exclusivamente o canal Qt; não é o `latest.yml` do Electron.
- Download e reinício exigem clique e confirmação. Projeto com mudanças segue a confirmação normal. Não há instalação silenciosa, encerramento forçado ou elevação pelo app.
- A oferta, URL, tamanho e hashes do instalador são assinados em TUF. O instalador reside nos assets da release; somente URLs Qt do repositório fixado no bootstrap são aceitas. Limite: 256 MiB. Redirects exigem HTTPS; bytes finais continuam sujeitos ao tamanho/hash assinado.
- O helper espera o processo encerrar, verifica novamente o SHA256 e abre o instalador. Perfil/documentos não são parte da substituição de binários.

## Publicar / renovar

Ferramenta de mantenedor (Node 22+ ou 24, não embarcado no aplicativo):

```powershell
# Executar init somente uma vez para um NOVO canal. Nunca trocar a raiz por conveniência.
node scripts/qt/publish-updates.cjs init C:/caminho/qt-update-feed C:/privado/qt-update-keys
# Copiar somente source.json para apps/nodivu-desktop/resources/updates/source.json antes de compilar.
./scripts/Build-Desktop.ps1 -Test
./scripts/qt/Package-Desktop.ps1 -Installer
# Publicar o instalador como prerelease v0.2.2-qt.1. Após verificar o asset público:
node scripts/qt/publish-updates.cjs publish C:/caminho/qt-update-feed C:/privado/qt-update-keys C:/caminho/Nodivu-0.2.2-windows-x64-qt-setup.exe 0.2.2 v0.2.2-qt.1
# Revisar/commitar/push APENAS metadados, oferta e bootstrap na branch qt-updates.
```

O diretório privado fica fora dos dois repositórios. Chaves criptografadas com DPAPI CurrentUser; não publicar `keys.dpapi`. A publicação verifica a identidade das quatro chaves. Renovar **timestamp antes de 14 dias**, snapshot 30 dias, targets 90 dias; a raiz inicial vence em 365 dias. Cada publicação incrementa revisões. O tool atual renova os três papéis, mas **rotação da raiz e backup portátil das chaves ainda precisam de procedimento operacional próprio**. Perder a chave DPAPI pode exigir distribuição manual; manter backup seguro do perfil/custódia. Não copiar as chaves da loja.

Sonda de mantenedor `public_update_probe --source resources/updates/source.json --current 0.2.1 --download` autentica o canal e baixa em perfil temporário sem instalar. A versão atual deve retornar `available:false`. Testes locais de TUF cobrem expiração, rollback, assinatura e adulteração; `Test-Installer.ps1 -ViaUpdater` usa autoridade efêmera e identidade isolada, sem tocar no app instalado.

## Limites

A primeira edição Qt requer instalação manual; o canal prepara as próximas atualizações Qt. Não converte automaticamente usuários Electron. Assinatura TUF autentica conteúdo, mas não é certificado Authenticode/SmartScreen. Testes Windows 11/PC limpo e atualização real entre duas futuras versões públicas Qt permanecem aceites separados.

## Referências

- [WM_HOTKEY / Qt native events](https://doc.qt.io/qt-6/qabstractnativeeventfilter.html)
- [Releases GitHub](https://docs.github.com/en/rest/releases/releases)
