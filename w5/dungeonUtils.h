#pragma once
#include "ecsTypes.h"
#include <flecs.h>

namespace dungeon
{
  constexpr char wall = '#';
  constexpr char floor = ' ';
  constexpr char city = 'c';
  constexpr char lair = 'l';

  Position find_walkable_tile(flecs::world &ecs);
  bool is_tile_walkable(flecs::world &ecs, Position pos);
  bool is_walkable(char tile);
};

const int dirs[4][2] = { {1, 0}, {0, 1}, {-1, 0}, {0, -1} };