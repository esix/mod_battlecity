#ifndef WORLD_H_
#define WORLD_H_


#include <vector>
#include <list>
#include <memory>
#include <utility>
#include <pthread.h>            // no std::shared_lock
#include <gst/gst.h>


#define CELL_SIZE 20

enum Orientation
{
  E_NORTH = 0,
  E_WEST  = 1,
  E_SOUTH = 2,
  E_EAST  = 3
};


class World;
class Player;



class GameObject
{
public:
  GameObject(World *world, int x, int y, int width, int height, Orientation orientation=E_NORTH) :
          _world(world), _x(x), _y(y), _width(width), _height(height), _orientation(orientation), _dx(0), _dy(0)
  {
  }

  World*      get_world()           const { return _world;       }
  int         get_x()               const { return _x;           }
  int         get_y()               const { return _y;           }
  int         get_width()           const { return _width;       }
  int         get_height()          const { return _height;      }
  Orientation get_orientation()     const { return _orientation; }

  int         get_screen_center_x() const { return _x * CELL_SIZE + _width  * CELL_SIZE / 2 + _dx; }
  int         get_screen_center_y() const { return _y * CELL_SIZE + _height * CELL_SIZE / 2 + _dy; }

  bool        passed_half_way() const;
  void        move(int speed);
  void        set_position(int x, int y)  { _x = x; _y = y; _dx = 0; _dy = 0; }

  bool        has_point(int x, int y) const { return _x <= x && x < _x + _width && _y <= y && y < _y + _height;  }

protected:
  World *     _world;
  int         _x;
  int         _y;
  int         _width;
  int         _height;
  Orientation _orientation;
  int         _dx;
  int         _dy;

  virtual void collision(const GameObject *go) {
    g_print ("Unhandled collision\n");
  };

private:
  std::vector<std::pair<int, int> > get_forward_points() const;
};



enum WallType {
  E_WALL1 = 0,
  E_WALL2,
  E_ICE1,
  E_ICE2,
  E_SOLID,
  E_TREE,
  E_WTF,
  E_BLOCK
};


class Wall : public GameObject {
public:
  Wall(World* world, int x, int y, WallType wall_type);
  void destroy()         { _destruction_timer = 500;                    }
  bool is_active() const { return _destruction_timer == 0;              }
  void live()            { if(_destruction_timer) _destruction_timer--; }
private:
  WallType _wall_type;
  int      _destruction_timer;
};



class Respawn : public GameObject {
public:
  Respawn(World* world, int x, int y) : GameObject(world, x, y, 2, 2)
  {
  }

  bool is_free() const;
};



class Explosion : public GameObject {
public:
  Explosion(World* world, int x, int y, int dx, int dy);
  ~Explosion();
  void live();
  int get_stage() const;
  bool is_active() const;
private:
  int _stage;
};



class Bullet : public GameObject {
public:
  Bullet(World* world, Player* player) :
      GameObject(world, 0, 0, 1, 1), _player(player), _active(false)
  {
  }

  bool is_active() const { return _active; }

  void activate(int x, int y, int dx, int dy, int width, int height, Orientation orientation) {
    _x = x;
    _y = y;
    _dx = dx;
    _dy = dy;
    _orientation = orientation;
    _width = width;
    _height = height;
    _active = true;
  }

  void live() {
    if(_active) {
      move(5);
    }
  }

protected:
  virtual void collision(const GameObject *go);

private:
  bool    _active;
  Player* _player;
};



class Player : public GameObject {
public:
  Player(World* world, int x, int y) : GameObject(world, x, y, 2, 2) {
    _is_moving = false;
    _bullet = new Bullet(get_world(), this);
  }

  void command_move(Orientation orientation);
  void command_fire();

  void live();

  const Bullet& get_bullet() const { return *_bullet; }

protected:
  virtual void collision(const GameObject *go) {
    g_print ("Player collision\n");
    stop_moving();
  }

private:
  bool _is_moving;
  Bullet * _bullet;

  bool is_moving() const { return _is_moving; }
  void start_moving() { _is_moving = true;  }
  void stop_moving()  { _is_moving = false; }
};



typedef std::shared_ptr<Wall>       wall_t;
typedef std::shared_ptr<Player>     player_t;
typedef std::shared_ptr<Bullet>     bullet_t;
typedef std::shared_ptr<Explosion>  explosion_t;

typedef std::vector<wall_t>         walls_t;
typedef std::vector<player_t>       players_t;
typedef std::vector<bullet_t>       bullets_t;
typedef std::vector<explosion_t>    explosions_t;

class World {
public:
  World();
  ~World();

  void live();
  player_t                 add_player();
  void                     remove_player(const player_t& player);

  explosion_t              add_explosion(int x, int y, int dx, int dy);
  void                     remove_explosion(const Explosion* explosion);

  std::vector<wall_t>      get_walls()      const;
  std::vector<player_t>    get_players()    const;
  std::vector<Bullet>      get_bullets()    const;
  std::vector<explosion_t> get_explosions() const;

  bool is_free(int x, int y) const;

  void print() const;

  const GameObject* get_game_object(int x, int y) const;
  void destroy_game_object(const GameObject* go);

private:
  Respawn get_free_respawn() const;

  mutable pthread_rwlock_t _lock;

  std::vector<wall_t>      _walls;
  std::vector<player_t>    _players;
  std::vector<explosion_t> _explosions;
  std::vector<Respawn>     _respawns;
};

#endif // WORLD_H_
