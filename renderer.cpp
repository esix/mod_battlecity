#include "renderer.h"
#include <stddef.h>
#include <stdlib.h>
#include <cstring>
#include <map>
#include <string>
#include <sstream>
#include <vector>
#include <gst/gst.h>

#include "resources/tank.xpm"
#include "resources/enemy_tank.xpm"
#include "resources/tiles.xpm"
#include "resources/bullet.xpm"
#include "resources/explosion.xpm"



using namespace std;

struct RGB {
  unsigned char r, g, b;
};


class Texture
{
public:
  Texture(const char **data);
  int  get_width()             const { return _width;      }
  int  get_height()            const { return _height;     }
  RGB  get_pixel(int x, int y) const { return _data[y][x]; }
  bool has_pixel(int x, int y) const { return _mask[y][x]; }

private:
  int _width;
  int _height;
  vector< vector<RGB> >  _data;
  vector< vector<bool> > _mask;
};


Texture::Texture(const char * data[]) {
  int palette_length, cpp;
  sscanf(data[0], "%d %d %d %d", &_width, &_height, &palette_length, &cpp);

  string none_color = "";
  map<string, RGB> palette;
  for(int i = 0; i < palette_length; i++) {
    string line = data[1+i];
    string palette_id = line.substr(0, cpp);
    string color_value = line.substr(line.length() - 6);
    int clr = std::stoul(color_value, nullptr, 16);
    RGB rgb;
    rgb.r = (clr >> 16) & 0xff;
    rgb.g = (clr >>  8) & 0xff;
    rgb.b = (clr >>  0) & 0xff;
    palette[palette_id] = rgb;
    if(line.substr(line.length()-4) == string("None")) none_color = palette_id;
  }

  _data = vector< vector<RGB>  >(_height, vector<RGB>(_width));
  _mask = vector< vector<bool> >(_height, vector<bool>(_width, true));

  for(int y = 0; y < _height; y++) {
    string line = data[1 + palette_length + y];
    for(int x = 0; x < _width; x++) {
      string palette_idx = line.substr(x * cpp, cpp);
      _data[y][x] = palette[palette_idx];
      if(palette_idx == none_color) _mask[y][x] = false;
    }
  }
}



Canvas::Canvas() {
  int size = get_data_size();
  _data = new char[size];
  memset(_data, 0x00, size);
}

Canvas::~Canvas() {
  delete[] _data;
  _data = NULL;
}

void Canvas::set_pixel(int x, int y, char r, char g, char b) {
  int ptr = 3 * (y * WIDTH + x);
  _data[ptr + 0] = r;
  _data[ptr + 1] = g;
  _data[ptr + 2] = b;
  //_data[ptr + 3] = 0;
}
char* Canvas::get_data     () const {  return _data;              }
int   Canvas::get_data_size() const {  return WIDTH * HEIGHT * 3; }
int   Canvas::get_width    () const {  return WIDTH;              }
int   Canvas::get_height   () const {  return HEIGHT;             }



Renderer::Renderer()
{
  _tank = new Texture(tank);
  _enemy_tank = new Texture(enemy_tank);
  _tiles = new Texture(tiles);
  _bullet = new Texture(bullet);
  _explosion = new Texture(explosion);
}

Renderer::~Renderer()
{
  delete _tank;
  delete _enemy_tank;
  delete _tiles;
  delete _bullet;
  delete _explosion;
}

void Renderer::render_world(const shared_ptr<Player> player, Canvas& canvas) {
  World *world = player->get_world();

  // render walls
  std::vector<wall_t> walls = world->get_walls();
  for(int i = 0; i < walls.size(); i++) {
    std::shared_ptr<Wall> wall = walls[i];
    put_texture(_tiles, wall->get_screen_center_x(), wall->get_screen_center_y(),
                20,0,20,20,
                0,
                canvas);
  }

  // render tanks
  std::vector<player_t> players = world->get_players();
  for(int i = 0; i < players.size(); i++) {
    std::shared_ptr<Player> p = players[i];
    if(p != player)
      put_texture(_enemy_tank, p->get_screen_center_x(), p->get_screen_center_y(), p->get_orientation(), canvas);
  }
  // player tank
  put_texture(_tank, player->get_screen_center_x(), player->get_screen_center_y(), player->get_orientation(), canvas);

  // render bullets
  std::vector<Bullet> bullets = world->get_bullets();
  for(int i = 0; i < bullets.size(); i++) {
    put_texture(_bullet, bullets[i].get_screen_center_x(), bullets[i].get_screen_center_y(), bullets[i].get_orientation(), canvas);
  }


  // render explosions
  explosions_t explosions = world->get_explosions();
  for(explosions_t::iterator it = explosions.begin(); it != explosions.end(); ++it) {
    int stage = (*it)->get_stage();
    if(0 <= stage && stage <= 6) {
      int x = stage * 20;
      put_texture(_explosion, (*it)->get_screen_center_x(), (*it)->get_screen_center_y(),
                  x, 0, 20,20,
                  0,
                  canvas);
    }
  }


  for(int y = 0; y < 12; y++) {
    for(int x = 0; x < 16; x++) {
      canvas.set_pixel(x * CELL_SIZE, y * CELL_SIZE, 0, 255, 0);
    }
  }

}


void Renderer::put_texture(const Texture* texture, int center_x, int center_y, int orientation, Canvas& canvas) {
  put_texture(texture, center_x, center_y, 0, 0, texture->get_width(), texture->get_height(), orientation, canvas);
}

void Renderer::put_texture(const Texture* texture,
                           int center_x, int center_y,
                           int txt_start_x, int txt_start_y, int txt_width, int txt_height,
                           int orientation,
                           Canvas& canvas) {
  int screen_x, screen_y;
  switch(orientation) {
    case E_NORTH: screen_y = center_y - txt_height / 2; screen_x = center_x - txt_width  / 2; break;
    case E_WEST : screen_y = center_y - txt_width  / 2; screen_x = center_x - txt_height / 2; break;
    case E_SOUTH: screen_y = center_y - txt_height / 2; screen_x = center_x - txt_width  / 2; break;
    case E_EAST : screen_y = center_y - txt_width  / 2; screen_x = center_x - txt_height / 2; break;
  }
  //g_print ("screen coords: %d %d \n", screen_x,screen_y);

  for(int txt_y = txt_start_y; txt_y < txt_start_y + txt_height; txt_y++) {
    for(int txt_x = txt_start_x; txt_x < txt_start_x + txt_width; txt_x++) {

      int ty = txt_y - txt_start_y;
      int tx = txt_x - txt_start_x;

      int inv_ty = txt_height - ty - 1;
      int inv_tx = txt_width - tx - 1;

      int oy, ox;
      switch(orientation) {
        case E_NORTH: oy = ty;      ox = tx;     break;
        case E_WEST : oy = inv_tx;  ox = ty;     break;
        case E_SOUTH: oy = inv_ty;  ox = inv_tx; break;
        case E_EAST : oy = tx;      ox = inv_ty; break;
      }

      int y = screen_y + oy, x = screen_x + ox;

      if(0 <= y && 0 <= x && y < HEIGHT && x < WIDTH) {
        if(texture->has_pixel(txt_x, txt_y)) {
          RGB color = texture->get_pixel(txt_x, txt_y);
          canvas.set_pixel(x, y, color.r, color.g, color.b);
        }
      }

    }
  }
}
