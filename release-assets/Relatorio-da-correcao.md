# Recuperação UWP — 7 de novembro de 2026

Pacote final: **YuGiOhForbiddenMemoriesUWP 1.1.0.3**, x64, assinado com
`CN=Antigravity`. Instalado no PC, com os dados anteriores preservados.

Base: [upstream v0.3.0](https://github.com/Unchiga/Yu-Gi-Oh-Forbidden-Memories-Recompiled/releases/tag/v0.3.0),
commit `663626b1d38355dd11f5532894a2b94169e7f349`. É a versão estável mais
recente consultada em 7/11/2026. O checkout de recuperação está em
`E:\yu gi oh uwp\upstream-review`; o projeto UWP está em `uwp/`.

## Causas e correções

- O patch anterior do teclado virtual lia descritores negativos como bytes
  sem sinal. Isso tornava selecionáveis colunas invisíveis. O núcleo atual
  preserva a leitura `s8` e a navegação original.
- O `GlyphSprite` antigo ocupava 0x78 bytes em objetos de 0x70 bytes. Sua
  escrita em +0x72 sobrescrevia o enlace do objeto seguinte, formando um
  ciclo na lista de exibição. As estruturas G32 do upstream restauram o
  tamanho de 0x6C e os offsets corretos.
- A realocação do executável UWP deslocava referências relativas à RAM
  absoluta do PlayStation. As unidades de jogo usam `-mcmodel=large`;
  LLD `/lldmingw` mantém os símbolos absolutos. O build rejeita qualquer
  referência REL32 a esses símbolos. A imagem mantém a tabela `.reloc`,
  pois `/FIXED` impediu sua ativação no ambiente UWP.
- A faixa baixa do executável é validada antes de iniciar o núcleo, com
  espaço para os mods e sem sobrepor RAM/scratchpad. As APIs FromApp usam
  placeholders ao mapear os espelhos de memória.
- Teclado e controle físico têm caminhos separados na interface. Um novo
  início exige soltar A/Enter/Espaço; transições limpam os eventos da UI.
- A página restaura o disco pela FutureAccessList de forma assíncrona,
  aplicando arquivo e caminho juntos. Uma seleção nova prevalece sobre
  uma restauração pendente.
- A textura do jogo agora é `R8G8B8A8_UNORM`, acompanhando os bytes RGBA
  do núcleo. O swap chain permanece BGRA. Isso corrige a troca vermelho/azul.
- O pacote contém os cinco mods, quatro objetos de código x64, cinco
  arquivos de idiomas e a música de abertura. O tema toca em loop no menu
  e para antes de entrar no jogo. R3 continua reservado para o menu do jogo.

## Evidências

- Navegação: 630 transições, 5.040 frames reais de animação; controle
  negativo com leitura sem sinal falha.
- Glifos: 80.897 criações reais sem sobrescrita; o controle negativo com
  o layout antigo reproduz o ciclo `21 -> 21`.
- Auditoria final: 771 objetos, 783 símbolos fixos, 3.417 referências;
  zero referências relativas inseguras.
- A biblioteca de produção foi testada após aplicação de 32.702 relocações
  PE para a base 0x3F860000. Passou nome, primeiro diálogo, restante da
  introdução e mão completa do primeiro duelo (frame 4806), com mods,
  callbacks de vídeo e áudio ativos.
- O aplicativo 1.1.0.2 instalado chegou ao primeiro duelo no PC durante o
  teste por teclado. A versão final 1.1.0.3 conserva o mesmo núcleo e corrige
  o formato da textura. Foi observada em execução na base 0x3F590000,
  passou pelos diálogos e chegou ao primeiro duelo durante o teste do
  usuário. Os contadores continuaram avançando a 60 frames por segundo.
  O usuário confirmou as cores corretas na versão final; a captura do
  aplicativo instalado mostra o campo do duelo.
- SHA256 do núcleo: `985BB42F10DD27721C7E93C8FD83AF32121A3F405DA8E4E69EA9B00051F0B272`.
- SHA256 do EXE final instalado: `0590B5ADE6217FB15AE2679D247314C3642A5B34ECFD39563D5EBEF90DD96D74`.

Os relatórios JSON, resultados dos testes e capturas estão em
`C:\Users\Cainã\Documents\YuGiOh-UWP\Evidencias`.
O MSIX final está na pasta imediatamente acima. A assinatura foi verificada
com `signtool verify /pa`; `tools/uwp/verify_package.py` verificou o conteúdo
e os cabeçalhos do executável empacotado.

## Recompilar e observar

Use as instruções de `tools/uwp/README.md` para gerar o núcleo com o
toolchain verificado. Depois compile o `.vcxproj` em Release/x64 com
MSBuild, `AppxBundle=Never` e `AppxPackageSigningEnabled=false`; assine o
MSIX resultante com SHA256. A geração de WinMD usa o linker MSVC em uma
etapa separada; o executável nativo usa LLD.

`tools/uwp/inspect_running.py` lê o processo instalado sem escrever memória
nem enviar comandos. `send_keyboard.ps1` só envia teclas se a janela do
jogo estiver em primeiro plano. O executável headless é exclusivamente
uma ferramenta de validação e não faz parte do MSIX.

Teclado: setas = direções; Enter/A/Z = Cross; B = Circle; X/C = Square;
Y/V = Triangle; P/Backspace = Start; Q/E = L1/R1.

O caminho inicial foi validado no PC. Não houve teste físico desta versão
no Xbox nem validação de campanha completa. Os atalhos QuickSave/QuickLoad
do host recuperado continuam sem implementação; save-states do núcleo
entre bases diferentes não foram habilitados por este trabalho. Isso não
altera os saves normais do jogo em memory card.
