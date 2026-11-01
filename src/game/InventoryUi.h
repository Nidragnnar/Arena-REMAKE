#pragma once

#include <string>
#include <vector>

#include "Game.h"
#include "Items.h"

namespace arena
{
	// Nome traduzido e linha de detalhes de um item ("Espada curta (Aço)", "Dano 1-8  Peso 3 kg").
	std::string itemName(const Game &game, const Item &item);
	std::string itemDetails(const Game &game, const Item &item);

	// Peso carregado e limite (Força x 2 kg, como no original).
	float carriedWeight(const Game &game);
	float maxCarryWeight(const Game &game);

	// Desenha o personagem com roupas, rosto e o equipamento vestido (0EQUIP/1EQUIP.CIF).
	class PaperDoll
	{
	public:
		void load(Game &game);
		void draw(Canvas &canvas, const Game &game) const;
		int getWidth() const { return this->body.width; } // largura do fundo (o boneco fica na direita do 320x200)

		// Boneco montado em RGBA (0xAABBGGRR) e recortado, já com os PNG redesenhados de cada
		// peça; sai na maior escala entre as peças (1 = 320x200 original).
		void compose(const Game &game, std::vector<uint32_t> &pixels, int &width, int &height) const;

	private:
		// Peças na ordem de desenho, com o arquivo e o quadro de origem.
		struct Part { const IndexedImage *image; std::string source; int frame, frameCount, x, y; };
		std::vector<Part> parts(const Game &game) const;

		IndexedImage body, shirt, pants;
		std::vector<IndexedImage> heads, equipment;
		std::string bodyName, shirtName, pantsName, headsName, equipmentName;
		std::array<int, 2> shirtOffset{}, pantsOffset{};
	};

	// Resultado de uma interação com as janelas sobre o mundo.
	struct OverlayResult
	{
		bool close = false;
		bool inventoryChanged = false; // arma/armadura trocada ou item usado
		int healthDelta = 0;
		int spellPointsDelta = 0;
		bool rested = false; // dormiu: vida e magia cheias
		int request = 0;     // conversa no balcão da taverna: 1 = trabalho, 2 = artefatos
		int castSpell = -1;  // item mágico usado: feitiço a lançar sem gastar magia
		bool crime = false;  // pego furtando no balcão: chama a guarda
		std::string message;
	};

	// Equipa/tira/usa o item (poções somem); notice recebe o motivo quando não é possível.
	OverlayResult useInventoryItem(Game &game, int index, int currentHealth, int currentSpellPoints, std::string &notice);
	// Pega a linha do saque (0 = ouro, se houver); notice avisa se pesar demais.
	OverlayResult takeLoot(Game &game, Loot &loot, int row, std::string &notice);
	std::string kilogramsText(float kg);
}
