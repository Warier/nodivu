# Limpeza de voz

Reduz ruído de microfone com RNNoise na CPU. 30 ms de atraso compensado.

Mic → Limpeza de voz → Ganho → Limitador de picos → Saída. Para múltiplas fontes, use o limitador depois do Mixer. Configurações ficam no inspetor; intensidade/teto também no bloco. O bypass mantém o atraso e a sincronização dos caminhos.

Processamento local em CPU, estéreo 48 kHz (o host converte o dispositivo). Sem GPU, rede ou instalação de modelos. RNNoise 0.2 e modelo padrão BSD; adaptador Nodivu MIT. Consumo depende de CPU/quantidade de instâncias: “leve” é uma classificação, não garantia em qualquer computador. Compilar somente os plugins: `./scripts/Build-AudioCleanup.ps1`.
