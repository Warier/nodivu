# Atalhos e macros no Qt

Abra **Exibir → Atalhos e macros**. Escolha ação/bloco, alcance global ou local, pressione a combinação e adicione. Para substituir uma associação, remova e adicione a nova. As mudanças são salvas imediatamente no perfil Qt; Escape/Fechar retoma os registros. O botão Testar executa a ação selecionada mesmo durante configuração. Conflitos são informados ao fechar; não tomamos teclas de outros aplicativos.

Ações: mostrar janela, silêncio geral, escuta local (depois de selecionar/ativar uma saída explicitamente), play/pause/recomeçar/parar player, alternar controles booleanos, definir/somar parâmetros numéricos e selecionar opções numéricas declaradas pelos plugins. Valores respeitam limites e parâmetros inteiros. Somar ganho usa dB, como o controle do bloco.

Persistência `hotkeys.json` versão 1, até 64 associações e 64 KiB, com escrita atômica. Arquivo inválido é preservado antes de substituir. Identidade: UUID do bloco + ID do plugin/tipo + ação; mudar a ordem ou o título não muda a associação. Ausência do bloco/plugin não ativa outro por nome. Não importa automaticamente atalhos do Electron: abra o projeto correspondente e configure no perfil Qt.

Windows usa `RegisterHotKey` + `MOD_NOREPEAT`, sem hooks de teclado, injeção, drivers ou privilégios administrativos. Atalhos locais passam pelo loop Qt. Uma ação por vez, intervalo de 250 ms, snapshot atualizado antes de agir, prazo de 7 s antes de cada etapa. Durante abrir/salvar/recuperar projeto ou fechar app não inicia comandos. Player valida arquivo, rota e disponibilidade; não seleciona arquivos/dispositivos sozinho.

Win, F12 e combinações de sistema/edição ficam reservadas. F13–F24 são úteis para macros de mouse remapeadas pelo software do fabricante. Não implementa captura direta de botões laterais, teclas de mídia nem gesto global pressionar/soltar; toggle é suportado. Jogos com anti-cheat, atalhos tomados por outro programa e apps elevados exigem teste manual; não há bypass dessas restrições.

Contrato geral e Electron: [HOTKEYS.md](https://github.com/Warier/nodivu/blob/main/HOTKEYS.md). API nativa: [Microsoft RegisterHotKey](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey).
