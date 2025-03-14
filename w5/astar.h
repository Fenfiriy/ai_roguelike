#include <algorithm>
#include <map>
#include "ecsTypes.h"
#include "dungeonUtils.h"

struct scores
{
	float g = 0;
	float h = 0;
	int prev_idx = -1;
};

class Astar
{
public:
	DungeonData dd;
	std::vector<int> open_list;
	std::vector<int> closed_list;
	std::map<int, scores> scores_map;


	Astar(const DungeonData& dd) : dd(dd)
	{
		open_list.reserve(dd.width * dd.height);
		closed_list.reserve(dd.width * dd.height);
	}

	int coords2idx(const Position& pos)
	{
		return pos.y * dd.width + pos.x;
	}
	Position idx2coords(int idx)
	{
		return Position{ (int)(idx % dd.width), (int)(idx / dd.width) };
	}
	std::vector<Position> init_search(Position start, Position goal)
	{
		open_list.clear();
		closed_list.clear();
		scores_map.clear();
		open_list.push_back(coords2idx(start));
		scores_map[coords2idx(start)] = (scores{ 0, dist_sq(start, goal), -1 });

		int cur;

		while (open_list.size() > 0)
		{
			cur = open_list[0];
			open_list.erase(open_list.begin());
			closed_list.push_back(cur);

			if (cur == coords2idx(goal))
			{
				break;
			}

			Position cur_pos = idx2coords(cur);
			for (int d = 0; d < 4; d++)
			{
				Position next_pos = Position{ cur_pos.x + dirs[d][0], cur_pos.y + dirs[d][1] };
				int next_idx = coords2idx(next_pos);
				if (next_pos.x < 0 || next_pos.x >= dd.width || next_pos.y < 0 || next_pos.y >= dd.height)
				{
					continue;
				}
				if (!dungeon::is_walkable(dd.tiles[next_idx]))
				{
					continue;
				}
				if (std::find(closed_list.begin(), closed_list.end(), next_idx) != closed_list.end())
				{
					continue;
				}

				float g = scores_map[cur].g + 1;
				float h = dist_sq(next_pos, goal);
				if (std::find(open_list.begin(), open_list.end(), next_idx) == open_list.end())
				{
					open_list.push_back(next_idx);
					scores_map[next_idx] = (scores{ g, h, cur });
				}
				else
				{
					if (g + h < scores_map[next_idx].g + scores_map[next_idx].h)
					{
						scores_map[next_idx] = scores{ g, h, cur };
					}
				}
			}

			std::sort(open_list.begin(), open_list.end(), [this](int a, int b) { return scores_map[a].g + scores_map[a].h < scores_map[b].g + scores_map[b].h; });
			printf("%i", cur);
		}

		std::vector<Position> path;
		while (cur != coords2idx(start))
		{
			path.push_back(idx2coords(cur));
			cur = scores_map[cur].prev_idx;
			printf("%i", cur);
		}

		return path;
	}
};