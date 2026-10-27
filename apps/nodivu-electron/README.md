# Referência Electron

Este frontend é a referência da release pública 0.2.0 e da comparação de paridade durante a migração. O frontend principal em desenvolvimento é [Qt/C++](../nodivu-desktop/README.md). Não é necessário Node para compilar ou executar o pacote Qt.

Para reconstruir especificamente a referência Electron, a partir da raiz, com Node 22.12+, Rust e MSVC:

```powershell
./scripts/Build-Electron.ps1
./scripts/Start-Electron.ps1
./scripts/Test-Electron.ps1 -Project -Viewport -Capture
```

`scripts/Build-ElectronRelease.ps1` preserva o empacotamento anterior e não publica automaticamente. Não o confundir com `scripts/Build-Release.ps1`, que gera o candidato Qt.

O renderer usa JavaScript/HTML/CSS/SVG, sem React. `main.cjs` administra o processo e o backend; `preload.cjs` limita a ponte; `ui/` implementa o Estúdio. `npm ci`, `npm test` e `npm run dev:ui` nesta pasta continuam disponíveis para referência. A demonstração tem dispositivos simulados, sem áudio real.

Não executar esta versão e o Qt simultaneamente sobre `%APPDATA%/Nodivu`. Use os perfis de desenvolvimento ou o modo portátil para comparar. O Qt instalado recusa a abertura se detectar a versão Electron do mesmo usuário em execução.
