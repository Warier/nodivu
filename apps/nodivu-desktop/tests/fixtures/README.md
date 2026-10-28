# Projeto da referência Electron

`electron-project-v1.json` foi gravado em Windows pelo `writeProject(resourcesForFile(...))` de `apps/nodivu-electron/project-files.cjs`, sem alteração nessa implementação. O grafo de teste foi construído explicitamente com UUIDs fixos: fonte ausente → ganho → saída sem dispositivo e outra fonte ausente com intenção de captura. Não contém dados de usuário nem depende de arquivos/dispositivos reais.

O teste Qt abre o arquivo com o Rust real, preserva parâmetros, bypass, fios, posições, câmera, recurso relativo com Unicode e intenção persistente de captura; salva, reabre e verifica que o original não mudou. Os plugins são deliberadamente ausentes para testar placeholders e preservação. Nenhuma DLL é carregada desses IDs. A fixture é estática: Node não é requisito para executar os testes Qt.
