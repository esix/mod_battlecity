#ifndef RENDERER_H_
#define RENDERER_H_

#include "world.h"
#include <vector>

// 352x288
#define WIDTH 320
#define HEIGHT 240
#define SWIDTH  "320"
#define SHEIGHT "240"


class Texture;

class Canvas {
public:
  Canvas();
  ~Canvas();
  void     set_pixel(int x, int y, char r, char g, char b);
  char*    get_data() const;
  int      get_data_size() const;
  int      get_height() const;
  int      get_width() const;

private:
  char *_data;
};


class Renderer {
public:
  Renderer();
  ~Renderer();

  void render_world(const std::shared_ptr<Player> world, Canvas& canvas);

private:
  Texture * _tank;
  Texture * _enemy_tank;
  Texture * _tiles;
  Texture * _bullet;
  Texture * _explosion;

  void put_texture(const Texture* texture, int center_x, int center_y, int orientation,  Canvas& canvas);
  void put_texture(const Texture* texture, int center_x, int center_y, int txt_start_x, int txt_start_y, int txt_width, int txt_height, int orientation, Canvas& canvas);

};

#endif // RENDERER_H_
