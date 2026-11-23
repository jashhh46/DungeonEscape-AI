#include <SFML/Graphics.hpp>

#include "ai/BehaviourTree.hpp"
#include "ai/EnemyContext.hpp"
#include "ai/SkeletonDecision.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <string_view>

enum class TileType
{
	Floor,
	Wall,
	Pillar,
	Brazier
};

bool isWalkable(TileType tile)
{
	return tile == TileType::Floor;
}

struct EnemySpawn
{
	std::string_view role;
	std::string_view idleSheet;
	sf::Vector2f position;
	float scale;
};

struct EnemyVisual
{
	sf::Texture texture;
	sf::Sprite sprite;

	EnemyVisual() : sprite(texture) {}
};

int main()
{
	constexpr unsigned int windowWidth = 1024;
	constexpr unsigned int windowHeight = 768;
	constexpr int tileSize = 128;
	constexpr int tilesAcross = static_cast<int>(windowWidth) / tileSize;
	constexpr int tilesDown = static_cast<int>(windowHeight) / tileSize;
	constexpr int bossFrameSize = 256;
	constexpr int animationFrameCount = 4;
	constexpr float movementSpeed = 240.f;
	constexpr float idleFramesPerSecond = 3.f;
	constexpr float walkFramesPerSecond = 8.f;

	constexpr float playerColliderWidth = 48.f;
	constexpr float playerColliderHeight = 28.f;
	constexpr float playerColliderTopOffset = 90.f;
	// Visible art bounds keep the Boss on screen without using the padded frame as its collider.
	constexpr float visibleLeftExtent = 88.f;
	constexpr float visibleRightExtent = 86.f;
	constexpr float visibleTopExtent = 71.f;
	constexpr float visibleBottomExtent = 115.f;

	constexpr std::array<std::string_view, tilesDown> mapLayout = {
		"########",
		"#......#",
		"#.P..B.#",
		"#......#",
		"#..P...#",
		"########"
	};
	std::array<std::array<TileType, tilesAcross>, tilesDown> dungeon{};

	// Convert the easy-to-read map above into tile data used for drawing and collision.
	for (int row = 0; row < tilesDown; ++row)
	{
		for (int column = 0; column < tilesAcross; ++column)
		{
			switch (mapLayout[row][column])
			{
			case '#': dungeon[row][column] = TileType::Wall; break;
			case 'P': dungeon[row][column] = TileType::Pillar; break;
			case 'B': dungeon[row][column] = TileType::Brazier; break;
			default:  dungeon[row][column] = TileType::Floor; break;
			}
		}
	}

	// Positions are chosen on clear floor tiles, away from the internal pillar and brazier.
	constexpr std::array<EnemySpawn, 3> enemySpawns = {{
		{ "Skeleton Guard", "Assets/characters/skeleton/skeleton_idle_sheet.png", { 192.f, 192.f }, 0.8f },
		{ "Goblin Archer", "Assets/characters/archer/archer_idle_sheet.png", { 836.f, 192.f }, 0.8f },
		{ "Dungeon Ogre", "Assets/characters/orc/orc_idle_sheet.png", { 780.f, 520.f }, 0.9f }
	}};
	std::array<EnemyVisual, enemySpawns.size()> enemies;
	for (std::size_t i = 0; i < enemySpawns.size(); ++i)
	{
		const int column = static_cast<int>(enemySpawns[i].position.x / tileSize);
		const int row = static_cast<int>(enemySpawns[i].position.y / tileSize);
		if (column < 0 || column >= tilesAcross || row < 0 || row >= tilesDown ||
			!isWalkable(dungeon[row][column]))
		{
			std::cerr << "Enemy spawn is not on a walkable tile: " << enemySpawns[i].role << '\n';
			return 1;
		}

		if (!enemies[i].texture.loadFromFile(std::string(enemySpawns[i].idleSheet)))
		{
			std::cerr << "Could not load idle sheet for " << enemySpawns[i].role << ".\n";
			return 1;
		}
		enemies[i].texture.setSmooth(false);
		enemies[i].sprite.setTextureRect(sf::IntRect({ 0, 0 }, { bossFrameSize, bossFrameSize }));
		enemies[i].sprite.setOrigin({ bossFrameSize / 2.f, bossFrameSize / 2.f });
		enemies[i].sprite.setPosition(enemySpawns[i].position);
		enemies[i].sprite.setScale({ enemySpawns[i].scale, enemySpawns[i].scale });
	}

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

	sf::Texture pillarTexture;
	if (!pillarTexture.loadFromFile("Assets/tileset/wall_pillar.png"))
	{
		std::cerr << "Could not load the pillar obstacle.\n";
		return 1;
	}
	pillarTexture.setSmooth(false);

	sf::Texture brazierTexture;
	if (!brazierTexture.loadFromFile("Assets/tileset/brazier_big.png"))
	{
		std::cerr << "Could not load the brazier obstacle.\n";
		return 1;
	}
	brazierTexture.setSmooth(false);

	sf::Texture bossIdleTexture;
	if (!bossIdleTexture.loadFromFile("Assets/characters/boss/boss_idle_sheet.png"))
	{
		std::cerr << "Could not load the Boss idle spritesheet.\n";
		return 1;
	}
	bossIdleTexture.setSmooth(false);

	sf::Texture bossWalkTexture;
	if (!bossWalkTexture.loadFromFile("Assets/characters/boss/boss_walk_sheet.png"))
	{
		std::cerr << "Could not load the Boss walk spritesheet.\n";
		return 1;
	}
	bossWalkTexture.setSmooth(false);

	// Sheet rows face down, up, left, and right; each frame is 256x256.
	sf::Sprite boss(bossIdleTexture, sf::IntRect({ 0, 0 }, { bossFrameSize, bossFrameSize }));
	boss.setOrigin({ bossFrameSize / 2.f, bossFrameSize / 2.f });
	boss.setPosition({ windowWidth / 2.f, windowHeight / 2.f });

	// The first enemy is the Skeleton Guard; its context and tree belong to this instance.
	ai::EnemyContext skeletonContext;
	skeletonContext.detectionRange = 250.f;
	skeletonContext.attackRange = 60.f;
	skeletonContext.moveSpeed = 100.f;
	ai::BehaviourTree skeletonTree;
	ai::buildSkeletonBehaviourTree(skeletonTree);

	sf::Sprite floorTile(floorTexture);
	sf::Sprite wallTile(wallTexture);
	sf::Sprite pillarTile(pillarTexture);
	sf::Sprite brazierTile(brazierTexture);

	sf::RenderWindow window(sf::VideoMode({ windowWidth, windowHeight }), "Dungeon Escape");
	window.setFramerateLimit(60);

	auto canOccupy = [&](sf::Vector2f playerPosition)
	{
		const float left = playerPosition.x - playerColliderWidth / 2.f;
		const float top = playerPosition.y + playerColliderTopOffset;
		const float right = left + playerColliderWidth;
		const float bottom = top + playerColliderHeight;

		// Keep the visible Boss art on screen; this is separate from the foot-sized collider.
		if (playerPosition.x < visibleLeftExtent ||
			playerPosition.x > windowWidth - visibleRightExtent ||
			playerPosition.y < visibleTopExtent ||
			playerPosition.y > windowHeight - visibleBottomExtent)
			return false;

		for (int row = 0; row < tilesDown; ++row)
		{
			for (int column = 0; column < tilesAcross; ++column)
			{
				if (isWalkable(dungeon[row][column]))
					continue;

				const float tileLeft = static_cast<float>(column * tileSize);
				const float tileTop = static_cast<float>(row * tileSize);
				const float tileRight = tileLeft + tileSize;
				const float tileBottom = tileTop + tileSize;

				const bool overlaps = left < tileRight && right > tileLeft &&
					top < tileBottom && bottom > tileTop;
				if (overlaps)
					return false;
			}
		}

		return true;
	};

	sf::Clock deltaClock;
	float animationTime = 0.f;
	int facingRow = 0;

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

		const sf::Vector2f oldPosition = boss.getPosition();
		sf::Vector2f newPosition = oldPosition;
		const sf::Vector2f movement = direction * movementSpeed * deltaTime;

		// Resolve horizontal and vertical movement separately so the Boss can slide along walls.
		sf::Vector2f candidate = newPosition;
		candidate.x += movement.x;
		if (canOccupy(candidate))
			newPosition.x = candidate.x;

		candidate = newPosition;
		candidate.y += movement.y;
		if (canOccupy(candidate))
			newPosition.y = candidate.y;
		boss.setPosition(newPosition);

		// Use the player's collision-foot bottom and the Skeleton's visible foot line.
		const sf::Vector2f playerGroundPosition{
			boss.getPosition().x,
			boss.getPosition().y + playerColliderTopOffset + playerColliderHeight
		};
		constexpr float skeletonFootYInFrame = 244.f;
		skeletonContext.enemyPosition = enemies[0].sprite.getTransform().transformPoint(
			{ bossFrameSize / 2.f, skeletonFootYInFrame });
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

		const bool isWalking = newPosition.x != oldPosition.x || newPosition.y != oldPosition.y;
		animationTime += deltaTime;
		const float animationFramesPerSecond = isWalking ? walkFramesPerSecond : idleFramesPerSecond;
		const int frame = static_cast<int>(animationTime * animationFramesPerSecond) % animationFrameCount;
		boss.setTexture(isWalking ? bossWalkTexture : bossIdleTexture);
		boss.setTextureRect(sf::IntRect(
			{ frame * bossFrameSize, facingRow * bossFrameSize },
			{ bossFrameSize, bossFrameSize }));

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

				sf::Sprite* overlay = nullptr;
				switch (dungeon[row][column])
				{
				case TileType::Wall:   overlay = &wallTile; break;
				case TileType::Pillar: overlay = &pillarTile; break;
				case TileType::Brazier: overlay = &brazierTile; break;
				case TileType::Floor:  break;
				}
				if (overlay)
				{
					overlay->setPosition(tilePosition);
					window.draw(*overlay);
				}
			}
		}

		window.draw(boss);
		for (const EnemyVisual& enemy : enemies)
			window.draw(enemy.sprite);
		window.display();
	}
}
