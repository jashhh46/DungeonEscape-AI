#include <SFML/Graphics.hpp>

#include "ai/BehaviourTree.hpp"
#include "ai/EnemyContext.hpp"
#include "ai/GridCollision.hpp"
#include "ai/SkeletonDecision.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

	// The Player uses the supplied character sheets for appearance only.
	// Their rows face down, up, left, and right; frames are 256x256.
	sf::Sprite player(playerIdleTexture, sf::IntRect({ 0, 0 }, { playerFrameSize, playerFrameSize }));
	player.setOrigin({ playerFrameSize / 2.f, playerFrameSize / 2.f });
	player.setPosition(playerStartPosition);

	// The first enemy is the Skeleton Guard; its context and tree belong to this instance.
	ai::EnemyContext skeletonContext;
	skeletonContext.detectionRange = 250.f;
	skeletonContext.attackRange = 60.f;
	skeletonContext.moveSpeed = 100.f;
	ai::BehaviourTree skeletonTree;
	ai::buildSkeletonBehaviourTree(skeletonTree);

	sf::Sprite floorTile(floorTexture);

	sf::RenderWindow window(sf::VideoMode({ windowWidth, windowHeight }), "Dungeon Escape");
	window.setFramerateLimit(60);
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

	sf::Clock deltaClock;
	float animationTime = 0.f;
	int facingRow = 0;
	float skeletonAnimationTime = 0.f;
	int skeletonFacingRow = 0;

	while (window.isOpen())
	{
		while (const std::optional event = window.pollEvent())
		{
			if (event->is<sf::Event::Closed>())
				window.close();
		}
		if (!window.isOpen())
			break;

		const float deltaTime = std::min(deltaClock.restart().asSeconds(), 0.1f);
		sf::Vector2f direction{};
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W))
			direction.y -= 1.f;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S))
			direction.y += 1.f;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A))
			direction.x -= 1.f;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D))
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

		// Use the player's collision-foot bottom and the Skeleton's visible foot line.
		const sf::Vector2f playerGroundPosition{
			player.getPosition().x,
			player.getPosition().y + playerColliderTopOffset + playerColliderHeight
		};
		constexpr float skeletonFootYInFrame = 244.f;
		skeletonContext.enemyPosition = enemies[0].sprite.getTransform().transformPoint(
			{ playerFrameSize / 2.f, skeletonFootYInFrame });
		skeletonContext.playerPosition = playerGroundPosition;
		ai::updateSkeletonVisibility(skeletonContext);
		const ai::EnemyContext::Action previousSkeletonAction = skeletonContext.currentAction;
		skeletonTree.tick(skeletonContext, deltaTime);
		if (skeletonContext.currentAction != previousSkeletonAction)
		{
			const char* actionName = "Idle";
			if (skeletonContext.currentAction == ai::EnemyContext::Action::Attack)
				actionName = "Attack";
			else if (skeletonContext.currentAction == ai::EnemyContext::Action::Chase)
				actionName = "Chase";
			std::cout << "Skeleton Guard: " << actionName << '\n';
		}

		const auto skeletonCanOccupy = [&](const sf::Vector2f& groundPosition)
		{
			return ai::isFootprintClear(groundPosition,
				skeletonColliderWidth, skeletonColliderHeight, dungeon,
				static_cast<float>(tileSize), [](TileType tile) { return isWalkable(tile); }) &&
				propCollisionFree(groundPosition, skeletonColliderWidth, skeletonColliderHeight);
		};
		const sf::Vector2f skeletonMovement = ai::moveSkeleton(
			skeletonContext, deltaTime, skeletonCanOccupy);
		const bool skeletonIsWalking = skeletonMovement.x != 0.f || skeletonMovement.y != 0.f;

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
		skeletonAnimationTime += deltaTime;
		const float skeletonFps = skeletonIsWalking ? walkFramesPerSecond : idleFramesPerSecond;
		const int skeletonFrame = static_cast<int>(skeletonAnimationTime * skeletonFps) % animationFrameCount;
		enemies[0].sprite.setTexture(skeletonIsWalking ? skeletonWalkTexture : enemies[0].texture);
		enemies[0].sprite.setTextureRect(sf::IntRect(
			{ skeletonFrame * playerFrameSize, skeletonFacingRow * playerFrameSize },
			{ playerFrameSize, playerFrameSize }));

		const bool isWalking = newPosition.x != oldPosition.x || newPosition.y != oldPosition.y;
		animationTime += deltaTime;
		const float animationFramesPerSecond = isWalking ? walkFramesPerSecond : idleFramesPerSecond;
		const int frame = static_cast<int>(animationTime * animationFramesPerSecond) % animationFrameCount;
		player.setTexture(isWalking ? playerWalkTexture : playerIdleTexture);
		player.setTextureRect(sf::IntRect(
			{ frame * playerFrameSize, facingRow * playerFrameSize },
			{ playerFrameSize, playerFrameSize }));

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
		window.display();
	}
}
