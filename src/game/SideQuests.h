#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace arena
{
	class Game;
	class ArenaData;
	struct CityLocation;
	struct CharacterDraft;

	// Trabalho pago (missões secundárias do original, modelos #0005..#0011 do TEMPLATE.DAT):
	// o governante (no palácio, de dia) pede entregas, buscas, escoltas, resgates e caçadas; o
	// taverneiro, entregas a outra taverna e achados em masmorras. Um trabalho por vez, com prazo.
	struct SideJob
	{
		enum class Kind { None, Deliver, Fetch, Escort, Rescue, Monster, TavernDeliver, TavernFind };
		Kind kind = Kind::None;
		int giver = -1;      // lugar de quem pediu (jobs::code)
		int target = -1;     // cidade de destino ou masmorra (jobs::code)
		int reward = 0;
		double deadline = 0; // minutos do jogo
		int step = 0;        // 0 = ir ao destino, 1 = voltar a quem pediu
		int subject = 0;     // item, parente ou criatura (índice do texto / da criatura)
		std::string name;    // nome do parente
		bool active() const { return this->kind != Kind::None; }
		bool fromRuler() const { return this->kind != Kind::TavernDeliver && this->kind != Kind::TavernFind; }
		// O alvo fica no fundo de uma masmorra (pessoa, criatura ou objeto).
		bool inDungeon() const { return this->kind == Kind::Rescue || this->kind == Kind::Monster || this->kind == Kind::TavernFind; }
	};

	// Busca de artefato: a informação comprada na taverna aponta a masmorra (ver Artifacts.h).
	struct ArtifactHunt
	{
		int artifact = -1;
		bool active() const { return this->artifact >= 0; }
	};

	namespace jobs
	{
		// Lugar como número único (província × 100 + local), já que masmorras (32..47) não têm id global próprio.
		int code(const CityLocation &place);
		const CityLocation *place(const ArenaData &data, int code);

		// Trabalho oferecido hoje em `here` (palácio ou taverna); nada em alguns dias.
		std::optional<SideJob> offer(const Game &game, const CityLocation &here, bool ruler, uint32_t seed);
		// Texto da proposta (o jogador aceita ou recusa).
		std::string offerText(const Game &game, const SideJob &job);
		// Anota no diário o trabalho aceito.
		void accept(Game &game, const SideJob &job);

		// Ao entrar no palácio (ruler) ou falar com o taverneiro de `here`: entrega, pagamento ou
		// fracasso do trabalho atual. Título e texto vazios = nada a dizer.
		struct Outcome { std::string title, text; };
		Outcome arrive(Game &game, const CityLocation &here, bool ruler);
		// O alvo da masmorra foi achado (pessoa resgatada, criatura morta, objeto encontrado).
		std::string targetDone(Game &game);
		std::string targetName(const Game &game, const SideJob &job);

		// Artefato de que o estranho da taverna fala (-1 = todos achados) e quanto ele cobra.
		int artifactRumor(const CharacterDraft &character, uint32_t seed);
		int artifactInfoPrice(const CharacterDraft &character);
		std::string artifactOfferText(const Game &game, int artifact);
		std::string artifactRevealText(const Game &game, int artifact);

		// Linhas do diário: trabalho atual e artefato procurado (vazio se nenhum).
		std::string objective(const Game &game);
	}
}
