# Arena REMAKE — The Elder Scrolls: Arena em C++ (PT-BR)

Sou fã de *The Elder Scrolls* desde sempre, e *Arena* (1994) foi onde tudo começou: Tamriel
inteira para explorar, a Prisão Imperial, Jagar Tharn no lugar do Imperador e a busca pelos
pedaços do Cajado do Caos. Só que hoje ele é difícil de jogar: roda no DOSBox, em 320x200, com
interface e controles da época e só em inglês.

Este projeto é a minha tentativa de trazer o Arena para os dias de hoje sem perder a alma dele.
É um motor novo, escrito do zero em C++20 + SDL2, que lê os arquivos do jogo original e recria
o mundo com o mesmo conteúdo e as mesmas regras (cidades, ermo, masmorras e monstros saem
iguais aos do original, pelas mesmas sementes), mas com cara e jogabilidade atuais:

- **Totalmente em português do Brasil**, de menus a diálogos;
- **Full HD e widescreen**, renderização pela placa de vídeo (OpenGL) com luz difusa, relevo,
  água com reflexos, bloom, céu com dia e noite, chuva e neve;
- **Olhar livre com o mouse** e controles modernos (WASD, atalhos, tela cheia);
- **Interface nova** no estilo medieval (madeira, pergaminho e dourado), mais simples e clara;
- **Salvar e carregar** em vários espaços, criação de personagem em passos, níveis, magias,
  inventário, comércio, cidades com moradores, ermo, viagem por Tamriel, masmorras e a busca
  pelos pedaços do Cajado do Caos;
- **Sprites e texturas redesenháveis**: basta colocar PNGs em `data/mods/` para trocar a arte
  original (estou redesenhando aos poucos).

O remake **não inclui nenhum arquivo do jogo**. Para jogar, é preciso a sua cópia do Arena, que a
Bethesda distribui gratuitamente (também está na Steam e no GOG).

> Projeto de fã, sem fins lucrativos e sem afiliação com a Bethesda Softworks / ZeniMax Media.

## Compilar (Windows, Visual Studio 2026)

```bash
cmake -S . -B C:/arena-build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build C:/arena-build --config Release
```

Dependências: `vcpkg install sdl2:x64-windows stb:x64-windows`. Use um diretório de build com caminho curto
(o MSBuild falha com caminhos acima de 260 caracteres).

## Executar

```bash
ArenaRemake.exe --arena "Z:/SteamLibrary/steamapps/common/The Elder Scrolls Arena/ARENA"
```

Ou defina `ArenaPath` em `settings.ini`. Teclas: **F11** / **Alt+Enter** tela cheia,
**F12** captura de tela, setas + Enter ou atalhos (C/N/S) nos menus.

`--continuar` entra direto no mundo com o personagem salvo (o mesmo que "Carregar Jogo").

### Controles no mundo 3D

| Tecla | Ação |
|---|---|
| W A S D / setas | andar e virar |
| Mouse | olhar livre (horizontal e vertical) |
| Shift | correr |
| Clique ou F | atacar (arrastar o mouse antes do clique muda o golpe: direita/esquerda; com W é estocada) |
| E ou Enter | usar: abrir/fechar portas, pegar a chave, saquear baús, pilhas e corpos (clique também usa se não houver inimigo à frente) |
| Q | conjura o feitiço preparado (mãos e efeito do original; projéteis explodem em paredes e criaturas) |
| C / I / B | janela do personagem nas abas Personagem / Inventário / Feitiços (1 2 3 ou setas trocam de aba) |
| R | descansar (recupera a vida se não houver inimigos por perto) |
| M | minimapa |
| H | mostrar/ocultar ajuda |
| Esc | pausa (S volta ao menu) |

`Video.RenderScale` no `settings.ini` define a resolução 3D em relação à janela (padrão 0.6).
Os efeitos modernos ficam em `[Graphics]` (veja "Gráficos" abaixo); `--classico` desliga todos.

Modo sem janela para testes: `ArenaRemake --screenshot <title|menu|menu-popup|class> saida.png`,
ou um roteiro de entradas com capturas no meio, por exemplo:

```bash
ArenaRemake --seed 42 --script "k:x;k:n;k:g;enter;k:a;shot:pergunta.png"
```

Comandos: `k:X`, `enter`, `esc`, `up`, `down`, `left`, `right`, `bs`, `click:x,y`, `rclick:x,y`,
`move:x,y`, `wheel:n`, `text:Nome`, `shot:arquivo.png`, e no mundo 3D `hold:W,1.5`, `run:W,1`,
`turn:graus`, `look:graus`, `wait:segundos`, `attack:segundos`, `press:E`, `bench:N` (mede o tempo médio de N quadros),
`goto:x,z,graus`, `gotowater:N` (borda do N-ésimo fosso), `gotosprite:NOME,N` (diante de um sprite) e
`gotocreature:N` (de frente para a N-ésima criatura viva), `size:L,A` (resolução das capturas e do bench, ex.: `size:1920,1080`). Com `--gpu` o roteiro usa a placa de vídeo
(janela escondida) e as capturas saem do quadro final da GPU.

## Estrutura

| Pasta | Conteúdo |
|---|---|
| `src/assets` | Leitores dos formatos originais: PKLITE (ACD.EXE), BSA, IMG, COL, fontes, compressões tipo 4 (LZSS) e tipo 8 (LZHUF) |
| `src/render` | Framebuffer indexado 320x200, texto com acentos sintetizados, remoção de texto embutido na arte |
| `src/i18n` | Localização: `data/lang/pt_BR.txt` com fallback para o texto original lido do executável |
| `src/game` | Máquina de telas com fade; regras do jogo (`ArenaData`); criação de personagem (`CharacterCreation`); diálogos no estilo original (`Dialog`) |
| `src/world` | Leitores .MIF (camadas FLOR/MAP1/MAP2) e .INF (texturas, plataformas, sprites; decifra os do BSA) e o nível jogável (`Level`); estilo HD das texturas (`TextureStyle`) |
| `src/game/Creatures.*` | Criaturas: animações .CFA por direção (8, com espelhamento), IA (vagar, perceber por visão/audição, perseguir, atacar) e morte |
| `src/audio` | Leitor .VOC e mixer de efeitos (SDL), leitor .XMI e música pelo sintetizador MIDI do Windows |
| `src/gpu` | Renderização OpenGL 3.3: shaders da cena, reflexos, bloom, composição do HUD e telas 2D |
| `src/render3d` | Renderizador 3D por software (z-buffer, perspectiva correta, iluminação por pixel com relevo, reflexos, bloom, pool de threads) e HUD |
| `src/main.cpp` | Janela SDL2, escala "sharp bilinear" em 4:3, entrada, configurações |
| `tools/arena_tool.cpp` | Utilitário: descompactar EXE, listar/extrair BSA, converter IMG para PNG, amostra de fontes |
| `data/` | Traduções, mapa de textos do ACD.EXE, `settings.ini`, `mods/` (substitui qualquer asset) |

### Tradução

- Todo texto do jogo vem de chaves `Secao.Chave`. `data/exe/acd_strings.txt` aponta onde
  cada texto em inglês está no ACD.EXE descompactado; `data/lang/pt_BR.txt` sobrescreve.
- Chave sem tradução mostra o inglês original; chave inexistente aparece como `[Chave]`.
- As fontes do Arena só têm ASCII; á é í ó ú â ê ô ã õ à ç (e maiúsculas) são gerados
  em pixel art a partir da letra base.
- Texto desenhado dentro de imagens (menu principal, mapa de Tamriel, ficha de atributos)
  é apagado por inpainting e reescrito em PT-BR com as fontes originais.
- As 40 perguntas de classe estão em `data/lang/pt_BR_questions.txt` (mesmo formato do
  QUESTION.TXT; a marcação `(5l)`/`(5c)`/`(5v)` define a categoria e deve ser mantida).
- Chaves com sufixo `_F` são usadas para personagens femininas (Maga, Bretã, conhecida...).

### Áudio, gatilhos e travas

- Efeitos .VOC originais com volume/pan pela posição: passos (pedra/água), golpes, portas,
  fechadura, gritos e rosnados das criaturas (tabela de sons do ACD.EXE), gotas/tambores/vento
  dos gatilhos do mapa.
- Música .XMI convertida em eventos MIDI e tocada pelo sintetizador do Windows: menu
  (PERCNTRO), criação (SHEET) e masmorra (DUNGEON2–5). Volumes em `[Audio]` no settings.ini.
- Gatilhos do MIF mostram os textos do .INF traduzidos em `[Triggers]` do pt_BR.txt
  (chave `Triggers.<INF>.<n>`). O texto "+N" entrega a chave N.
- Na prisão, a grade da cela tem trava nível 10: é preciso pegar a chave de rubi no canto da
  cela (ou arrombar, se a classe for de ladrão).

### Gráficos

- **Placa de vídeo (padrão):** OpenGL 3.3 core criado pelo SDL, com carregador de funções próprio
  (`src/gpu/GlDevice`). A cena vai para shaders com a mesma iluminação do renderizador por software
  (relevo, água/lava, oclusão de ambiente, chamas) e os efeitos de tela também rodam na GPU: reflexos
  na água (marcha no buffer de profundidade), bloom em 1/4 da resolução, vinheta e fade. Texturas HD
  usam mipmaps (sem cintilação à distância). O HUD, a arma e as janelas são desenhados pela CPU numa
  camada transparente composta por cima; as telas 2D usam um shader "sharp bilinear".
  Medido numa RX 7600 em 1920x1080 nativo com todos os efeitos: 7–10 ms por quadro (105–150 FPS).
- Sem OpenGL 3.3 (ou com `Renderer=cpu` / `--cpu`) o jogo usa o renderizador por software abaixo.
- **Estilo HD (padrão):** cada textura de 64x64 é ampliada 4x por interpolação bicúbica, recebe um
  grão fino de pedra procedural e é lida com filtro bilinear; paredes, pisos e tetos ganham um mapa
  de relevo derivado da própria imagem (juntas escuras ficam fundas), iluminado pela tocha com um
  brilho leve de pedra úmida. Sprites e criaturas são ampliados com o contorno preservado (Scale2x).
  Nada disso cria arte nova: o desenho continua o original.
- **Clássico:** `HDTextures=0` (ou `--classico`, que desliga todos os efeitos) volta aos pixels originais.
- Água e lava usam as animações originais WATERANI.RCI e LAVAANI.RCI (5 quadros de 320x100);
  as texturas SWATER/SLAVA.IMG citadas no .INF são só marcadores quase pretos.
- A superfície fica um pouco abaixo do chão, com margens de pedra, ondulação animada, brilho
  especular da tocha e reflexos em espaço de tela (o raio refletido percorre o z-buffer).
- Lava brilha sozinha, pulsa e ilumina os arredores.
- Luz difusa pela normal das superfícies, oclusão de ambiente nos cantos (chão/teto junto a
  paredes e encontros de paredes), chamas de velas/braseiros que brilham no escuro, bloom e vinheta.
- A cena é enfileirada, ordenada da frente para trás e rasterizada em faixas pequenas distribuídas
  dinamicamente entre um pool de threads fixo; reflexos (em xadrez), bloom e vinheta também rodam
  em paralelo. Medido em 12 threads: janela Full HD com `RenderScale=0.6` (1152x648) ≈ 9–13 ms por
  quadro no estilo HD; 1920x1080 nativo (`RenderScale=1.0`) ≈ 24–33 ms; clássico 1080p ≈ 11–14 ms.
- Cada efeito liga/desliga em `[Graphics]` no `settings.ini`.

### Interface

- Toda a interface do jogo é desenhada em resolução nativa (`src/render3d/UiKit`): molduras de madeira
  com filete dourado e cravos, pergaminho procedural, botões em relevo, barras e listas, com as fontes
  góticas do próprio Arena ampliadas por um fator inteiro (2x em 720p, 3x em 1080p).
- HUD: local e efeitos mágicos ativos (canto superior esquerdo), bússola em faixa com a criatura sob a
  mira (topo), minimapa emoldurado (canto superior direito), barras de vida/vigor/magia (inferior esquerdo),
  arma, feitiço preparado, ouro e experiência (inferior direito) e mensagens numa faixa central.
- Janela do personagem (`src/game/GameMenus`): retrato com o boneco vestido e barras à esquerda; abas
  Personagem (atributos, habilidades, efeitos), Inventário (lista com peso, detalhes, Equipar/Tirar/Usar e
  Largar) e Feitiços (lista com custo, detalhes e Preparar). Clique escolhe; clique de novo usa.
- Saque, pausa (Continuar / Personagem / Feitiços / Controles / Sair) e morte com botões clicáveis;
  H mostra o quadro de controles. O mouse usa a posição na tela inteira (`InputEvent::viewX/viewY`);
  nos roteiros, `vclick:x,y` e `vmove:x,y` recebem frações da tela.

### Magia

- Os 64 feitiços padrão vêm do `SPELLSG.65` (registros de 85 bytes: parâmetros [6][3], alvo, elemento,
  até 3 efeitos com sub-efeito e atributo, custo e nome). As descrições seguem os modelos do
  `SPELLMKR.TXT`, traduzidos em `[SpellText]`; os nomes estão em `[Spells]` no pt_BR.txt.
- Livro de feitiços na aba Feitiços da janela do personagem (B). Até 32 feitiços, gravados na seção
  `[Spellbook]` do save junto com a magia atual.
- O original vende feitiços na guilda dos magos; o remake dá um livro inicial às classes que conjuram:
  Dardo de Fogo, Cura Leve e Luz, mais um ou dois da classe (Escudo para o Espadachim Arcano, Raio de Gelo e
  Abrir para o Mago, Cura e Curar Veneno para o Curandeiro...).
- Custo em magia: custo do arquivo ÷ 10 (no original é custo ÷ nível do personagem; ÷ 10 equivale a um
  personagem de nível 10, até existirem níveis). 1 rodada = 5 s reais, como no original.
- Conjurar mostra as mãos (`SPELL.IMG`) e a animação do elemento (`FIRE/ICE/SHOCK/MAGIC/SPARKLE.CFA`).
  Alvos: conjurador, toque, projétil (esfera luminosa com luz própria), área ao redor e área à distância.
- Efeitos implementados: dano, dano contínuo, veneno, paralisia (a criatura congela), roubo de vida,
  cura de vida/magia, luz (globo que acompanha o jogador), escudo mágico (absorve golpes), invisibilidade
  e Santuário (as criaturas não notam o jogador), resistências, reforço de atributo (Força aumenta o dano),
  regeneração e Abrir (destranca a porta à frente). Os demais avisam que ainda não existem nesta versão.
- Magia volta ao descansar (exceto Feiticeiro, como no original) e com a Poção de Restaurar Magia.

### Combate

- Criaturas vêm dos marcadores do mapa (*ITEM 32–54 do .INF) com vida, dano, nível, escala e
  experiência das tabelas do ACD.EXE (Rato: vida 1–6, dano 1–4; Goblin: vida 1–8, dano 1–5).
- A arma equipada (ver Inventário) define dano e animação; sem arma, punhos (1–3; Monge 1–8).
- Acerto do jogador: 55% + 5% × bônus de acerto (Agilidade) − 5% por nível da criatura acima do 1º;
  dano = arma + bônus de dano (Força). Acerto da criatura: 45% + 5% × nível − 5% × defesa do jogador.
- Experiência ao matar = experiência base + vida máxima × multiplicador (fórmula do original).
- A armadura vestida protege a parte do corpo atingida (peito, mãos, pernas, ombros, cabeça,
  pés; o escudo soma a todas): −5% de chance de acerto da criatura por ponto de proteção.
- Ao morrer: Enter recomeça na cela, S volta ao menu. Fórmulas exatas do original para
  acerto/defesa ainda são simplificações.

### Inventário, equipamento e saque

- Tabelas lidas do ACD.EXE: 18 armas (dano, peso, uma/duas mãos), 11 peças de armadura
  (placas/malha/couro, escudos), 8 materiais (Ferro…Ébano, bônus de dano/acerto/proteção) e 15 poções.
- Kit inicial por classe (arma de aço + armadura permitida + Poção de Cura). A arma equipada
  define o dano e a animação em primeira pessoa (SWORD/AXE/MACE/STAR/HAMMER/STAFF.CIF).
- Restrições do original: armadura permitida pela classe, arma de duas mãos não combina com escudo;
  arcos ainda não podem ser usados. Limite de carga = Força × 2 kg.
- Ficha em jogo (CHARSTAT/EQUIP.IMG) com o boneco vestido pelas peças de 0EQUIP/1EQUIP.CIF.
- Saque (NEWPOP.IMG): pilhas de tesouro/equipamento, baús, restos mortais do mapa (*ITEM 2–7)
  e corpos das criaturas mortas (chances de saque por criatura do ACD.EXE). Pilhas vazias somem.
- Vida, experiência, ouro e inventário são gravados em `saves/personagem.ini` (seção `[Inventory]`).

### Sprites redesenhados (PNG)

`arena_tool export-sprites <pasta ARENA> <saída>` exporta todas as imagens do original em PNG,
com a paleta certa e um `indice.csv` (tamanho e alinhamento de cada quadro). Para usar sprites
redesenhados, coloque os PNG em `data/mods/` (ao lado do executável, em qualquer subpasta), com o
mesmo nome da exportação (`GOBLIN1_CFA_000.png`, `RAT1_CFA_003.png`, `BRICK_IMG.png`...). O PNG
pode ter o tamanho original (recebe a ampliação HD) ou um múltiplo exato, como 2x ou 4x (usado
como está, com relevo nas paredes). Vale para paredes, pisos, tetos, sprites do mapa, criaturas,
arma, feitiços, arte de título, mapa de Tamriel, rostos e o boneco da ficha (montado em RGBA na
maior escala entre as peças). No mundo 3D, alfa abaixo de 50% vira recorte; na interface as bordas
semitransparentes são misturadas.

### Menu principal e criação de personagem

O menu principal mostra a arte de abertura com uma faixa de botões embaixo: Continuar (com nome,
nível e local do último jogo), Carregar jogo, Novo jogo e Sair. A criação é um assistente em
seis passos, com as regras do original: classe por 10 perguntas sorteadas entre as 40 (ou direto
na lista das 18 classes) → nome e gênero → província no mapa de Tamriel (define a raça) →
atributos (base da raça + rolagem + pontos de bônus, com +/- e "Rolar de novo") → rosto → resumo.

### Níveis

Como no original: o nível 2 pede a experiência-base da classe e cada nível seguinte pede 1,875×
o anterior até o 10º (1,5× depois). Ao subir, a vida máxima cresce 1 + dado da classe + bônus de
Resistência, e o jogador distribui de 3 a 6 pontos de atributo; vigor e magia são recalculados.

### Salvar e carregar

Dez espaços (`saves/jogo1.ini` a `jogo10.ini`) pela pausa, pela tela de morte ou pelo menu
principal, além do salvamento automático (`saves/personagem.ini`). O save guarda o personagem e o
estado do mundo: posição, portas abertas e trancadas, gatilhos já lidos, criaturas mortas e o que
sobrou em baús e corpos.

### Portão de Transição

Ao tocar o Portão de Transição (canto sudoeste da prisão), um clarão leva o jogador para fora e a
visão de Ria Silmane encerra o primeiro capítulo, com um resumo da fuga. O jogo é salvo como
"Fora das Masmorras". As cidades e a viagem por Tamriel ainda não existem, então dá para continuar
explorando a prisão.

### Cidades

As cidades são geradas como no original, com os dados do próprio jogo: os locais vêm do
`CITYDATA.65`, o clima do `TERRAIN.IMG` e as tabelas do `ACD.EXE` (cidades costeiras, moldes,
cantos iniciais e quarteirões reservados). O molde (`CITY/TOWN/VILLAGE[W]n.MIF`) recebe
quarteirões de 20x20 (`EQ/MG/NB/TP/TV/TS/BSBDnX.MIF`) sorteados pela semente do local com o
gerador do Arena (x 7143469), então cada cidade sai igual à do jogo original. O `.INF` segue o
clima (`TCN/DCN/MCN.INF`), com céu aberto, luz do dia e andares de cima (MAP2).

Ao atravessar o Portão de Transição, o painel oferece seguir para uma cidade da província natal.
O save guarda a cidade (`Map=CITY:província:local`). As portas levam aos interiores como no
original: o `*MENU` da textura da porta diz o tipo (loja, taverna, guilda dos magos, templo, casa,
casa nobre, palácio) e a variação do `.MIF` (TAVERN1..8 etc.) sai da posição da porta. Andar contra
a porta ou apertar E entra; a porta de dentro devolve à rua, na frente do prédio. 

### Moradores

As ruas têm moradores como no original: animações de andar e parado pelo clima
(`MLGEN0nT/D/W.CFA`, `FMGEN0n.CFA`...), roupas com cores sorteadas e pele pela raça da província
(tabelas `CitizenColorBase`/`CitizenSkinColors` do executável). Andam pelas ruas nas quatro
direções, viram nas esquinas, param de vez em quando e desviam do jogador. Com E diante de um
morador, abre a conversa: o nome dele (gerado pelas regras do original com o `NAMECHNK.DAT`),
"Quem és tu?", "Onde fica...?" (taverna, loja, templo, guilda e palácio, com a direção pela bússola
e a distância a partir do morador) e boatos.

### Ermo e viagem

Os portões da cidade levam ao ermo em volta dela, gerado como no original: 64x64 blocos de 64x64
voxels (`WILDnnn.RMD`) sorteados pela semente do nome da cidade, com as listas de blocos do
executável (comum, vilarejo, masmorra, taverna, templo) e as muralhas da cidade no centro. Aqui
só uma janela de 10x10 blocos (640x640 voxels) em volta da cidade é montada; a borda dela é um
limite invisível. O `.INF` do ermo segue o clima e o tempo (`TWN`, `MWS`...). Taverna, templo,
casas, criptas e torres do ermo têm interior. O portão
visto de fora leva de volta para dentro da cidade.

No ermo surgem criaturas fora da vista (a 18..28 passos, atrás ou dos lados) e somem quando ficam
longe (45 passos). De dia são poucas (até 2 por perto) e só bichos do campo: ratos, goblins,
homens-lagarto, lobos (lobos da neve nas montanhas), orcs, aranhas e trolls. À noite são mais (até
6) e aparecem mortos-vivos: esqueletos, carniçais, cães infernais, fantasmas, zumbis e espectros.
Só criaturas até 2 níveis acima do jogador. O ermo não guarda criaturas no save.

Com **T** (na cidade ou no ermo) abre a viagem por Tamriel: o mapa com as províncias, a lista dos
locais da província com os dias de viagem (pelo terreno do mapa-múndi e as tabelas de velocidade
do executável, como no original) e os locais marcados no mapa. Ao viajar, os dias passam e o
jogador chega aos arredores do destino, do lado de fora do portão. A Cidade Imperial usa o mapa
pronto do original (`IMPERIAL.MIF`).

### Masmorras

As masmorras aleatórias são geradas como no original: blocos de 32x32 do `RANDOM1.MIF` (um dos
4 conjuntos) sorteados pela semente, 1 ou 2 andares, escadas (`*LEVELUP`/`*LEVELDOWN`) em blocos
sorteados e uma borda de parede. As entradas do ermo usam a semente da província e do bloco do
ermo (2x2 blocos de 32). Os monstros das masmorras geradas são sorteados ao entrar, mais fortes
com o nível do jogador (semente pelo nome do mapa, para o save reencontrar os mesmos).

As masmorras com nome do `CITYDATA` aparecem na viagem (T), marcadas em roxo no mapa. As duas de
cada província ligadas à missão principal usam o mapa pronto do original (`NNNNNNNN.MIF`, pelo
número da semente, com os monstros que o mapa traz); as outras são geradas. A escada de subida do
andar de cima leva para fora (aos arredores da cidade mais próxima, quando se chegou viajando).

### Cutscenes

As animações do original (.FLC) tocam em tela cheia: o livro do `INTRO.FLC` ao abrir o jogo, o
Imperador e a traição de Tharn (`KING.FLC`, `MORPH.FLC`) no novo jogo e Ria Silmane
(`VISION.FLC`, mais `CHAOSVSN.FLC` no primeiro sonho) antes de cada sonho; no Palácio Imperial,
Tharn (`JAGAR.FLC`), a câmara da Joia (`WALK.FLC`) e o final (`JAGARDTH`, `END01`, `END02`, `NUKING`). Qualquer tecla ou
clique pula a cena atual; Esc pula todas. Para trocar por versões redesenhadas, coloque PNGs em
`data/mods/` (em qualquer subpasta) com os nomes da exportação: `INTRO_000.png`, `INTRO_001.png`...
Com até o número original de quadros, cada PNG troca o quadro de mesmo número e os que faltarem
continuam os originais; com mais quadros, a cena passa a ser só os PNGs, na mesma duração total
(o dobro de quadros deixa o movimento duas vezes mais fluido). PNGs em 16:10 (como o 320x200
original) são esticados para 4:3 como num monitor VGA; outras proporções aparecem como são.

### Missão principal

Os oito pedaços do Cajado do Caos, na ordem de províncias do executável (Hammerfell, Skyrim,
Valenwood, Elsweyr, Summerset, Rocha Alta, Pântano Negro e Morrowind). Ao descansar (a partir do
nível 2), Ria Silmane aparece em sonho com o lugar do próximo pedaço (textos do `TEMPLATE.DAT`,
traduzidos). Nas cidades da província certa, o primeiro boato aponta a masmorra da pista, que passa
a aparecer no mapa de viagem; o item da pista (`*ITEM 13` do mapa pronto) revela a masmorra do
pedaço, e o pedaço em si leva ao próximo sonho. As masmorras da missão ficam escondidas no mapa
até serem descobertas. Com os oito pedaços, o último sonho manda ao Palácio Imperial: viajar para a
Cidade Imperial leva direto ao mapa pronto do original (`IMPPAL.MIF`, 4 andares), onde Tharn recebe o
jogador. No último andar está a Joia do Fogo; tocá-la com o Cajado mata Tharn (a 24ª criatura do
executável, `*ITEM 73`) e mostra o final. Simplificado: no original o item da pista é entregue a um
rei ou sábio.

### Dia e noite

O relógio anda como no original (13 segundos de jogo por segundo real; um dia dura cerca de 1h50)
e fica salvo no personagem (`Time=`). O HUD mostra a hora e a data ao lado da bússola. Nas cidades
o céu e a luz mudam com a hora (amanhecer das 5h30 às 7h, entardecer das 17h30 às 19h, com o
horizonte alaranjado), a música troca para `NIGHT.XMI` às 17h45, só um em cada quatro moradores
fica na rua à noite, e lojas, guilda, casas nobres e palácio fecham das 21h às 6h (taverna e
templo ficam abertos). Dormir num quarto da taverna leva até as 6h; descansar (R) passa duas horas.

As janelas usam a cor dinâmica do original (índice 113 da paleta): escuras de dia e acesas à noite,
com a transição da tabela `WindowTwilightColors` do executável. Lampiões e tochas das ruas ficam
apagados de dia (versão `...DA.IMG`) e acendem às 17h45, iluminando a rua. O tempo é sorteado por
cidade e por dia: limpo, nublado, chuva ou neve (neve no clima de montanha; quase sem chuva no
deserto), com céu cinza, luz mais fraca, chuva ou flocos na tela e a música do tempo
(`OVERCAST`, `RAINING`, `SNOWING.XMI`).

### Comércio e serviços

Com E diante do atendente (taverneiro, ferreiro, sacerdote ou mago) abre a janela de serviços,
com os preços do executável (armas, armaduras, multiplicador do material, poções e bebidas) e um
desconto de 10% nas vilas e 20% nas aldeias:
- **Loja de equipamentos:** estoque fixo por loja (armas e armaduras com materiais sorteados pela
  tabela do original); vende pela metade do preço (o que está equipado precisa ser tirado antes).
- **Taverna:** as 15 bebidas do original e quatro quartos (uma noite recupera vida e magia).
- **Templo:** cura dos ferimentos (2 de ouro por ponto de vida, mínimo 10).
- **Guilda dos Magos:** os feitiços do livro do original que o personagem ainda não conhece (o
  dobro do custo-base em ouro) e as poções.

## Roteiro

1. ~~Formatos base, título e menu principal em PT-BR~~ ✅
2. ~~Criação de personagem completa (perguntas de classe, lista de classes, nome, gênero, raça no mapa, atributos, rosto)~~ ✅
3. Animações FLC (abertura, cenas) e áudio (VOC + música XMI/MIDI via FluidSynth)
4. Mundo 3D — **em andamento**: ~~Prisão Imperial (START.MIF/INF) em widescreen com olhar livre, portas, água, plataformas, sprites e luzes~~ ✅; ~~criaturas (ratos e goblins) com animações CFA, IA e combate corpo a corpo~~ ✅; ~~sons, música, gatilhos de texto e travas~~ ✅; ~~inventário, equipamentos, saque e ficha em jogo~~ ✅; ~~gráficos modernos (água/lava animadas com reflexos, luz difusa, oclusão de ambiente, bloom, renderização em várias threads)~~ ✅; ~~texturas HD com detalhe e relevo~~ ✅; ~~renderização pela placa de vídeo (OpenGL)~~ ✅; ~~magias (livro, conjuração, projéteis e efeitos)~~ ✅; ~~níveis, salvar em 10 espaços e Portão de Transição~~ ✅; ~~sprites redesenhados em PNG~~ ✅; ~~primeira cidade gerada como no original~~ ✅; ~~interiores, comércio e serviços~~ ✅; ~~moradores, diálogos, dia e noite e tempo~~ ✅; ~~ermo e viagem por Tamriel~~ ✅; ~~masmorras do ermo e com nome~~ ✅; ~~criaturas no ermo~~ ✅; faltam outros andares/masmorras, guildas e criação de feitiços
5. Cidades, masmorras, NPCs, diálogos, lojas, guildas, magias, combate
6. Viagem pelo mapa de Tamriel, missões principais e importação de saves originais
7. Extras modernos: remapeamento de controles, gamepad, interface escalável, autosave

## Créditos

- Formatos de arquivo e mapa de textos baseados em pesquisa do projeto
  [OpenTESArena](https://github.com/afritz1/OpenTESArena) (MIT, © The OpenTESArena Developers).
- *The Elder Scrolls: Arena* © Bethesda Softworks / ZeniMax Media. Este projeto não é afiliado.
