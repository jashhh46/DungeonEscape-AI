#include <SFML/Graphics.hpp>

#include "ai/behaviour_tree/BehaviourTree.hpp"
#include "ai/enemies/ArcherDecision.hpp"
#include "ai/enemies/ArcherCombat.hpp"
#include "ai/enemies/ArcherPathfinding.hpp"
#include "ai/enemies/SkeletonCombat.hpp"
#include "ai/behaviour_tree/EnemyContext.hpp"
#include "ai/navigation/GridCollision.hpp"
#include "ai/enemies/SkeletonDecision.hpp"
#include "ai/enemies/SkeletonPathfinding.hpp"
#include "ai/enemies/OgreDecision.hpp"
#include "ai/enemies/OgrePathfinding.hpp"
#include "ai/enemies/OgreCombat.hpp"
#include "player/PlayerAttack.hpp"
#include "game/EnemyHealth.hpp"
#include "player/PlayerCombat.hpp"
#include "game/GameSession.hpp"
#include "evaluation/EvaluationLogger.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

enum class TileType
{
	Floor,
	Wall
};

bool isWalkable(TileType tile)
{
	return tile == TileType::Floor;
}

void drawTextureRegion(sf::RenderTarget& target,
	const sf::Texture& texture,
	int sourceX, int sourceY, int sourceWidth, int sourceHeight,
	sf::Vector2f destination, float destinationWidth, float destinationHeight)
{
	sf::Sprite part(texture, sf::IntRect(
		{ sourceX, sourceY }, { sourceWidth, sourceHeight }));
	part.setPosition(destination);
	part.setScale({ destinationWidth / sourceWidth, destinationHeight / sourceHeight });
	target.draw(part);
}

template <typename Grid>
void drawConnectedWallTile(sf::RenderTarget& target,
	const sf::Texture& texture,
	const Grid& dungeon,
	int row,
	int column,
	sf::Vector2f position,
	float tileSize)
{
	constexpr int sideEdge = 4;
	constexpr int capHeight = 16;
	constexpr int bodyTop = 16;
	constexpr int bodyHeight = 76;
	constexpr int bottomCapTop = bodyTop + bodyHeight;

	const bool wallAbove = row > 0 && dungeon[row - 1][column] == TileType::Wall;
	const bool wallBelow = row + 1 < static_cast<int>(dungeon.size()) &&
		dungeon[row + 1][column] == TileType::Wall;
	const bool wallLeft = column > 0 && dungeon[row][column - 1] == TileType::Wall;
	const bool wallRight = column + 1 < static_cast<int>(dungeon[row].size()) &&
		dungeon[row][column + 1] == TileType::Wall;
	const bool topCap = !wallAbove;
	const bool bottomCap = !wallBelow;
	const bool leftEnd = !wallLeft;
	const bool rightEnd = !wallRight;

	const float insetTop = topCap ? static_cast<float>(capHeight) : 0.f;
	const float insetBottom = bottomCap ? static_cast<float>(capHeight) : 0.f;
	const float bodyDestinationY = position.y + insetTop;
	const float bodyDestinationHeight = tileSize - insetTop - insetBottom;

	// Fill the connected face from the brick area, without repeating its border bands.
	drawTextureRegion(target, texture, sideEdge, bodyTop,
		128 - sideEdge * 2, bodyHeight,
		{ position.x, bodyDestinationY }, tileSize, bodyDestinationHeight);

	if (leftEnd)
		drawTextureRegion(target, texture, 0, bodyTop, sideEdge, bodyHeight,
			position, static_cast<float>(sideEdge), bodyDestinationHeight);
	if (rightEnd)
		drawTextureRegion(target, texture, 128 - sideEdge, bodyTop, sideEdge, bodyHeight,
			{ position.x + tileSize - sideEdge, bodyDestinationY },
			static_cast<float>(sideEdge), bodyDestinationHeight);

	if (topCap)
	{
		drawTextureRegion(target, texture, sideEdge, 0, 128 - sideEdge * 2, capHeight,
			position, tileSize, static_cast<float>(capHeight));
		if (leftEnd)
			drawTextureRegion(target, texture, 0, 0, sideEdge, capHeight,
				position, static_cast<float>(sideEdge), static_cast<float>(capHeight));
		if (rightEnd)
			drawTextureRegion(target, texture, 128 - sideEdge, 0, sideEdge, capHeight,
				{ position.x + tileSize - sideEdge, position.y },
				static_cast<float>(sideEdge), static_cast<float>(capHeight));
	}
	if (bottomCap)
	{
		const sf::Vector2f bottomPosition{ position.x, position.y + tileSize - capHeight };
		drawTextureRegion(target, texture, sideEdge, bottomCapTop,
			128 - sideEdge * 2, capHeight, bottomPosition, tileSize,
			static_cast<float>(capHeight));
		if (leftEnd)
			drawTextureRegion(target, texture, 0, bottomCapTop, sideEdge, capHeight,
				bottomPosition, static_cast<float>(sideEdge), static_cast<float>(capHeight));
		if (rightEnd)
			drawTextureRegion(target, texture, 128 - sideEdge, bottomCapTop,
				sideEdge, capHeight,
				{ position.x + tileSize - sideEdge, bottomPosition.y },
				static_cast<float>(sideEdge), static_cast<float>(capHeight));
	}
}

struct GridCell
{
	int column;
	int row;
};

struct EnemySpawn
{
	std::string_view role;
	std::string_view idleSheet;
	GridCell cell;
	float scale;
};

struct EnemyVisual
{
	sf::Texture texture;
	sf::Sprite sprite;

	EnemyVisual() : sprite(texture) {}
};

struct WorldProp
{
	sf::Sprite sprite;
	GridCell cell;
	sf::Vector2f groundPosition;
	float colliderWidth;
	float colliderHeight;
	bool blocksMovement;

	WorldProp(const sf::Texture& texture, sf::IntRect source, GridCell propCell, sf::Vector2f ground,
		float scale, float width, float height, bool solid)
		: sprite(texture, source), cell(propCell), groundPosition(ground), colliderWidth(width),
		  colliderHeight(height), blocksMovement(solid)
	{
		sprite.setOrigin({ source.size.x / 2.f, static_cast<float>(source.size.y) });
		sprite.setScale({ scale, scale });
		sprite.setPosition(groundPosition);
	}
};

bool footprintsOverlap(sf::Vector2f firstGround, float firstWidth, float firstHeight,
	sf::Vector2f secondGround, float secondWidth, float secondHeight)
{
	const float firstLeft = firstGround.x - firstWidth / 2.f;
	const float firstTop = firstGround.y - firstHeight;
	const float secondLeft = secondGround.x - secondWidth / 2.f;
	const float secondTop = secondGround.y - secondHeight;
	return firstLeft < secondLeft + secondWidth && firstLeft + firstWidth > secondLeft &&
		firstTop < secondTop + secondHeight && firstTop + firstHeight > secondTop;
}

int main()
{
	constexpr unsigned int windowWidth = 1024;
	constexpr unsigned int windowHeight = 768;
	constexpr int tileSize = 128;
	constexpr int tilesAcross = 28;
	constexpr int tilesDown = 16;
	constexpr int playerFrameSize = 256;
	constexpr int animationFrameCount = 4;
	constexpr float movementSpeed = 240.f;
	constexpr float idleFramesPerSecond = 3.f;
	constexpr float walkFramesPerSecond = 8.f;

	constexpr float playerColliderWidth = 48.f;
	constexpr float playerColliderHeight = 28.f;
	constexpr float playerColliderTopOffset = 90.f;
	constexpr float skeletonColliderWidth = 28.f;
	constexpr float skeletonColliderHeight = 18.f;
	constexpr std::array<float, 3> enemyFootWidths{
		skeletonColliderWidth, ai::archerFootWidth, ai::ogreFootWidth };
	constexpr std::array<float, 3> enemyFootHeights{
		skeletonColliderHeight, ai::archerFootHeight, ai::ogreFootHeight };
	constexpr float skeletonNavigationGroundOffset = 104.f;
	// Keep the Player's visible art inside the world without using padded art as the collider.
	constexpr float visibleLeftExtent = 88.f;
	constexpr float visibleRightExtent = 86.f;
	constexpr float visibleTopExtent = 71.f;
	constexpr float visibleBottomExtent = 115.f;
	constexpr float dungeonWorldWidth = static_cast<float>(tilesAcross * tileSize);
	constexpr float dungeonWorldHeight = static_cast<float>(tilesDown * tileSize);

	std::array<std::array<TileType, tilesAcross>, tilesDown> dungeon{};

	// Start with floor, then add the outer walls and two-room divider.
	for (int row = 0; row < tilesDown; ++row)
	{
		for (int column = 0; column < tilesAcross; ++column)
		{
			dungeon[row][column] = TileType::Floor;
			if (row == 0 || row == tilesDown - 1 || column == 0 || column == tilesAcross - 1)
				dungeon[row][column] = TileType::Wall;
		}
	}
	for (int row = 1; row < tilesDown - 1; ++row)
	{
		if (row >= 6 && row <= 9)
			continue;
		dungeon[row][13] = TileType::Wall;
		dungeon[row][14] = TileType::Wall;
	}

	constexpr GridCell playerSpawn{ 6, 9 };
	const auto cellCenter = [=](GridCell cell)
	{
		return sf::Vector2f{
			(static_cast<float>(cell.column) + 0.5f) * tileSize,
			(static_cast<float>(cell.row) + 0.5f) * tileSize
		};
	};
	const sf::Vector2f playerStartPosition = cellCenter(playerSpawn);
	const sf::Vector2f playerStartGround{
		playerStartPosition.x,
		playerStartPosition.y + playerColliderTopOffset + playerColliderHeight
	};
	if (!isWalkable(dungeon[playerSpawn.row][playerSpawn.column]) ||
		!ai::isFootprintClear(playerStartGround, playerColliderWidth, playerColliderHeight,
			dungeon, static_cast<float>(tileSize), [](TileType tile) { return isWalkable(tile); }))
	{
		std::cerr << "Player spawn is blocked.\n";
		return 1;
	}

	// Enemy spawn positions are stored as (column, row) tile cells.
	constexpr std::array<EnemySpawn, 3> enemySpawns = {{
		{ "Skeleton Guard", "Assets/characters/skeleton/skeleton_idle_sheet.png", { 10, 6 }, 0.8f },
		{ "Goblin Archer", "Assets/characters/archer/archer_idle_sheet.png", { 22, 4 }, 0.8f },
		{ "Dungeon Ogre", "Assets/characters/orc/orc_idle_sheet.png", { 19, 12 }, 0.9f }
	}};
	std::array<EnemyVisual, enemySpawns.size()> enemies;
	for (std::size_t i = 0; i < enemySpawns.size(); ++i)
	{
		const int column = enemySpawns[i].cell.column;
		const int row = enemySpawns[i].cell.row;
		if (column < 0 || column >= tilesAcross || row < 0 || row >= tilesDown ||
			!isWalkable(dungeon[row][column]))
		{
			std::cerr << "Enemy spawn is not on a walkable tile: " << enemySpawns[i].role << '\n';
			return 1;
		}
		const sf::Vector2f spawnPosition = cellCenter(enemySpawns[i].cell);
		const sf::Vector2f spawnGroundPosition{
			spawnPosition.x,
			spawnPosition.y + (244.f - 128.f) * enemySpawns[i].scale
		};
		if (!ai::isFootprintClear(spawnGroundPosition,
			skeletonColliderWidth * enemySpawns[i].scale,
			skeletonColliderHeight * enemySpawns[i].scale,
			dungeon, static_cast<float>(tileSize),
			[](TileType tile) { return isWalkable(tile); }))
		{
			std::cerr << "Enemy spawn footprint is blocked: " << enemySpawns[i].role << '\n';
			return 1;
		}

		if (!enemies[i].texture.loadFromFile(std::string(enemySpawns[i].idleSheet)))
		{
			std::cerr << "Could not load idle sheet for " << enemySpawns[i].role << ".\n";
			return 1;
		}
		enemies[i].texture.setSmooth(false);
		enemies[i].sprite.setTextureRect(sf::IntRect({ 0, 0 }, { playerFrameSize, playerFrameSize }));
		enemies[i].sprite.setOrigin({ playerFrameSize / 2.f, playerFrameSize / 2.f });
		enemies[i].sprite.setPosition(spawnPosition);
		enemies[i].sprite.setScale({ enemySpawns[i].scale, enemySpawns[i].scale });
	}

	sf::Texture skeletonWalkTexture;
	if (!skeletonWalkTexture.loadFromFile("Assets/characters/skeleton/skeleton_walk_sheet.png"))
	{
		std::cerr << "Could not load the Skeleton Guard walk spritesheet.\n";
		return 1;
	}
	skeletonWalkTexture.setSmooth(false);

	sf::Texture skeletonAttackTexture;
	if (!skeletonAttackTexture.loadFromFile("Assets/characters/skeleton/skeleton_attack_sheet.png"))
	{
		std::cerr << "Could not load the Skeleton Guard attack spritesheet.\n";
		return 1;
	}
	skeletonAttackTexture.setSmooth(false);

	sf::Texture archerWalkTexture;
	if (!archerWalkTexture.loadFromFile("Assets/characters/archer/archer_walk_sheet.png"))
	{
		std::cerr << "Could not load the Goblin Archer walk spritesheet.\n";
		return 1;
	}
	archerWalkTexture.setSmooth(false);

	sf::Texture archerAttackTexture;
	if (!archerAttackTexture.loadFromFile("Assets/characters/archer/archer_attack_sheet.png"))
	{
		std::cerr << "Could not load the Goblin Archer attack spritesheet.\n";
		return 1;
	}
	archerAttackTexture.setSmooth(false);

	sf::Texture ogreWalkTexture;
	if (!ogreWalkTexture.loadFromFile("Assets/characters/orc/orc_walk_sheet.png"))
	{
		std::cerr << "Could not load the Dungeon Ogre walk spritesheet.\n";
		return 1;
	}
	ogreWalkTexture.setSmooth(false);

	sf::Texture ogreAttackTexture;
	if (!ogreAttackTexture.loadFromFile("Assets/characters/orc/orc_attack_sheet.png"))
	{
		std::cerr << "Could not load the Dungeon Ogre attack spritesheet.\n";
		return 1;
	}
	ogreAttackTexture.setSmooth(false);

	sf::Texture floorTexture;
	if (!floorTexture.loadFromFile("Assets/tileset/floor_plain.png"))
	{
		std::cerr << "Could not load the dungeon floor tile.\n";
		return 1;
	}
	floorTexture.setSmooth(false);

	sf::Texture wallTexture;
	if (!wallTexture.loadFromFile("Assets/tileset/wall_a.png"))
	{
		std::cerr << "Could not load the wall tile.\n";
		return 1;
	}
	wallTexture.setSmooth(false);

	sf::Texture propsTexture;
	if (!propsTexture.loadFromFile("Assets/tileset/environment/props.png"))
	{
		std::cerr << "Could not load the dungeon prop atlas.\n";
		return 1;
	}
	propsTexture.setSmooth(false);

	sf::Texture props2bTexture;
	if (!props2bTexture.loadFromFile("Assets/tileset/environment/props2b.png"))
	{
		std::cerr << "Could not load the secondary dungeon prop atlas.\n";
		return 1;
	}
	props2bTexture.setSmooth(false);

	// Props use atlas cutouts, with their visible base aligned to this ground point.
	std::vector<WorldProp> props;
	const auto propGround = [=](GridCell cell)
	{
		return sf::Vector2f{
			(static_cast<float>(cell.column) + 0.5f) * tileSize,
			static_cast<float>(cell.row * tileSize) + 116.f
		};
	};
	const auto addProp = [&](const sf::Texture& texture, sf::IntRect source,
		GridCell cell, float scale, float colliderWidth, float colliderHeight, bool solid)
	{
		props.emplace_back(texture, source, cell, propGround(cell), scale,
			colliderWidth, colliderHeight, solid);
	};
	// Solid props form small clusters; all cells stay floor for navigation.
	addProp(propsTexture, sf::IntRect({ 732, 108 }, { 172, 212 }), { 4, 4 }, 0.48f, 66.f, 24.f, true); // crate
	addProp(propsTexture, sf::IntRect({ 436, 76 }, { 180, 236 }), { 5, 4 }, 0.43f, 58.f, 24.f, true); // barrel
	addProp(props2bTexture, sf::IntRect({ 440, 656 }, { 156, 164 }), { 10, 12 }, 0.50f, 54.f, 22.f, true); // broken column
	addProp(propsTexture, sf::IntRect({ 728, 436 }, { 180, 184 }), { 11, 11 }, 0.42f, 46.f, 20.f, true); // pot
	addProp(propsTexture, sf::IntRect({ 732, 108 }, { 172, 212 }), { 17, 4 }, 0.48f, 66.f, 24.f, true); // crate
	addProp(propsTexture, sf::IntRect({ 436, 76 }, { 180, 236 }), { 18, 4 }, 0.43f, 58.f, 24.f, true); // barrel
	addProp(props2bTexture, sf::IntRect({ 440, 656 }, { 156, 164 }), { 16, 11 }, 0.50f, 54.f, 22.f, true); // broken column
	addProp(propsTexture, sf::IntRect({ 728, 436 }, { 180, 184 }), { 23, 11 }, 0.42f, 46.f, 20.f, true); // pot

	// These atlas regions are transparent skull and bone piles, without tile backgrounds.
	constexpr sf::IntRect skullPileRect({ 364, 424 }, { 252, 196 });
	constexpr sf::IntRect bonePileRect({ 216, 704 }, { 188, 100 });
	constexpr unsigned int decorationSeed = 0xD06E5EED;
	constexpr int decorationCount = 52;
	std::mt19937 decorationRandom(decorationSeed);
	std::uniform_int_distribution<int> offsetInCluster(-1, 1);
	std::uniform_int_distribution<int> decorationKind(0, 99);
	std::uniform_real_distribution<float> horizontalJitter(-42.f, 42.f);
	std::uniform_real_distribution<float> verticalJitter(-28.f, 28.f);
	std::uniform_real_distribution<float> boneScale(0.28f, 0.48f);
	std::uniform_real_distribution<float> skullScale(0.20f, 0.34f);
	std::uniform_real_distribution<float> rotation(-180.f, 180.f);
	std::array<std::array<int, tilesAcross>, tilesDown> decorationsPerCell{};

	const auto decorationCellAllowed = [&](GridCell cell)
	{
		if (cell.column <= 0 || cell.column >= tilesAcross - 1 ||
			cell.row <= 0 || cell.row >= tilesDown - 1 ||
			!isWalkable(dungeon[cell.row][cell.column]))
			return false;
		if (cell.column >= 13 && cell.column <= 14 && cell.row >= 6 && cell.row <= 9)
			return false;

		// Keep a clear tile around every spawn so decorative art does not cover characters.
		if (std::abs(cell.column - playerSpawn.column) <= 1 &&
			std::abs(cell.row - playerSpawn.row) <= 1)
			return false;
		for (const EnemySpawn& spawn : enemySpawns)
		{
			if (std::abs(cell.column - spawn.cell.column) <= 1 &&
				std::abs(cell.row - spawn.cell.row) <= 1)
				return false;
		}
		for (const WorldProp& prop : props)
		{
			if (prop.blocksMovement && prop.cell.column == cell.column && prop.cell.row == cell.row)
				return false;
		}
		return decorationsPerCell[cell.row][cell.column] < 3;
	};

	const auto addRandomDecoration = [&](GridCell cell)
	{
		const bool useSkullPile = decorationKind(decorationRandom) < 45;
		const sf::Texture& texture = useSkullPile ? propsTexture : props2bTexture;
		const sf::IntRect source = useSkullPile ? skullPileRect : bonePileRect;
		const float scale = useSkullPile ? skullScale(decorationRandom) : boneScale(decorationRandom);
		const sf::Vector2f center = cellCenter(cell);
		const sf::Vector2f ground{
			center.x + horizontalJitter(decorationRandom),
			static_cast<float>(cell.row * tileSize) + 92.f + verticalJitter(decorationRandom)
		};
		props.emplace_back(texture, source, cell, ground, scale, 0.f, 0.f, false);
		props.back().sprite.setRotation(sf::degrees(rotation(decorationRandom)));
		++decorationsPerCell[cell.row][cell.column];
	};

	// Cluster around room edges and the solid prop groups, then fill the open floor sparsely.
	constexpr std::array<GridCell, 8> clusterAnchors = {{
		{ 4, 4 }, { 10, 12 }, { 1, 1 }, { 11, 13 },
		{ 17, 4 }, { 16, 11 }, { 26, 1 }, { 26, 13 }
	}};
	int decorationPlaced = 0;
	for (const GridCell anchor : clusterAnchors)
	{
		int clusterPlaced = 0;
		for (int attempt = 0; attempt < 100 && clusterPlaced < 4; ++attempt)
		{
			const GridCell cell{
				anchor.column + offsetInCluster(decorationRandom),
				anchor.row + offsetInCluster(decorationRandom)
			};
			if (!decorationCellAllowed(cell))
				continue;
			addRandomDecoration(cell);
			++clusterPlaced;
			++decorationPlaced;
		}
	}
	std::uniform_int_distribution<int> randomColumn(1, tilesAcross - 2);
	std::uniform_int_distribution<int> randomRow(1, tilesDown - 2);
	for (int attempt = 0; attempt < 4000 && decorationPlaced < decorationCount; ++attempt)
	{
		const GridCell cell{ randomColumn(decorationRandom), randomRow(decorationRandom) };
		if (!decorationCellAllowed(cell))
			continue;
		addRandomDecoration(cell);
		++decorationPlaced;
	}
	if (decorationPlaced != decorationCount)
	{
		std::cerr << "Could not place the full set of remains decorations.\n";
		return 1;
	}

	const auto propCollisionFree = [&](sf::Vector2f ground, float width, float height)
	{
		for (const WorldProp& prop : props)
		{
			if (prop.blocksMovement && footprintsOverlap(ground, width, height,
				prop.groundPosition, prop.colliderWidth, prop.colliderHeight))
				return false;
		}
		return true;
	};
	if (!propCollisionFree(playerStartGround, playerColliderWidth, playerColliderHeight))
	{
		std::cerr << "A solid prop overlaps the Player spawn.\n";
		return 1;
	}
	for (const EnemySpawn& spawn : enemySpawns)
	{
		const sf::Vector2f center = cellCenter(spawn.cell);
		const sf::Vector2f ground{ center.x, center.y + (244.f - 128.f) * spawn.scale };
		if (!propCollisionFree(ground, skeletonColliderWidth * spawn.scale,
			skeletonColliderHeight * spawn.scale))
		{
			std::cerr << "A solid prop overlaps an enemy spawn: " << spawn.role << '\n';
			return 1;
		}
	}
	for (int row = 6; row <= 9; ++row)
	{
		for (int column : { 13, 14 })
		{
			const sf::Vector2f passageGround{
				(static_cast<float>(column) + 0.5f) * tileSize,
				static_cast<float>(row * tileSize) + 104.f
			};
			if (!propCollisionFree(passageGround, 1.f, 1.f))
			{
				std::cerr << "A solid prop blocks the central passage.\n";
				return 1;
			}
		}
	}

	sf::Texture playerIdleTexture;
	if (!playerIdleTexture.loadFromFile("Assets/characters/boss/boss_idle_sheet.png"))
	{
		std::cerr << "Could not load the Player idle appearance sheet.\n";
		return 1;
	}
	playerIdleTexture.setSmooth(false);

	sf::Texture playerWalkTexture;
	if (!playerWalkTexture.loadFromFile("Assets/characters/boss/boss_walk_sheet.png"))
	{
		std::cerr << "Could not load the Player walk appearance sheet.\n";
		return 1;
	}
	playerWalkTexture.setSmooth(false);

	sf::Texture playerAttackTexture;
	if (!playerAttackTexture.loadFromFile("Assets/characters/boss/boss_attack_sheet.png"))
	{
		std::cerr << "Could not load the Player attack spritesheet.\n";
		return 1;
	}
	playerAttackTexture.setSmooth(false);

	// The Player uses the supplied character sheets for appearance only.
	// Their rows face down, up, left, and right; frames are 256x256.
	sf::Sprite player(playerIdleTexture, sf::IntRect({ 0, 0 }, { playerFrameSize, playerFrameSize }));
	player.setOrigin({ playerFrameSize / 2.f, playerFrameSize / 2.f });
	player.setPosition(playerStartPosition);
	ai::PlayerAttackState playerAttackState;

	// The first enemy is the Skeleton Guard; its context and tree belong to this instance.
	ai::EnemyContext skeletonContext;
	skeletonContext.detectionRange = 375.f;
	skeletonContext.attackRange = 60.f;
	skeletonContext.attackRangeBuffer = 10.f;
	skeletonContext.moveSpeed = ai::skeletonMoveSpeed;
	ai::BehaviourTree skeletonTree;
	ai::buildSkeletonBehaviourTree(skeletonTree);
	ai::SkeletonAttackState skeletonAttack;
	ai::PlayerHealth playerHealth;
	ai::GameSessionState gameSession;
	evaluation::EvaluationLogger evaluationLogger;
	double evaluationTimeSeconds = 0.0;
	std::array<ai::EnemyHealth, enemySpawns.size()> enemyHealth{
		ai::EnemyHealth{ 45 }, ai::EnemyHealth{ 35 }, ai::EnemyHealth{ 100 }
	};

	// Archer AI is separate from the Skeleton's decision tree and state.
	ai::EnemyContext archerContext;
	archerContext.detectionRange = 600.f;
	archerContext.moveSpeed = 120.f;
	ai::BehaviourTree archerTree;
	ai::buildArcherBehaviourTree(archerTree);
	ai::ArcherPathState archerPathState;
	ai::ArcherAttackState archerAttackState;
	std::vector<ai::ArcherProjectile> archerProjectiles;
	sf::RectangleShape arrowVisual({ 24.f, 4.f });
	arrowVisual.setOrigin({ 12.f, 2.f });
	arrowVisual.setFillColor(sf::Color(210, 180, 115));

	// The Ogre uses its own decision context and tree.
	ai::EnemyContext ogreContext;
	ogreContext.detectionRange = 450.f;
	ogreContext.attackRange = 85.f;
	ogreContext.moveSpeed = 65.f;
	ai::BehaviourTree ogreTree;
	ai::buildOgreBehaviourTree(ogreTree);
	ai::OgrePathState ogrePathState;
	ai::OgreCombatState ogreCombatState;

	sf::Font uiFont;
	const bool uiFontLoaded = uiFont.openFromFile("C:/Windows/Fonts/arial.ttf");
	sf::Text healthLabel(uiFont, "Player HP: 100 / 100", 22);
	healthLabel.setFillColor(sf::Color::White);

	sf::Sprite floorTile(floorTexture);

	sf::RenderWindow window(sf::VideoMode({ windowWidth, windowHeight }), "Dungeon Escape");
	window.setFramerateLimit(60);
	window.setKeyRepeatEnabled(false);
	sf::View camera(sf::FloatRect({ 0.f, 0.f },
		{ static_cast<float>(windowWidth), static_cast<float>(windowHeight) }));

	auto canOccupy = [&](sf::Vector2f playerPosition)
	{
		// Keep visible Player art inside the dungeon; collision uses only the foot box.
		if (playerPosition.x < visibleLeftExtent ||
			playerPosition.x > dungeonWorldWidth - visibleRightExtent ||
			playerPosition.y < visibleTopExtent ||
			playerPosition.y > dungeonWorldHeight - visibleBottomExtent)
			return false;

		const sf::Vector2f groundPosition{
			playerPosition.x,
			playerPosition.y + playerColliderTopOffset + playerColliderHeight
		};
		if (!ai::isFootprintClear(groundPosition,
			playerColliderWidth, playerColliderHeight, dungeon,
			static_cast<float>(tileSize), [](TileType tile) { return isWalkable(tile); }))
			return false;
		return propCollisionFree(groundPosition, playerColliderWidth, playerColliderHeight);
	};
	const auto skeletonCanOccupy = [&](const sf::Vector2f& groundPosition)
	{
		return ai::isFootprintClear(groundPosition,
			skeletonColliderWidth, skeletonColliderHeight, dungeon,
			static_cast<float>(tileSize), [](TileType tile) { return isWalkable(tile); }) &&
			propCollisionFree(groundPosition, skeletonColliderWidth, skeletonColliderHeight);
	};
	const auto skeletonCellWalkable = [&](ai::GridCell cell)
	{
		const sf::Vector2f ground = ai::gridCellWaypoint(
			cell, static_cast<float>(tileSize), skeletonNavigationGroundOffset);
		return skeletonCanOccupy(ground);
	};
	const auto archerCanOccupy = [&](const sf::Vector2f& groundPosition)
	{
		return ai::isFootprintClear(groundPosition, ai::archerFootWidth,
			ai::archerFootHeight, dungeon, static_cast<float>(tileSize),
			[](TileType tile) { return isWalkable(tile); }) &&
			propCollisionFree(groundPosition, ai::archerFootWidth, ai::archerFootHeight);
	};
	const auto archerCellWalkable = [&](ai::GridCell cell)
	{
		const sf::Vector2f ground = ai::gridCellWaypoint(cell,
			static_cast<float>(tileSize), ai::archerNavigationGroundOffset);
		return archerCanOccupy(ground);
	};
	const auto archerRayCanOccupy = [&](const sf::Vector2f& groundPosition)
	{
		return ai::isFootprintClear(groundPosition, 1.f, 1.f, dungeon,
			static_cast<float>(tileSize), [](TileType tile) { return isWalkable(tile); }) &&
			propCollisionFree(groundPosition, 1.f, 1.f);
	};
	const auto arrowCanOccupy = [&](const sf::Vector2f& center)
	{
		constexpr float arrowWidth = 12.f;
		constexpr float arrowHeight = 6.f;
		const sf::Vector2f ground{ center.x, center.y + arrowHeight / 2.f };
		return ai::isFootprintClear(ground, arrowWidth, arrowHeight, dungeon,
			static_cast<float>(tileSize), [](TileType tile) { return isWalkable(tile); }) &&
			propCollisionFree(ground, arrowWidth, arrowHeight);
	};
	const auto ogreRayCanOccupy = [&](const sf::Vector2f& groundPosition)
	{
		return ai::isFootprintClear(groundPosition, 1.f, 1.f, dungeon,
			static_cast<float>(tileSize), [](TileType tile) { return isWalkable(tile); }) &&
			propCollisionFree(groundPosition, 1.f, 1.f);
	};
	const auto ogreCanOccupy = [&](const sf::Vector2f& groundPosition)
	{
		return ai::isFootprintClear(groundPosition, ai::ogreFootWidth,
			ai::ogreFootHeight, dungeon, static_cast<float>(tileSize),
			[](TileType tile) { return isWalkable(tile); }) &&
			propCollisionFree(groundPosition, ai::ogreFootWidth, ai::ogreFootHeight);
	};
	const auto ogreCellWalkable = [&](ai::GridCell cell)
	{
		const sf::Vector2f ground = ai::gridCellWaypoint(cell,
			static_cast<float>(tileSize), ai::ogreNavigationGroundOffset);
		return ogreCanOccupy(ground);
	};

	sf::RectangleShape hudPanel({ 310.f, 92.f });
	hudPanel.setPosition({ 16.f, 16.f });
	hudPanel.setFillColor(sf::Color(15, 15, 24, 220));
	hudPanel.setOutlineColor(sf::Color(190, 170, 125));
	hudPanel.setOutlineThickness(2.f);
	healthLabel.setPosition({ 26.f, 20.f });
	sf::RectangleShape healthBarBackground({ 280.f, 24.f });
	healthBarBackground.setPosition({ 26.f, 58.f });
	healthBarBackground.setFillColor(sf::Color(65, 30, 35));
	sf::RectangleShape healthBar({ 280.f, 24.f });
	healthBar.setPosition({ 26.f, 58.f });
	healthBar.setFillColor(sf::Color(80, 190, 95));
	std::array<sf::RectangleShape, enemySpawns.size()> enemyHealthBackgrounds;
	std::array<sf::RectangleShape, enemySpawns.size()> enemyHealthBars;
	for (std::size_t i = 0; i < enemySpawns.size(); ++i)
	{
		enemyHealthBackgrounds[i].setSize({ 64.f, 7.f });
		enemyHealthBackgrounds[i].setFillColor(sf::Color(40, 20, 20));
		enemyHealthBars[i].setSize({ 64.f, 7.f });
		enemyHealthBars[i].setFillColor(sf::Color(210, 55, 50));
	}
	sf::Text gameOverTitle(uiFont, "GAME OVER", 64);
	gameOverTitle.setFillColor(sf::Color(235, 80, 70));
	gameOverTitle.setPosition({ 350.f, 315.f });
	sf::Text victoryTitle(uiFont, "DUNGEON CLEARED!", 58);
	victoryTitle.setFillColor(sf::Color(235, 210, 120));
	victoryTitle.setPosition({ 275.f, 315.f });
	sf::Text restartPrompt(uiFont, "Press R to Restart", 28);
	restartPrompt.setFillColor(sf::Color::White);
	restartPrompt.setPosition({ 365.f, 405.f });
	bool defeatReported = false;

	sf::Clock deltaClock;
	float animationTime = 0.f;
	int facingRow = 0;
	float skeletonAnimationTime = 0.f;
	int skeletonFacingRow = 0;
	ai::SkeletonPathState skeletonPathState;
	float archerAnimationTime = 0.f;
	int archerFacingRow = 0;
	float ogreAnimationTime = 0.f;
	int ogreFacingRow = 0;
	const auto resetGameSession = [&]
	{
		evaluationLogger.startNewTrial(evaluationTimeSeconds);
		evaluationTimeSeconds = 0.0;
		gameSession.restart(playerHealth, enemyHealth);
		player.setPosition(playerStartPosition);
		player.setTexture(playerIdleTexture);
		player.setTextureRect(sf::IntRect({ 0, 0 }, { playerFrameSize, playerFrameSize }));
		playerAttackState = {};
		animationTime = 0.f;
		facingRow = 0;

		for (std::size_t i = 0; i < enemies.size(); ++i)
		{
			enemies[i].sprite.setTexture(enemies[i].texture);
			enemies[i].sprite.setTextureRect(sf::IntRect(
				{ 0, 0 }, { playerFrameSize, playerFrameSize }));
			enemies[i].sprite.setPosition(cellCenter(enemySpawns[i].cell));
			enemies[i].sprite.setScale({ enemySpawns[i].scale, enemySpawns[i].scale });
		}

		skeletonContext = {};
		skeletonContext.detectionRange = 375.f;
		skeletonContext.attackRange = 60.f;
		skeletonContext.attackRangeBuffer = 10.f;
		skeletonContext.moveSpeed = ai::skeletonMoveSpeed;
		skeletonContext.enemyPosition = enemies[0].sprite.getTransform().transformPoint(
			{ playerFrameSize / 2.f, 244.f });
		skeletonContext.playerPosition = playerStartGround;
		ai::buildSkeletonBehaviourTree(skeletonTree);
		skeletonAttack = {};
		skeletonPathState = {};
		skeletonAnimationTime = 0.f;
		skeletonFacingRow = 0;

		archerContext = {};
		archerContext.detectionRange = 600.f;
		archerContext.moveSpeed = 120.f;
		archerContext.enemyPosition = enemies[1].sprite.getTransform().transformPoint(
			{ playerFrameSize / 2.f, 244.f });
		archerContext.playerPosition = playerStartGround;
		ai::buildArcherBehaviourTree(archerTree);
		archerPathState = {};
		archerAttackState = {};
		archerProjectiles.clear();
		archerAnimationTime = 0.f;
		archerFacingRow = 0;

		ogreContext = {};
		ogreContext.detectionRange = 450.f;
		ogreContext.attackRange = 85.f;
		ogreContext.moveSpeed = 65.f;
		ogreContext.enemyPosition = enemies[2].sprite.getTransform().transformPoint(
			{ playerFrameSize / 2.f, 244.f });
		ogreContext.playerPosition = playerStartGround;
		ai::buildOgreBehaviourTree(ogreTree);
		ogrePathState = {};
		ogreCombatState = {};
		ogreAnimationTime = 0.f;
		ogreFacingRow = 0;

		defeatReported = false;
		sf::Vector2f cameraCenter = playerStartPosition;
		const sf::Vector2f viewSize = camera.getSize();
		cameraCenter.x = std::clamp(cameraCenter.x, viewSize.x / 2.f,
			dungeonWorldWidth - viewSize.x / 2.f);
		cameraCenter.y = std::clamp(cameraCenter.y, viewSize.y / 2.f,
			dungeonWorldHeight - viewSize.y / 2.f);
		camera.setCenter(cameraCenter);
		window.setView(camera);
		deltaClock.restart();
	};

	while (window.isOpen())
	{
		bool restartPressed = false;
		while (const std::optional event = window.pollEvent())
		{
			if (event->is<sf::Event::Closed>())
				window.close();
			if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>())
			{
				if (gameSession.isPaused() && keyPressed->code == sf::Keyboard::Key::R)
					restartPressed = true;
				if (!gameSession.isPaused() && keyPressed->code == sf::Keyboard::Key::Space &&
					playerHealth.current > 0)
					ai::pressPlayerAttack(playerAttackState, facingRow);
			}
			if (const auto* keyReleased = event->getIf<sf::Event::KeyReleased>())
			{
				if (keyReleased->code == sf::Keyboard::Key::Space)
					ai::releasePlayerAttack(playerAttackState);
			}
		}
		if (!window.isOpen())
			break;
		if (gameSession.isPaused())
		{
			if (gameSession.canRestart(restartPressed))
			{
				resetGameSession();
				continue;
			}

			deltaClock.restart();
			window.setView(window.getDefaultView());
			window.clear(sf::Color(12, 12, 20));
			if (uiFontLoaded)
			{
				window.draw(gameSession.gameOver ? gameOverTitle : victoryTitle);
				window.draw(restartPrompt);
			}
			window.display();
			continue;
		}

		const float deltaTime = std::min(deltaClock.restart().asSeconds(), 0.1f);
		evaluationTimeSeconds += deltaTime;
		sf::Vector2f direction{};
		if (playerHealth.current > 0 && !playerAttackState.animationActive &&
			sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W))
			direction.y -= 1.f;
		if (playerHealth.current > 0 && !playerAttackState.animationActive &&
			sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S))
			direction.y += 1.f;
		if (playerHealth.current > 0 && !playerAttackState.animationActive &&
			sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A))
			direction.x -= 1.f;
		if (playerHealth.current > 0 && !playerAttackState.animationActive &&
			sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D))
			direction.x += 1.f;

		const float directionLength = std::sqrt(direction.x * direction.x + direction.y * direction.y);
		const bool hasInput = directionLength > 0.f;
		if (hasInput)
		{
			direction /= directionLength;

			if (std::abs(direction.x) > std::abs(direction.y))
				facingRow = direction.x > 0.f ? 3 : 2;
			else
				facingRow = direction.y > 0.f ? 0 : 1;
		}

		const sf::Vector2f oldPosition = player.getPosition();
		sf::Vector2f newPosition = oldPosition;
		const sf::Vector2f movement = direction * movementSpeed * deltaTime;

		// Resolve axes separately so the Player can slide along walls.
		sf::Vector2f candidate = newPosition;
		candidate.x += movement.x;
		if (canOccupy(candidate))
			newPosition.x = candidate.x;

		candidate = newPosition;
		candidate.y += movement.y;
		if (canOccupy(candidate))
			newPosition.y = candidate.y;
		player.setPosition(newPosition);
		const ai::PlayerAttackUpdate playerAttackUpdate =
			ai::updatePlayerAttack(playerAttackState, deltaTime);

		// Use the player's collision-foot bottom and the Skeleton's visible foot line.
		const sf::Vector2f playerGroundPosition{
			player.getPosition().x,
			player.getPosition().y + playerColliderTopOffset + playerColliderHeight
		};
		constexpr float skeletonFootYInFrame = 244.f;
		if (playerAttackUpdate.damageFrame)
		{
			for (std::size_t i = 0; i < enemies.size(); ++i)
			{
				if (enemyHealth[i].isDefeated())
					continue;
				const sf::Vector2f enemyGround = enemies[i].sprite.getTransform().transformPoint(
					{ playerFrameSize / 2.f, skeletonFootYInFrame });
				const bool clearLineOfSight = ai::hasClearLineOfSight(
					playerGroundPosition, enemyGround, ogreRayCanOccupy, 8.f);
				if (ai::applyPlayerAttackDamage(playerAttackUpdate, enemyHealth[i],
					playerGroundPosition, playerColliderWidth, playerColliderHeight,
					enemyGround, enemyFootWidths[i], enemyFootHeights[i], clearLineOfSight))
				{
					std::cout << "Player hit " << enemySpawns[i].role << " for "
						<< ai::playerAttackDamage << " damage ("
						<< enemyHealth[i].current << " HP remaining).\n";
				}
			}
		}
		skeletonContext.enemyPosition = enemies[0].sprite.getTransform().transformPoint(
			{ playerFrameSize / 2.f, skeletonFootYInFrame });
		skeletonContext.playerPosition = playerGroundPosition;
		skeletonContext.attackCycleActive = skeletonAttack.animationActive;
		ai::updateSkeletonVisibility(skeletonContext);
		skeletonContext.hasLineOfSight = ai::hasClearLineOfSight(
			skeletonContext.enemyPosition, skeletonContext.playerPosition, skeletonCanOccupy, 12.f);
		const ai::EnemyContext::Action previousSkeletonAction = skeletonContext.currentAction;
		if (playerHealth.current > 0 && !enemyHealth[0].isDefeated())
		{
			const auto tickStart = std::chrono::steady_clock::now();
			skeletonTree.tick(skeletonContext, deltaTime);
			const auto tickDuration = std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - tickStart).count();
			evaluationLogger.recordBehaviourTreeTick(evaluationTimeSeconds,
				"Skeleton Guard", static_cast<std::uint64_t>(tickDuration));
		}
		else
		{
			skeletonContext.currentAction = ai::EnemyContext::Action::Idle;
			skeletonContext.moveDirection = { 0.f, 0.f };
			skeletonContext.playerVisible = false;
		}
		if (skeletonContext.currentAction != previousSkeletonAction)
		{
			const char* actionName = "Idle";
			if (skeletonContext.currentAction == ai::EnemyContext::Action::Attack)
				actionName = "Attack";
			else if (skeletonContext.currentAction == ai::EnemyContext::Action::Chase)
				actionName = "Chase";
			std::cout << "Skeleton Guard: " << actionName << '\n';
		}
		if (playerHealth.current > 0 && !enemyHealth[0].isDefeated())
			evaluationLogger.recordAction(evaluationTimeSeconds, "Skeleton Guard",
				skeletonContext.currentAction == ai::EnemyContext::Action::Attack ? "Attack" :
				skeletonContext.currentAction == ai::EnemyContext::Action::Chase ? "Chase" : "Idle",
				ai::distanceBetween(skeletonContext.enemyPosition, skeletonContext.playerPosition),
				skeletonContext.hasLineOfSight);
		evaluationLogger.recordDetection(evaluationTimeSeconds, "Skeleton Guard",
			skeletonContext.playerVisible,
			ai::distanceBetween(skeletonContext.enemyPosition, skeletonContext.playerPosition),
			skeletonContext.hasLineOfSight);

		const bool pathWasUnavailable = skeletonPathState.pathFailed;
		const sf::Vector2f skeletonMovement = ai::followSkeletonPath(
			skeletonContext, skeletonPathState, deltaTime, dungeon,
			static_cast<float>(tileSize), skeletonCellWalkable, skeletonCanOccupy,
			skeletonNavigationGroundOffset,
			[&](bool success, std::uint64_t durationMicroseconds)
			{
				evaluationLogger.recordAStar(evaluationTimeSeconds, "Skeleton Guard",
					success, durationMicroseconds);
			});
		if (!pathWasUnavailable && skeletonPathState.pathFailed &&
			skeletonContext.currentAction == ai::EnemyContext::Action::Chase)
			std::cerr << "Skeleton Guard: no A* path to Player; holding position.\n";
		const bool skeletonIsWalking = skeletonMovement.x != 0.f || skeletonMovement.y != 0.f;
		skeletonContext.hasLineOfSight = ai::hasClearLineOfSight(
			skeletonContext.enemyPosition, skeletonContext.playerPosition, skeletonCanOccupy, 12.f);
		const float distanceToPlayer = ai::distanceBetween(
			skeletonContext.enemyPosition, skeletonContext.playerPosition);
		const bool skeletonAttackWasActive = skeletonAttack.animationActive;
		const bool skeletonHitWasApplied = skeletonAttack.hitApplied;
		const ai::SkeletonAttackUpdate attackUpdate = ai::updateSkeletonAttack(
			skeletonAttack, playerHealth,
			skeletonContext.currentAction == ai::EnemyContext::Action::Attack,
			distanceToPlayer, skeletonContext.attackRange,
			skeletonContext.hasLineOfSight, deltaTime);
		skeletonContext.attackCycleActive = attackUpdate.animationActive;
		if (attackUpdate.damageApplied)
		{
			evaluationLogger.recordCombat(evaluationTimeSeconds, "Skeleton Guard", "hit",
				distanceToPlayer, skeletonContext.hasLineOfSight);
			std::cout << "Player hit by Skeleton Guard: " << playerHealth.current << " HP remaining.\n";
		}
		if (!skeletonAttackWasActive && attackUpdate.animationActive)
			evaluationLogger.recordCombat(evaluationTimeSeconds, "Skeleton Guard", "attack_attempt",
				distanceToPlayer, skeletonContext.hasLineOfSight);
		if (!skeletonHitWasApplied && skeletonAttack.hitApplied && !attackUpdate.damageApplied)
			evaluationLogger.recordCombat(evaluationTimeSeconds, "Skeleton Guard", "miss",
				distanceToPlayer, skeletonContext.hasLineOfSight);
		if (skeletonAttackWasActive && !attackUpdate.animationActive &&
			!skeletonHitWasApplied && !skeletonAttack.hitApplied)
			evaluationLogger.recordCombat(evaluationTimeSeconds, "Skeleton Guard", "miss",
				distanceToPlayer, skeletonContext.hasLineOfSight);
		if (playerHealth.current == 0 && !defeatReported)
		{
			std::cout << "Player defeated.\n";
			defeatReported = true;
		}

		// Use each sprite's ground point and the Player's ground point in world space.
		constexpr float archerFootYInFrame = 244.f;
		archerContext.enemyPosition = enemies[1].sprite.getTransform().transformPoint(
			{ playerFrameSize / 2.f, archerFootYInFrame });
		archerContext.playerPosition = playerGroundPosition;
		archerContext.hasLineOfSight = ai::hasClearLineOfSight(
			archerContext.enemyPosition, archerContext.playerPosition, archerRayCanOccupy, 12.f);
		ai::updateArcherVisibility(archerContext);
		const ai::EnemyContext::Action previousArcherAction = archerContext.currentAction;
		if (playerHealth.current > 0 && !enemyHealth[1].isDefeated())
		{
			const auto tickStart = std::chrono::steady_clock::now();
			archerTree.tick(archerContext, deltaTime);
			const auto tickDuration = std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - tickStart).count();
			evaluationLogger.recordBehaviourTreeTick(evaluationTimeSeconds,
				"Goblin Archer", static_cast<std::uint64_t>(tickDuration));
		}
		else
		{
			archerContext.currentAction = ai::EnemyContext::Action::Idle;
			archerContext.moveDirection = { 0.f, 0.f };
			archerContext.playerVisible = false;
		}
		if (archerContext.currentAction != previousArcherAction)
		{
			const char* actionName = "Idle";
			switch (archerContext.currentAction)
			{
			case ai::EnemyContext::Action::Retreat:      actionName = "Retreat"; break;
			case ai::EnemyContext::Action::RangedAttack: actionName = "Ranged Attack"; break;
			case ai::EnemyContext::Action::Approach:     actionName = "Approach"; break;
			case ai::EnemyContext::Action::Reposition:   actionName = "Reposition"; break;
			default: break;
			}
			std::cout << "Goblin Archer: " << actionName << '\n';
		}
		const char* archerActionName = "Idle";
		switch (archerContext.currentAction)
		{
		case ai::EnemyContext::Action::Retreat:      archerActionName = "Retreat"; break;
		case ai::EnemyContext::Action::RangedAttack: archerActionName = "RangedAttack"; break;
		case ai::EnemyContext::Action::Approach:     archerActionName = "Approach"; break;
		case ai::EnemyContext::Action::Reposition:   archerActionName = "Reposition"; break;
		default: break;
		}
		if (playerHealth.current > 0 && !enemyHealth[1].isDefeated())
			evaluationLogger.recordAction(evaluationTimeSeconds, "Goblin Archer",
				archerActionName,
				ai::archerDistance(archerContext), archerContext.hasLineOfSight);
		evaluationLogger.recordDetection(evaluationTimeSeconds, "Goblin Archer",
			archerContext.playerVisible, ai::archerDistance(archerContext),
			archerContext.hasLineOfSight);
		const bool archerPathWasUnavailable = archerPathState.pathFailed;
		const sf::Vector2f archerMovement = ai::moveArcher(
			archerContext, archerPathState, deltaTime, dungeon,
			static_cast<float>(tileSize), archerCellWalkable, archerCanOccupy,
			[&](const sf::Vector2f& from, const sf::Vector2f& to)
			{
				return ai::hasClearLineOfSight(from, to, archerRayCanOccupy, 12.f);
			},
			[&](bool success, std::uint64_t durationMicroseconds)
			{
				evaluationLogger.recordAStar(evaluationTimeSeconds, "Goblin Archer",
					success, durationMicroseconds);
			});
		if (!archerPathWasUnavailable && archerPathState.pathFailed &&
			(archerContext.currentAction == ai::EnemyContext::Action::Retreat ||
			 archerContext.currentAction == ai::EnemyContext::Action::Approach ||
			 archerContext.currentAction == ai::EnemyContext::Action::Reposition))
			std::cerr << "Goblin Archer: no valid route or firing position; holding position.\n";
		const bool archerIsWalking = archerMovement.x != 0.f || archerMovement.y != 0.f;
		if (archerIsWalking)
		{
			if (std::abs(archerMovement.x) > std::abs(archerMovement.y))
				archerFacingRow = archerMovement.x > 0.f ? 3 : 2;
			else
				archerFacingRow = archerMovement.y > 0.f ? 0 : 1;
		}
		constexpr float archerScale = 0.8f;
		constexpr float archerFootOffset =
			(244.f - playerFrameSize / 2.f) * archerScale;
		enemies[1].sprite.setPosition({ archerContext.enemyPosition.x,
			archerContext.enemyPosition.y - archerFootOffset });

		if (archerContext.currentAction == ai::EnemyContext::Action::RangedAttack)
		{
			const sf::Vector2f toPlayer = archerContext.playerPosition - archerContext.enemyPosition;
			if (std::abs(toPlayer.x) > std::abs(toPlayer.y))
				archerFacingRow = toPlayer.x > 0.f ? 3 : 2;
			else
				archerFacingRow = toPlayer.y > 0.f ? 0 : 1;
		}

		if (enemyHealth[1].isDefeated())
			archerProjectiles.clear();
		const int archerHits = enemyHealth[1].isDefeated() ? 0 :
			ai::updateArcherProjectiles(archerProjectiles,
				deltaTime, playerGroundPosition, playerColliderWidth, playerColliderHeight,
				playerHealth, arrowCanOccupy);
		if (archerHits > 0)
		{
			for (int hit = 0; hit < archerHits; ++hit)
				evaluationLogger.recordCombat(evaluationTimeSeconds, "Goblin Archer", "hit",
					ai::archerDistance(archerContext), archerContext.hasLineOfSight);
			std::cout << "Player hit by Goblin Archer: " << playerHealth.current
				<< " HP remaining.\n";
		}
		for (const ai::ArcherProjectile& projectile : archerProjectiles)
		{
			if (!projectile.active && !projectile.hitPlayer)
				evaluationLogger.recordCombat(evaluationTimeSeconds, "Goblin Archer", "miss",
					ai::archerDistance(archerContext), archerContext.hasLineOfSight);
		}
		archerProjectiles.erase(std::remove_if(archerProjectiles.begin(), archerProjectiles.end(),
			[](const ai::ArcherProjectile& projectile) { return !projectile.active; }),
			archerProjectiles.end());

		const ai::ArcherAttackUpdate archerAttackUpdate = ai::updateArcherAttack(
			archerAttackState,
			!enemyHealth[1].isDefeated() &&
			archerContext.currentAction == ai::EnemyContext::Action::RangedAttack,
			deltaTime);
		if (archerAttackUpdate.releaseProjectile)
		{
			const sf::Vector2f toPlayer = archerContext.playerPosition - archerContext.enemyPosition;
			const float distance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y);
			if (distance > 0.001f)
			{
				const sf::Vector2f aim = toPlayer / distance;
				const sf::Vector2f bowOrigin = archerContext.enemyPosition +
					sf::Vector2f{ 0.f, -48.f } + aim * 28.f;
				const ai::ArcherProjectile projectile = ai::makeArcherProjectile(
					bowOrigin, archerContext.playerPosition);
				if (projectile.active)
				{
					archerProjectiles.push_back(projectile);
					evaluationLogger.recordCombat(evaluationTimeSeconds, "Goblin Archer",
						"attack_attempt", distance, archerContext.hasLineOfSight);
				}
			}
		}
		if (playerHealth.current == 0 && !defeatReported)
		{
			std::cout << "Player defeated.\n";
			defeatReported = true;
		}

		// The stationary Ogre makes decisions using ground positions in world space.
		ogreContext.enemyPosition = enemies[2].sprite.getTransform().transformPoint(
			{ playerFrameSize / 2.f, 244.f });
		ogreContext.playerPosition = playerGroundPosition;
		ogreContext.hasLineOfSight = ai::hasClearLineOfSight(
			ogreContext.enemyPosition, ogreContext.playerPosition, ogreRayCanOccupy, 12.f);
		ai::updateOgreVisibility(ogreContext);
		const ai::EnemyContext::Action previousOgreAction = ogreContext.currentAction;
		ai::OgreCombatUpdate ogreAttackUpdate{};
		if (playerHealth.current > 0 && !enemyHealth[2].isDefeated())
		{
			const auto tickStart = std::chrono::steady_clock::now();
			ogreTree.tick(ogreContext, deltaTime);
			const auto tickDuration = std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - tickStart).count();
			evaluationLogger.recordBehaviourTreeTick(evaluationTimeSeconds,
				"Dungeon Ogre", static_cast<std::uint64_t>(tickDuration));
			const bool ogreHitWasApplied = ogreCombatState.hitApplied;
			ogreAttackUpdate = ai::updateOgreCombat(
				ogreCombatState, ogreContext, playerHealth, deltaTime);
			if (ogreAttackUpdate.attackStarted)
				evaluationLogger.recordCombat(evaluationTimeSeconds, "Dungeon Ogre",
					"attack_attempt", ai::ogreDistance(ogreContext), ogreContext.hasLineOfSight);
			if (!ogreHitWasApplied && ogreCombatState.hitApplied)
				evaluationLogger.recordCombat(evaluationTimeSeconds, "Dungeon Ogre",
					ogreAttackUpdate.damageApplied ? "hit" : "miss",
					ai::ogreDistance(ogreContext), ogreContext.hasLineOfSight);
		}
		else
		{
			ogreContext.currentAction = ai::EnemyContext::Action::Idle;
			ogreContext.moveDirection = { 0.f, 0.f };
			ogreContext.playerVisible = false;
			ogreContext.attackCycleActive = false;
			ogreCombatState = {};
		}
		if (ogreAttackUpdate.attackStarted)
			ogreFacingRow = ai::ogreFacingRow(ogreContext.enemyPosition,
				ogreContext.playerPosition);
		if (ogreAttackUpdate.damageApplied)
			std::cout << "Dungeon Ogre hit the Player for " << ai::ogreHeavyAttackDamage << " damage.\n";
		if (ogreContext.currentAction != previousOgreAction)
		{
			const char* actionName = "Idle";
			switch (ogreContext.currentAction)
			{
			case ai::EnemyContext::Action::Chase:       actionName = "Chase"; break;
			case ai::EnemyContext::Action::HeavyAttack: actionName = "HeavyAttack"; break;
			case ai::EnemyContext::Action::Recovery:    actionName = "Recovery"; break;
			default: break;
			}
			std::cout << "Dungeon Ogre: " << actionName << '\n';
		}
		const char* ogreActionName = "Idle";
		switch (ogreContext.currentAction)
		{
		case ai::EnemyContext::Action::Chase:       ogreActionName = "Chase"; break;
		case ai::EnemyContext::Action::HeavyAttack: ogreActionName = "HeavyAttack"; break;
		case ai::EnemyContext::Action::Recovery:    ogreActionName = "Recovery"; break;
		default: break;
		}
		if (playerHealth.current > 0 && !enemyHealth[2].isDefeated())
			evaluationLogger.recordAction(evaluationTimeSeconds, "Dungeon Ogre",
				ogreActionName, ai::ogreDistance(ogreContext), ogreContext.hasLineOfSight);
		evaluationLogger.recordDetection(evaluationTimeSeconds, "Dungeon Ogre",
			ogreContext.playerVisible, ai::ogreDistance(ogreContext),
			ogreContext.hasLineOfSight);
		if (ogreContext.currentAction == ai::EnemyContext::Action::Recovery &&
			previousOgreAction != ai::EnemyContext::Action::Recovery)
			evaluationLogger.recordCombat(evaluationTimeSeconds, "Dungeon Ogre",
				"recovery_started", ai::ogreDistance(ogreContext), ogreContext.hasLineOfSight);
		const bool ogrePathWasUnavailable = ogrePathState.pathFailed;
		const sf::Vector2f ogreMovement = ai::followOgrePath(
			ogreContext, ogrePathState, deltaTime, dungeon,
			static_cast<float>(tileSize), ogreCellWalkable, ogreCanOccupy,
			ai::ogreNavigationGroundOffset,
			[&](bool success, std::uint64_t durationMicroseconds)
			{
				evaluationLogger.recordAStar(evaluationTimeSeconds, "Dungeon Ogre",
					success, durationMicroseconds);
			});
		if (!ogrePathWasUnavailable && ogrePathState.pathFailed &&
			ogreContext.currentAction == ai::EnemyContext::Action::Chase)
			std::cerr << "Dungeon Ogre: no A* path to Player; holding position.\n";
		const bool ogreIsWalking = ogreMovement.x != 0.f || ogreMovement.y != 0.f;
		if (ogreIsWalking)
		{
			if (std::abs(ogreMovement.x) > std::abs(ogreMovement.y))
				ogreFacingRow = ogreMovement.x > 0.f ? 3 : 2;
			else
				ogreFacingRow = ogreMovement.y > 0.f ? 0 : 1;
		}
		constexpr float ogreScale = 0.9f;
		constexpr float ogreFootOffset = (244.f - playerFrameSize / 2.f) * ogreScale;
		enemies[2].sprite.setPosition({ ogreContext.enemyPosition.x,
			ogreContext.enemyPosition.y - ogreFootOffset });
		if (ogreAttackUpdate.animationActive)
		{
			enemies[2].sprite.setTexture(ogreAttackTexture);
			enemies[2].sprite.setTextureRect(sf::IntRect(
				{ ogreAttackUpdate.frame * playerFrameSize, ogreFacingRow * playerFrameSize },
				{ playerFrameSize, playerFrameSize }));
		}
		else
		{
			ogreAnimationTime += deltaTime;
			const float ogreFps = ogreIsWalking ? walkFramesPerSecond : idleFramesPerSecond;
			const int ogreFrame = static_cast<int>(ogreAnimationTime * ogreFps) % animationFrameCount;
			enemies[2].sprite.setTexture(ogreIsWalking ? ogreWalkTexture : enemies[2].texture);
			enemies[2].sprite.setTextureRect(sf::IntRect(
				{ ogreFrame * playerFrameSize, ogreFacingRow * playerFrameSize },
				{ playerFrameSize, playerFrameSize }));
		}

		archerAnimationTime += deltaTime;
		const float archerFps = archerIsWalking && !archerAttackUpdate.animationActive
			? walkFramesPerSecond : idleFramesPerSecond;
		const int archerFrame = static_cast<int>(archerAnimationTime * archerFps) %
			animationFrameCount;
		if (archerAttackUpdate.animationActive)
		{
			enemies[1].sprite.setTexture(archerAttackTexture);
			enemies[1].sprite.setTextureRect(sf::IntRect(
				{ archerAttackUpdate.frame * playerFrameSize, archerFacingRow * playerFrameSize },
				{ playerFrameSize, playerFrameSize }));
		}
		else
		{
			enemies[1].sprite.setTexture(archerIsWalking ? archerWalkTexture : enemies[1].texture);
			enemies[1].sprite.setTextureRect(sf::IntRect(
				{ archerFrame * playerFrameSize, archerFacingRow * playerFrameSize },
				{ playerFrameSize, playerFrameSize }));
		}

		// Keep the sprite's feet aligned with its ground position.
		constexpr float skeletonScale = 0.8f;
		constexpr float skeletonFootOffset = (skeletonFootYInFrame - playerFrameSize / 2.f) * skeletonScale;
		enemies[0].sprite.setPosition({
			skeletonContext.enemyPosition.x,
			skeletonContext.enemyPosition.y - skeletonFootOffset
		});

		if (skeletonIsWalking)
		{
			if (std::abs(skeletonMovement.x) > std::abs(skeletonMovement.y))
				skeletonFacingRow = skeletonMovement.x > 0.f ? 3 : 2;
			else
				skeletonFacingRow = skeletonMovement.y > 0.f ? 0 : 1;
		}
		if (attackUpdate.animationActive)
		{
			enemies[0].sprite.setTexture(skeletonAttackTexture);
			enemies[0].sprite.setTextureRect(sf::IntRect(
				{ attackUpdate.frame * playerFrameSize, skeletonFacingRow * playerFrameSize },
				{ playerFrameSize, playerFrameSize }));
		}
		else
		{
			skeletonAnimationTime += deltaTime;
			const float skeletonFps = skeletonIsWalking ? walkFramesPerSecond : idleFramesPerSecond;
			const int skeletonFrame = static_cast<int>(skeletonAnimationTime * skeletonFps) % animationFrameCount;
			enemies[0].sprite.setTexture(skeletonIsWalking ? skeletonWalkTexture : enemies[0].texture);
			enemies[0].sprite.setTextureRect(sf::IntRect(
				{ skeletonFrame * playerFrameSize, skeletonFacingRow * playerFrameSize },
				{ playerFrameSize, playerFrameSize }));
		}

		if (playerAttackUpdate.animationActive)
		{
			player.setTexture(playerAttackTexture);
			player.setTextureRect(sf::IntRect(
				{ playerAttackUpdate.frame * playerFrameSize,
					playerAttackUpdate.facingRow * playerFrameSize },
				{ playerFrameSize, playerFrameSize }));
		}
		else
		{
			const bool isWalking = newPosition.x != oldPosition.x || newPosition.y != oldPosition.y;
			animationTime += deltaTime;
			const float animationFramesPerSecond = isWalking ? walkFramesPerSecond : idleFramesPerSecond;
			const int frame = static_cast<int>(animationTime * animationFramesPerSecond) % animationFrameCount;
			player.setTexture(isWalking ? playerWalkTexture : playerIdleTexture);
			player.setTextureRect(sf::IntRect(
				{ frame * playerFrameSize, facingRow * playerFrameSize },
				{ playerFrameSize, playerFrameSize }));
		}
		gameSession.update(playerHealth, enemyHealth);

		const sf::Vector2f viewSize = camera.getSize();
		sf::Vector2f cameraCenter = player.getPosition();
		cameraCenter.x = std::clamp(cameraCenter.x, viewSize.x / 2.f,
			dungeonWorldWidth - viewSize.x / 2.f);
		cameraCenter.y = std::clamp(cameraCenter.y, viewSize.y / 2.f,
			dungeonWorldHeight - viewSize.y / 2.f);
		camera.setCenter(cameraCenter);
		window.setView(camera);

		window.clear();
		for (int row = 0; row < tilesDown; ++row)
		{
			for (int column = 0; column < tilesAcross; ++column)
			{
				const sf::Vector2f tilePosition{
					static_cast<float>(column * tileSize),
					static_cast<float>(row * tileSize)
				};
				floorTile.setPosition(tilePosition);
				window.draw(floorTile);
				if (dungeon[row][column] == TileType::Wall)
				{
					drawConnectedWallTile(window, wallTexture, dungeon,
						row, column, tilePosition, static_cast<float>(tileSize));
					continue;
				}

			}
		}

		struct DepthItem
		{
			sf::Sprite* sprite;
			float groundY;
		};
		std::vector<DepthItem> depthItems;
		depthItems.reserve(props.size() + enemies.size() + 1);
		for (WorldProp& prop : props)
		{
			if (prop.blocksMovement)
				depthItems.push_back({ &prop.sprite, prop.groundPosition.y });
			else
				window.draw(prop.sprite);
		}
		depthItems.push_back({ &player, playerGroundPosition.y });
		for (std::size_t i = 0; i < enemies.size(); ++i)
		{
			if (!ai::shouldRenderEnemy(enemyHealth[i]))
				continue;
			const float enemyGroundY = enemies[i].sprite.getTransform().transformPoint(
				{ playerFrameSize / 2.f, skeletonFootYInFrame }).y;
			depthItems.push_back({ &enemies[i].sprite, enemyGroundY });
		}
		std::stable_sort(depthItems.begin(), depthItems.end(),
			[](const DepthItem& left, const DepthItem& right)
			{
				return left.groundY < right.groundY;
			});
		for (const DepthItem& item : depthItems)
			window.draw(*item.sprite);
		for (std::size_t i = 0; i < enemies.size(); ++i)
		{
			if (!ai::shouldRenderEnemy(enemyHealth[i]))
				continue;
			const float barWidth = 72.f * enemySpawns[i].scale;
			const float barHeight = 7.f;
			const sf::Vector2f barAnchor = enemies[i].sprite.getTransform().transformPoint(
				{ playerFrameSize / 2.f, 52.f });
			const sf::Vector2f barPosition{ barAnchor.x - barWidth / 2.f,
				barAnchor.y - 14.f };
			enemyHealthBackgrounds[i].setSize({ barWidth, barHeight });
			enemyHealthBackgrounds[i].setPosition(barPosition);
			enemyHealthBars[i].setSize({ barWidth * enemyHealth[i].current /
				static_cast<float>(enemyHealth[i].maximum), barHeight });
			enemyHealthBars[i].setPosition(barPosition);
			window.draw(enemyHealthBackgrounds[i]);
			window.draw(enemyHealthBars[i]);
		}
		for (const ai::ArcherProjectile& projectile : archerProjectiles)
		{
			const float angle = std::atan2(projectile.velocity.y, projectile.velocity.x) *
				180.f / 3.1415926535f;
			arrowVisual.setPosition(projectile.position);
			arrowVisual.setRotation(sf::degrees(angle));
			window.draw(arrowVisual);
		}

		window.setView(window.getDefaultView());
		window.draw(hudPanel);
		window.draw(healthBarBackground);
		healthBar.setSize({ 280.f * playerHealth.current / ai::PlayerHealth::maximum, 24.f });
		window.draw(healthBar);
		if (uiFontLoaded)
		{
			healthLabel.setString("Player HP: " + std::to_string(playerHealth.current) + " / 100");
			window.draw(healthLabel);
		}
		if (gameSession.isPaused())
		{
			sf::RectangleShape defeatOverlay({ static_cast<float>(windowWidth),
				static_cast<float>(windowHeight) });
			defeatOverlay.setFillColor(sf::Color(0, 0, 0, 185));
			window.draw(defeatOverlay);
			if (uiFontLoaded)
			{
				window.draw(gameSession.gameOver ? gameOverTitle : victoryTitle);
				window.draw(restartPrompt);
			}
		}
		window.display();
	}
}
