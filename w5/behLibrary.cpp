#include "aiLibrary.h"
#include "ecsTypes.h"
#include "aiUtils.h"
#include "math.h"
#include "raylib.h"
#include "blackboard.h"
#include "dungeonUtils.h"
#include <algorithm>
#include "astar.h"

struct CompoundNode : public BehNode
{
  std::vector<BehNode*> nodes;

  virtual ~CompoundNode()
  {
    for (BehNode *node : nodes)
      delete node;
    nodes.clear();
  }

  CompoundNode &pushNode(BehNode *node)
  {
    nodes.push_back(node);
    return *this;
  }
};

struct Sequence : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (BehNode *node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res != BEH_SUCCESS)
        return res;
    }
    return BEH_SUCCESS;
  }
};

struct Selector : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (BehNode *node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res != BEH_FAIL)
        return res;
    }
    return BEH_FAIL;
  }
};

struct UtilitySelector : public BehNode
{
  std::vector<std::pair<BehNode*, utility_function>> utilityNodes;

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    std::vector<std::pair<float, size_t>> utilityScores;
    for (size_t i = 0; i < utilityNodes.size(); ++i)
    {
      const float utilityScore = utilityNodes[i].second(bb);
      utilityScores.push_back(std::make_pair(utilityScore, i));
    }
    std::sort(utilityScores.begin(), utilityScores.end(), [](auto &lhs, auto &rhs)
    {
      return lhs.first > rhs.first;
    });
    for (const std::pair<float, size_t> &node : utilityScores)
    {
      size_t nodeIdx = node.second;
      BehResult res = utilityNodes[nodeIdx].first->update(ecs, entity, bb);
      if (res != BEH_FAIL)
        return res;
    }
    return BEH_FAIL;
  }
};

struct MoveToEntity : public BehNode
{
  size_t entityBb = size_t(-1); // wraps to 0xff...
  MoveToEntity(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.insert([&](Action &a, const Position &pos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
      if (!targetEntity.is_alive())
      {
        res = BEH_FAIL;
        return;
      }
      targetEntity.get([&](const Position &target_pos)
      {
        if (pos != target_pos)
        {
            Position p;
            auto dungeonDataQuery = ecs.query<const DungeonData>();
            dungeonDataQuery.each([&](const DungeonData& dd){
                auto pnav = Astar(dd);
                p = pnav.init_search(pos, target_pos).back();
                });
            a.action = move_towards(pos, p);
            res = BEH_RUNNING;
        }
        else
          res = BEH_SUCCESS;
      });
    });
    return res;
  }
};

struct MoveToPos : public BehNode
{
    size_t posBb = size_t(-1);
	MoveToPos(flecs::entity entity, const char* bb_name)
	{
		posBb = reg_entity_blackboard_var<Position>(entity, bb_name);
	}
	BehResult update(flecs::world& ecs, flecs::entity entity, Blackboard& bb) override
	{
		BehResult res = BEH_RUNNING;

		entity.insert([&](Action& a, const Position& pos)
		{
            Position targetPos = bb.get<Position>(posBb);
            if (pos != targetPos)
            {
                Position p;
                auto dungeonDataQuery = ecs.query<const DungeonData>();
                dungeonDataQuery.each([&](const DungeonData& dd) {
                    auto pnav = Astar(dd);
                    p = pnav.init_search(pos, targetPos).back();
                    });
                a.action = move_towards(pos, p);
                res = BEH_RUNNING;
            }
			else
				res = BEH_SUCCESS;
		});
		return res;
	}
};

struct IsLowHp : public BehNode
{
  float threshold = 0.f;
  IsLowHp(float thres) : threshold(thres) {}

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &) override
  {
    BehResult res = BEH_SUCCESS;
    entity.get([&](const Hitpoints &hp)
    {
      res = hp.hitpoints < threshold ? BEH_SUCCESS : BEH_FAIL;
    });
    return res;
  }
};

struct FindRoomTile : public BehNode
{
	char tile = ' ';
	size_t posBb = size_t(-1);
	FindRoomTile(flecs::entity entity, char t,const char* bb_name)
	{
		tile = t;
		posBb = reg_entity_blackboard_var<Position>(entity, bb_name);
	}
	BehResult update(flecs::world& ecs, flecs::entity entity, Blackboard& bb) override
	{
		BehResult res = BEH_FAIL;

		auto dungeonDataQuery = ecs.query<const DungeonData>();
		entity.insert([&](const Position& pos)
		{
			dungeonDataQuery.each([&](const DungeonData& dd)
			{
				// prebuild all walkable and get one of them
				std::vector<Position> posList;
				for (size_t y = 0; y < dd.height; ++y)
					for (size_t x = 0; x < dd.width; ++x)
                    {
                        auto tt = dd.tiles[y * dd.width + x];
                        if (dungeon::is_walkable(tt) && tt == tile)
                            posList.push_back(Position{ int(x), int(y) });
                    }
				size_t rndIdx = size_t(GetRandomValue(0, int(posList.size()) - 1));
				bb.set<Position>(posBb, posList[rndIdx]);
				res = BEH_SUCCESS;
			});
		});
        return res;
	}
};

struct FindEnemy : public BehNode
{
  size_t entityBb = size_t(-1);
  float distance = 0;
  FindEnemy(flecs::entity entity, float in_dist, const char *bb_name) : distance(in_dist)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_FAIL;
    auto enemiesQuery = ecs.query<const Position, const Team>();
    entity.insert([&](const Position &pos, const Team &t)
    {
      flecs::entity closestEnemy;
      float closestDist = FLT_MAX;
      Position closestPos;
      enemiesQuery.each([&](flecs::entity enemy, const Position &epos, const Team &et)
      {
        if (t.team == et.team)
          return;
        float curDist = dist(epos, pos);
        if (curDist < closestDist)
        {
          closestDist = curDist;
          closestPos = epos;
          closestEnemy = enemy;
        }
      });
      if (ecs.is_valid(closestEnemy) && closestDist <= distance)
      {
        bb.set<flecs::entity>(entityBb, closestEnemy);
        res = BEH_SUCCESS;
      }
    });
    return res;
  }
};

struct FindTeammate : public BehNode
{
    size_t entityBb = size_t(-1);
    float distance = 0;
    FindTeammate(flecs::entity entity, float in_dist, const char* bb_name) : distance(in_dist)
    {
        entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
    }
    BehResult update(flecs::world& ecs, flecs::entity entity, Blackboard& bb) override
    {
        BehResult res = BEH_FAIL;
        auto teammatesQuery = ecs.query<const Position, const Team>();
        entity.insert([&](const Position& pos, const Team& t)
            {
                flecs::entity closestTeammate;
                float closestDist = FLT_MAX;
                Position closestPos;
                teammatesQuery.each([&](flecs::entity mate, const Position& epos, const Team& et)
                    {
                        if (t.team != et.team)
                            return;
                        float curDist = dist(epos, pos);
                        if (curDist < closestDist)
                        {
                            closestDist = curDist;
                            closestPos = epos;
                            closestTeammate = mate;
                        }
                    });
                if (ecs.is_valid(closestTeammate) && closestDist <= distance)
                {
                    bb.set<flecs::entity>(entityBb, closestTeammate);
                    res = BEH_SUCCESS;
                }
            });
        return res;
    }
};

struct FindTeamHealer : public BehNode
{
    size_t entityBb = size_t(-1);
    float distance = 0;
    FindTeamHealer(flecs::entity entity, float in_dist, const char* bb_name) : distance(in_dist)
    {
        entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
    }
    BehResult update(flecs::world& ecs, flecs::entity entity, Blackboard& bb) override
    {
        BehResult res = BEH_FAIL;
        auto teammatesQuery = ecs.query<const Position, const Team, HealingAmount>();
        entity.insert([&](const Position& pos, const Team& t)
            {
                flecs::entity closestTeammate;
                float closestDist = FLT_MAX;
                Position closestPos;
				teammatesQuery.each([&](flecs::entity mate, const Position& epos, const Team& et, HealingAmount ha) // добавить проверку на хилера
                    {
                        if (t.team != et.team)
                            return;
                        float curDist = dist(epos, pos);
                        if (curDist < closestDist)
                        {
                            closestDist = curDist;
                            closestPos = epos;
                            closestTeammate = mate;
                        }
                    });
                if (ecs.is_valid(closestTeammate) && closestDist <= distance)
                {
                    bb.set<flecs::entity>(entityBb, closestTeammate);
                    res = BEH_SUCCESS;
                }
            });
        return res;
    }
};

struct FindTeamWounded : public BehNode
{
	size_t entityBb = size_t(-1);
	float distance = 0;
	float hpThreshold = 0;
	FindTeamWounded(flecs::entity entity, float in_dist, float minHp, const char* bb_name) : distance(in_dist), hpThreshold(minHp)
	{
		entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
	}
	BehResult update(flecs::world& ecs, flecs::entity entity, Blackboard& bb) override
	{
		BehResult res = BEH_FAIL;
		auto teammatesQuery = ecs.query<const Position, const Team, Hitpoints>();
		entity.insert([&](const Position& pos, const Team& t)
			{
				flecs::entity closestTeammate;
				float closestDist = FLT_MAX;
				Position closestPos;
				teammatesQuery.each([&](flecs::entity mate, const Position& epos, const Team& et, Hitpoints hp)
					{
						if (t.team != et.team)
							return;
						float curDist = dist(epos, pos);
						if (curDist < closestDist && hp.hitpoints < hpThreshold)
						{
							closestDist = curDist;
							closestPos = epos;
							closestTeammate = mate;
						}
					});
				if (ecs.is_valid(closestTeammate) && closestDist <= distance)
				{
					bb.set<flecs::entity>(entityBb, closestTeammate);
					res = BEH_SUCCESS;
				}
			});
		return res;
	}
};

struct Flee : public BehNode
{
  size_t entityBb = size_t(-1);
  Flee(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.insert([&](Action &a, const Position &pos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
      if (!targetEntity.is_alive())
      {
        res = BEH_FAIL;
        return;
      }
      targetEntity.get([&](const Position &target_pos)
      {
        a.action = inverse_move(move_towards(pos, target_pos));
      });
    });
    return res;
  }
};

struct Patrol : public BehNode
{
  size_t pposBb = size_t(-1);
  float patrolDist = 1.f;
  Patrol(flecs::entity entity, float patrol_dist, const char *bb_name)
    : patrolDist(patrol_dist)
  {
    pposBb = reg_entity_blackboard_var<Position>(entity, bb_name);
    entity.insert([&](Blackboard &bb, const Position &pos)
    {
      bb.set<Position>(pposBb, pos);
    });
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.insert([&](Action &a, const Position &pos)
    {
      Position patrolPos = bb.get<Position>(pposBb);
      if (dist(pos, patrolPos) > patrolDist)
        a.action = move_towards(pos, patrolPos);
      else
        a.action = GetRandomValue(EA_MOVE_START, EA_MOVE_END - 1); // do a random walk
    });
    return res;
  }
};

struct PatchUp : public BehNode
{
  float hpThreshold = 100.f;
  PatchUp(float threshold) : hpThreshold(threshold) {}

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &) override
  {
    BehResult res = BEH_SUCCESS;
    entity.insert([&](Action &a, Hitpoints &hp)
    {
      if (hp.hitpoints >= hpThreshold)
        return;
      res = BEH_RUNNING;
      a.action = EA_HEAL_SELF;
    });
    return res;
  }
};

struct HealAOE : public BehNode
{
	BehResult update(flecs::world&, flecs::entity entity, Blackboard&) override
	{
		BehResult res = BEH_SUCCESS;
		entity.insert([&](Action& a)
			{
				res = BEH_RUNNING;
				a.action = EA_HEAL_AOE;
			});
		return res;
	}
};



BehNode *sequence(const std::vector<BehNode*> &nodes)
{
  Sequence *seq = new Sequence;
  for (BehNode *node : nodes)
    seq->pushNode(node);
  return seq;
}

BehNode *selector(const std::vector<BehNode*> &nodes)
{
  Selector *sel = new Selector;
  for (BehNode *node : nodes)
    sel->pushNode(node);
  return sel;
}

BehNode *utility_selector(const std::vector<std::pair<BehNode*, utility_function>> &nodes)
{
  UtilitySelector *usel = new UtilitySelector;
  usel->utilityNodes = std::move(nodes);
  return usel;
}

BehNode *move_to_entity(flecs::entity entity, const char *bb_name)
{
  return new MoveToEntity(entity, bb_name);
}

BehNode *is_low_hp(float thres)
{
  return new IsLowHp(thres);
}

BehNode *find_enemy(flecs::entity entity, float dist, const char *bb_name)
{
  return new FindEnemy(entity, dist, bb_name);
}

BehNode *flee(flecs::entity entity, const char *bb_name)
{
  return new Flee(entity, bb_name);
}

BehNode *patrol(flecs::entity entity, float patrol_dist, const char *bb_name)
{
  return new Patrol(entity, patrol_dist, bb_name);
}

BehNode *patch_up(float thres)
{
  return new PatchUp(thres);
}

BehNode* move_to_pos(flecs::entity entity, const char* bb_name)
{
	return new MoveToPos(entity, bb_name);
}

BehNode* find_room_tile(flecs::entity entity, char tile, const char* bb_name)
{
	return new FindRoomTile(entity, tile, bb_name);
}

BehNode* find_teammate(flecs::entity entity, float dist, const char* bb_name)
{
	return new FindTeammate(entity, dist, bb_name);
}

BehNode* find_team_healer(flecs::entity entity, float dist, const char* bb_name)
{
	return new FindTeamHealer(entity, dist, bb_name);
}

BehNode* find_team_wounded(flecs::entity entity, float dist, float minHp, const char* bb_name)
{
	return new FindTeamWounded(entity, dist, minHp, bb_name);
}

BehNode* heal_aoe()
{
	return new HealAOE();
}

