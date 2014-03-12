#include "world.h"
#include <gst/gst.h>
#include <stdlib.h>
#include <string>

using namespace std;


int abs(int n) {
  return n < 0 ? -n : n;
}
int sign(int v) {
  if(v < 0) return -1;
  if(v > 0) return 1;
  return 0;
}

std::vector<std::pair<int, int> > GameObject::get_forward_points() const
{
  std::vector<std::pair<int, int> > points;
  int x,y;

  switch(_orientation) {
    case E_EAST:
      x = _x + _width;
      for(y = _y; y < _y + _height; y++) points.push_back(make_pair(x, y));
      break;
    case E_NORTH:
      y = _y - 1;
      for(x = _x; x < _x + _width; x++) points.push_back(make_pair(x, y));
      break;
    case E_WEST:
      x = _x - 1;
      for(y = _y; y < _y + _height; y++) points.push_back(make_pair(x, y));
      break;
    case E_SOUTH:
      y = _y + _height;
      for(x = _x; x < _x + _width; x++) points.push_back(make_pair(x, y));
      break;
  }
  return points;
}

bool GameObject::passed_half_way() const
{
  const int N = 0;   // CELL_SIZE / 4
  switch(_orientation) {
    case E_EAST:  return _dx >  N;
    case E_NORTH: return _dy < -N;
    case E_WEST:  return _dx < -N;
    case E_SOUTH: return _dy >  N;
  }
}

void GameObject::move(int speed)
{
  switch(_orientation) {
    case E_EAST:  _dx += speed; break;
    case E_NORTH: _dy -= speed; break;
    case E_WEST:  _dx -= speed; break;
    case E_SOUTH: _dy += speed; break;
  }
  if(_dx >= CELL_SIZE / 2) { _dx -= CELL_SIZE; _x++; }
  if(_dx < -CELL_SIZE / 2) { _dx += CELL_SIZE; _x--; }
  if(_dy >= CELL_SIZE / 2) { _dy -= CELL_SIZE; _y++; }
  if(_dy < -CELL_SIZE / 2) { _dy += CELL_SIZE; _y--; }

  if(passed_half_way()) {
    std::vector<std::pair<int, int> > points = get_forward_points();
    for(int i = 0; i < points.size(); i++) {
      int x = points[i].first, y = points[i].second;

      if(!(0 <= x && x < 16 && 0 <= y && y < 12)) {
        this->collision(NULL);     //  TODO: special objects: off the world?
        return;
      }

      const GameObject *go = _world->get_game_object(x, y);
      if(go) {
        this->collision(go);
        return;
      }
    }
  }
}


//
// Wall
//

Wall::Wall(World* world, int x, int y, WallType wall_type) :
      GameObject(world, x, y, 1, 1),
      _wall_type(wall_type),
      _destruction_timer(0)
{
}



//
// Bullet
//

void Bullet::collision(const GameObject *go)
{
  if(go == _player) {
    return;               // do not hert myself
  }
  _active = false;
  if(go) {
    _world->destroy_game_object(go);
  }
  _world->add_explosion(_x, _y, _dx, _dy);
}



//
// Respawn
//

bool Respawn::is_free() const
{
  return _world->is_free(_x, _y) && _world->is_free(_x+1, _y) &&
         _world->is_free(_x, _y+1) && _world->is_free(_x+1, _y+1);
}



//
// Explosion
//

Explosion::Explosion(World* world, int x, int y, int dx, int dy) : GameObject(world, x, y, 2, 2)
{
  g_print ("Explosion constructor\n");
  _dx = dx;
  _dy = dy;
  _stage = 0;
}

Explosion::~Explosion()
{
  g_print ("Explosion destructor\n");
}

void Explosion::live()
{
  _stage++;
  if(!is_active()) {
    _world->remove_explosion(this);            // TODO: review code to ensure this is not deleted
  }
}

int Explosion::get_stage() const
{
  return _stage / 4;
}

bool Explosion::is_active() const
{
  return get_stage() <= 6;
}



//
// Player
//

void Player::command_move(Orientation orientation)
{
  if(orientation == _orientation) {
    if(is_moving())
      stop_moving();
    else
      start_moving();
  } else {
    if(is_moving())
      stop_moving();
    _orientation = orientation;
    g_print ("changed orientation=%d\n", _orientation);
    g_print ("Screen coord=%d %d\n", get_screen_center_x(), get_screen_center_y());
  }
}

void Player::command_fire()
{
  if(_bullet->is_active()) return;

  const int C2 = CELL_SIZE / 2;

  int x = get_x() + 1;
  int y = get_y() + 1;
  int dx = _dx - C2;
  int dy = _dy - C2;
  int w = 1;
  int h = 1;

  while (dx >= C2) { x++; dx -= CELL_SIZE; }
  while (dy >= C2) { y++; dy -= CELL_SIZE; }
  while (dx < -C2) { x--; dx += CELL_SIZE; }
  while (dy < -C2) { y--; dy += CELL_SIZE; }

  switch(_orientation) {
    case E_EAST :
    case E_WEST :     // horizontal
      if(abs(dy) <= 2 || abs(dy) >= 8) {
        h = 2;
        dy -= CELL_SIZE / 2;
      }
      break;
    case E_NORTH:
    case E_SOUTH:
      if(abs(dx) <= 2 || abs(dx) >= 8) {
        w = 2;
        dx -= CELL_SIZE / 2;
      }
      break;
  }
  g_print ("Bullet init tnk=(%d %d) x=%d y=%d, dx=%d dy=%d, w=%d h=%d\n", _x, _y, x, y, dx, dy, w, h);


  _bullet->activate(x, y, dx, dy, w, h, get_orientation());
}




void Player::live() {
  if(is_moving()) {
    move(2);
  }

  _bullet->live();
}



//
// World
//

static const char *world_map[] = {
  "R       ww    R ",
  "        ww      ",
  "    ww          ",
  "    ww          ",
  "ww  ww  wwww  ww",
  "ww  ww  wwww  ww",
  "ww  wwww  ww  ww",
  "ww  wwww  ww  ww",
  "          ww    ",
  "          ww    ",
  "R     ww      R ",
  "      ww        "
};

World::World()
{
  pthread_rwlock_init(&_lock, NULL);

  for(int y = 0; y < 12; y++) {
    for(int x = 0; x < 16; x++) {
      char fld = world_map[y][x];
      switch(fld) {
        case 'w':
          _walls.push_back(shared_ptr<Wall>(new Wall(this, x, y, E_WALL2)));
          break;
        case 'R':
          _respawns.push_back(Respawn(this, x, y));
      }
    }
  }
}

World::~World()
{
  pthread_rwlock_destroy(&_lock);
}

void World::live()
{
  for(int i = 0; i < _walls.size(); i++)
    _walls[i]->live();

  players_t ps = get_players();
  for(players_t::iterator it = ps.begin(); it != ps.end(); ++it) {
    (*it)->live();
  }

  explosions_t es = get_explosions();
  for(explosions_t::iterator it = es.begin(); it != es.end(); ++it) {
    (*it)->live();
  }
}

Respawn World::get_free_respawn() const
{
  vector<Respawn> resps;
  for(int i = 0; i < _respawns.size(); i++)
    if(_respawns[i].is_free())
      resps.push_back(_respawns[i]);
  if(resps.size() == 0)
    resps = _respawns;
  return resps[random() % resps.size()];
}

player_t World::add_player()
{
  Respawn resp = get_free_respawn();

  player_t player(new Player(this, resp.get_x(), resp.get_y()));
  {
    pthread_rwlock_wrlock(&_lock);
    _players.push_back(player);
    pthread_rwlock_unlock(&_lock);
  }
  return player;
}

explosion_t World::add_explosion(int x, int y, int dx, int dy)
{
  explosion_t e(new Explosion(this, x, y, dx, dy));
  {
    pthread_rwlock_wrlock(&_lock);
    _explosions.push_back(e);
    pthread_rwlock_unlock(&_lock);
  }
  return e;
}

void World::remove_explosion(const Explosion* explosion)
{
  g_print ("Start removing explosion\n");

  pthread_rwlock_wrlock(&_lock);
  for(std::vector<explosion_t>::iterator it = _explosions.begin(); it != _explosions.end(); ++it) {
    if(it->get() == explosion) {
      g_print ("Removing explosion\n");
      _explosions.erase(it);
      break;
    }
  }
  pthread_rwlock_unlock(&_lock);
}

players_t World::get_players() const
{
  std::vector<player_t> result;
  {
    pthread_rwlock_rdlock(&_lock);
    result = _players;
    pthread_rwlock_unlock(&_lock);
  }
  return result;
}

explosions_t World::get_explosions() const
{
  explosions_t result;
  {
    pthread_rwlock_rdlock(&_lock);
    result = _explosions;
    pthread_rwlock_unlock(&_lock);
  }
  return result;
}


std::vector<Bullet> World::get_bullets() const
{
  players_t players = get_players();
  vector<Bullet> bullets;
  for(int i = 0; i < players.size(); i++) {
    if(players[i]->get_bullet().is_active()) {
      bullets.push_back(players[i]->get_bullet());
    }
  }
  return bullets;
}


std::vector<std::shared_ptr<Wall> > World::get_walls() const
{
  std::vector< shared_ptr<Wall> > walls;
  for(int i = 0; i < _walls.size(); i++)
    if(_walls[i]->is_active())
      walls.push_back(_walls[i]);
  return walls;
}

bool World::is_free(int x, int y) const
{
  return get_game_object(x,y) == NULL;
}

const GameObject* World::get_game_object(int x, int y) const
{
  for(int i = 0; i < _walls.size(); i++) {
    if(_walls[i]->is_active() && _walls[i]->has_point(x,y)) {
      return _walls[i].get();
    }
  }

  std::vector<std::shared_ptr<Player> > players = get_players();
  for(int i = 0; i < players.size(); i++) {
    std::shared_ptr<Player> p = players[i];
    if(p->has_point(x, y)) return p.get();
  }

  return NULL;
}

void World::destroy_game_object(const GameObject* go) {
  for(int i = 0; i < _walls.size(); i++) {
    std::shared_ptr<Wall> wall = _walls[i];
    if(wall.get() == go) {
      wall->destroy();
      return;
    }
  }

  for(int i = 0; i < _players.size(); i++) {
    std::shared_ptr<Player> p = _players[i];
    if(p.get() == go) {
      // TODO: create explosion
      Respawn resp = get_free_respawn();
      p->set_position(resp.get_x(), resp.get_y());
      return;
    }
  }
}


void World::print() const
{
  vector<string> a(12, "................");

  std::vector< std::shared_ptr<Wall> > walls = get_walls();
  for(int i = 0; i < walls.size(); i++) {
    std::shared_ptr<Wall> wall = walls[i];
    int x = wall->get_x(), y = wall->get_y();
    a[y][x] = wall->is_active() ? 'w' : '_';
  }

  std::vector<std::shared_ptr<Player> > players = get_players();
  for(int i = 0; i < players.size(); i++) {
    std::shared_ptr<Player> p = players[i];
    int y = p->get_y(), x = p->get_x();
    a[y][x] = 'T';
    a[y+1][x] = 'T';
    a[y][x+1] = 'T';
    a[y+1][x+1] = 'T';
  }


  for(int i = 0; i < 12; i++) {
    g_print ("%s\n", a[i].c_str());
  }
}
