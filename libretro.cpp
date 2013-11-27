/*
 *  InstancingViewer Tech demo
 *  Copyright (C) 2013 - Hans-Kristian Arntzen
 *  Copyright (C) 2013 - Daniel De Matteis
 *
 *  InstancingViewer is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  InstancingViewer is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with InstancingViewer.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "libretro.h"
#include "libretro_private.h"
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <algorithm>
#include <string>
#include <vector>
#include "rpng.h"

#include "gl.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static struct retro_hw_render_callback hw_render;

using namespace glm;

#define BASE_WIDTH 320
#define BASE_HEIGHT 240
#ifdef GLES
#define MAX_WIDTH 1024
#define MAX_HEIGHT 1024
#else
#define MAX_WIDTH 1920
#define MAX_HEIGHT 1600
#endif

enum {
   LAUNCH_CATEGORY_GAME = 0,
#if !defined(ANDROID) && !defined(IOS)
   LAUNCH_CATEGORY_MOVIE,
#endif
   LAUNCH_CATEGORY_SCENE1,
   LAUNCH_CATEGORY_SCENE2,
   LAUNCH_CATEGORY_MODEL1,
   LAUNCH_CATEGORY_MODEL2,
   LAUNCH_CATEGORY_END
};

static unsigned launch_category = 0;

static unsigned cube_size = 1;
static float cube_stride = 4.0f;
static unsigned width = BASE_WIDTH;
static unsigned height = BASE_HEIGHT;

static std::string texpath;

static GLuint prog;
static GLuint vbo;
static GLuint tex;
static bool update;

static vec3 player_pos;

static float camera_rot_x;
static float camera_rot_y;

struct Vertex
{
   GLfloat vert[4];
   GLfloat normal[4];
   GLfloat tex[2];
};

struct Cube
{
   struct Vertex vertices[36];
};

static const Vertex vertex_data[] = {
   { { -1, -1, -1, 1 }, { 0, 0, -1, 0 }, { 0, 0 } }, // Front
   { {  1, -1, -1, 1 }, { 0, 0, -1, 0 }, { 1, 0 } },
   { { -1,  1, -1, 1 }, { 0, 0, -1, 0 }, { 0, 1 } },
   { {  1,  1, -1, 1 }, { 0, 0, -1, 0 }, { 1, 1 } },

   { {  1, -1,  1, 1 }, { 0, 0,  1, 0 }, { 0, 0 } }, // Back
   { { -1, -1,  1, 1 }, { 0, 0,  1, 0 }, { 1, 0 } },
   { {  1,  1,  1, 1 }, { 0, 0,  1, 0 }, { 0, 1 } },
   { { -1,  1,  1, 1 }, { 0, 0,  1, 0 }, { 1, 1 } },
   
   { { -1, -1,  1, 1 }, { -1, 0, 0, 0 }, { 0, 0 } }, // Left
   { { -1, -1, -1, 1 }, { -1, 0, 0, 0 }, { 1, 0 } },
   { { -1,  1,  1, 1 }, { -1, 0, 0, 0 }, { 0, 1 } },
   { { -1,  1, -1, 1 }, { -1, 0, 0, 0 }, { 1, 1 } },

   { { 1, -1, -1, 1 }, { 1, 0, 0, 0 }, { 0, 0 } }, // Right
   { { 1, -1,  1, 1 }, { 1, 0, 0, 0 }, { 1, 0 } },
   { { 1,  1, -1, 1 }, { 1, 0, 0, 0 }, { 0, 1 } },
   { { 1,  1,  1, 1 }, { 1, 0, 0, 0 }, { 1, 1 } },

   { { -1,  1, -1, 1 }, { 0, 1, 0, 0 }, { 0, 0 } }, // Top
   { {  1,  1, -1, 1 }, { 0, 1, 0, 0 }, { 1, 0 } },
   { { -1,  1,  1, 1 }, { 0, 1, 0, 0 }, { 0, 1 } },
   { {  1,  1,  1, 1 }, { 0, 1, 0, 0 }, { 1, 1 } },

   { { -1, -1,  1, 1 }, { 0, -1, 0, 0 }, { 0, 0 } }, // Bottom
   { {  1, -1,  1, 1 }, { 0, -1, 0, 0 }, { 1, 0 } },
   { { -1, -1, -1, 1 }, { 0, -1, 0, 0 }, { 0, 1 } },
   { {  1, -1, -1, 1 }, { 0, -1, 0, 0 }, { 1, 1 } },
};

static const GLubyte indices[] = {
   0, 1, 2, // Front
   3, 2, 1,

   4, 5, 6, // Back
   7, 6, 5,

   8, 9, 10, // Left
   11, 10, 9,

   12, 13, 14, // Right
   15, 14, 13,

   16, 17, 18, // Top
   19, 18, 17,

   20, 21, 22, // Bottom
   23, 22, 21,
};

static const char *vertex_shader[] = {
   "uniform mat4 uVP;",
   "uniform mat4 uM;",
   "attribute vec4 aVertex;",
   "attribute vec4 aNormal;",
   "attribute vec2 aTexCoord;",
   "varying vec3 normal;",
   "varying vec4 model_pos;",
   "varying vec2 tex_coord;",
   "void main() {",
   "  model_pos = uM * aVertex;",
   "  gl_Position = uVP * model_pos;",
   "  vec4 trans_normal = uM * aNormal;",
   "  normal = trans_normal.xyz;",
   "  tex_coord = vec2(1.0 - aTexCoord.x, aTexCoord.y);",
   "}",
};

static const char *fragment_shader[] = {
#ifdef GLES
   "precision mediump float; \n",
#endif
   "varying vec3 normal;",
   "varying vec4 model_pos;",
   "varying vec2 tex_coord;",
   "uniform vec3 light_pos;",
   "uniform vec4 ambient_light;",
   "uniform sampler2D uTexture;",

   "void main() {",
   "  vec3 diff = light_pos - model_pos.xyz;",
   "  float dist_mod = 100.0 * inversesqrt(dot(diff, diff));",
   "  gl_FragColor = texture2D(uTexture, tex_coord) * (ambient_light + dist_mod * smoothstep(0.0, 1.0, dot(normalize(diff), normal)));",
   "}",
};

static void print_shader_log(GLuint shader)
{
   GLsizei len = 0;
   SYM(glGetShaderiv)(shader, GL_INFO_LOG_LENGTH, &len);
   if (!len)
      return;

   char *buffer = new char[len];
   SYM(glGetShaderInfoLog)(shader, len, &len, buffer);
   fprintf(stderr, "Info Log: %s\n", buffer);
   delete[] buffer;
}

static void compile_program(void)
{
   prog = SYM(glCreateProgram)();
   GLuint vert = SYM(glCreateShader)(GL_VERTEX_SHADER);
   GLuint frag = SYM(glCreateShader)(GL_FRAGMENT_SHADER);

   SYM(glShaderSource)(vert, ARRAY_SIZE(vertex_shader), vertex_shader, 0);
   SYM(glShaderSource)(frag, ARRAY_SIZE(fragment_shader), fragment_shader, 0);
   SYM(glCompileShader)(vert);
   SYM(glCompileShader)(frag);

   int status = 0;
   SYM(glGetShaderiv)(vert, GL_COMPILE_STATUS, &status);
   if (!status)
   {
      fprintf(stderr, "Vertex shader failed to compile!\n");
      print_shader_log(vert);
   }
   SYM(glGetShaderiv)(frag, GL_COMPILE_STATUS, &status);
   if (!status)
   {
      fprintf(stderr, "Fragment shader failed to compile!\n");
      print_shader_log(frag);
   }

   SYM(glAttachShader)(prog, vert);
   SYM(glAttachShader)(prog, frag);
   SYM(glLinkProgram)(prog);

   SYM(glGetProgramiv)(prog, GL_LINK_STATUS, &status);
   if (!status)
      fprintf(stderr, "Program failed to link!\n");
}

static void setup_vao(void)
{
   SYM(glGenBuffers)(1, &vbo);

   update = true;
}

static GLuint load_texture(const char *path)
{
   uint8_t *data;
   unsigned width, height;
   if (!rpng_load_image_rgba(path, &data, &width, &height))
   {
      fprintf(stderr, "Couldn't load texture: %s\n", path);
      return 0;
   }

   GLuint tex;
   SYM(glGenTextures)(1, &tex);
   SYM(glBindTexture)(GL_TEXTURE_2D, tex);

   SYM(glTexImage2D)(GL_TEXTURE_2D, 0, GL_RGBA, width, height,
         0, GL_RGBA, GL_UNSIGNED_BYTE, data);
   free(data);

   SYM(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   SYM(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   return tex;
}

void retro_init(void)
{}

void retro_deinit(void)
{}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "InstancingViewer GL";
   info->library_version  = "v2";
   info->need_fullpath    = false;
   info->valid_extensions = "png";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->timing.fps = 60.0;
   info->timing.sample_rate = 30000.0;

   info->geometry.base_width  = BASE_WIDTH;
   info->geometry.base_height = BASE_HEIGHT;
   info->geometry.max_width   = MAX_WIDTH;
   info->geometry.max_height  = MAX_HEIGHT;
}

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

#ifdef ANDROID
#include <android/log.h>
#endif

#include <stdarg.h>

void retro_stderr(const char *str)
{
#if defined(_WIN32)
   OutputDebugStringA(str);
#elif defined(ANDROID)
   __android_log_print(ANDROID_LOG_INFO, "ModelViewer: ", "%s", str);
#else
   fputs(str, stderr);
#endif
}

void retro_stderr_print(const char *fmt, ...)
{
   char buf[1024];
   va_list list;
   va_start(list, fmt);
   vsprintf(buf, fmt, list); // Unsafe, but vsnprintf isn't in C++03 :(
   va_end(list);
   retro_stderr(buf);
}


void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   struct retro_variable variables[] = {
      { "resolution",
#ifdef GLES
         "Internal resolution; 320x240|360x480|480x272|512x384|512x512|640x240|640x448|640x480|720x576|800x600|960x720|1024x768" },
#else
      "Internal resolution; 320x240|360x480|480x272|512x384|512x512|640x240|640x448|640x480|720x576|800x600|960x720|1024x768|1024x1024|1280x720|1280x960|1600x1200|1920x1080|1920x1440|1920x1600" },
#endif
                        {
         "cube_size",
         "Cube size; 1|2|4|8|16|32|64|128" },
                        {
         "cube_stride",
         "Cube stride; 2.0|3.0|4.0|5.0|6.0|7.0|8.0" },
                        {
         "launch_category",
#if !defined(ANDROID) || !defined(IOS)
         "Launch category; games|movies|scene1|scene2|model1|model2" },
#else
         "Launch category; games|scene1|scene2|model1|model2" },
#endif
      { NULL, NULL },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

static bool check_closest_cube(vec3 cube_max, vec3 closest_cube)
{
   return true;/*(((closest_cube.x > 0.0f) && (closest_cube.y > 0.0f) && (closest_cube.z > 0.0f)) &&
      ((closest_cube.x < cube_max.x) && (closest_cube.y < cube_max.y) && (closest_cube.z < cube_max.z)));*/
}

static bool check_cube_distance_per_dimension(vec3 cube)
{
   return cube.x * cube.x + cube.y * cube.y + cube.z * cube.z < 25.0f;
}

#ifdef ANDROID
#define LIB_DIR "/data/app-lib/org.retroarch-1"
#define ROM_DIR "/storage/sdcard1/roms"
#define FORMAT_STR "%s/libretro_%s.so"
#else
#define LIB_DIR "/home/squarepusher/local-repos/libretro-super/dist/unix"
#define ROM_DIR "/home/squarepusher/roms"
#define FORMAT_STR "%s/%s_libretro.so"
#endif

static void hit(vec3 cube)
{
   (void)cube;
   char path[256];

   switch (launch_category)
   {
      case LAUNCH_CATEGORY_GAME:
         snprintf(path, sizeof(path), FORMAT_STR, LIB_DIR, "pcsx_rearmed");
         if (environ_cb(RETRO_ENVIRONMENT_SET_LIBRETRO_PATH, (void*)&path))
         {
            snprintf(path, sizeof(path), "%s/%s", ROM_DIR, "tombraider.bin");
            environ_cb(RETRO_ENVIRONMENT_EXEC_ESCAPE, (void*)&path);
         }
         break;
#if !defined(ANDROID) && !defined(IOS)
      case LAUNCH_CATEGORY_MOVIE:
         snprintf(path, sizeof(path), FORMAT_STR, LIB_DIR, "ffmpeg");
         if (environ_cb(RETRO_ENVIRONMENT_SET_LIBRETRO_PATH, (void*)&path))
         {
            snprintf(path, sizeof(path), "%s/%s", ROM_DIR, "lionking.mp4");
            environ_cb(RETRO_ENVIRONMENT_EXEC_ESCAPE, (void*)&path);
         }
         break;
#endif
      case LAUNCH_CATEGORY_SCENE1:
         snprintf(path, sizeof(path), FORMAT_STR, LIB_DIR, "scenewalker");
         if (environ_cb(RETRO_ENVIRONMENT_SET_LIBRETRO_PATH, (void*)&path))
         {
            snprintf(path, sizeof(path), "%s/%s", ROM_DIR, "models/silenthill3_chapel/model.obj");
            environ_cb(RETRO_ENVIRONMENT_EXEC_ESCAPE, (void*)&path);
         }
         break;
      case LAUNCH_CATEGORY_SCENE2:
         snprintf(path, sizeof(path), FORMAT_STR, LIB_DIR, "scenewalker");
         if (environ_cb(RETRO_ENVIRONMENT_SET_LIBRETRO_PATH, (void*)&path))
         {
            snprintf(path, sizeof(path), "%s/%s", ROM_DIR, "models/Onechanbara - Hospital - by fullmoon/hospital.obj");
            environ_cb(RETRO_ENVIRONMENT_EXEC_ESCAPE, (void*)&path);
         }
         break;
      case LAUNCH_CATEGORY_MODEL1:
         snprintf(path, sizeof(path), FORMAT_STR, LIB_DIR, "modelviewer");
         if (environ_cb(RETRO_ENVIRONMENT_SET_LIBRETRO_PATH, (void*)&path))
         {
            snprintf(path, sizeof(path), "%s/%s", ROM_DIR, "models/mazda-3-mps/mazda 3.obj");
            environ_cb(RETRO_ENVIRONMENT_EXEC_ESCAPE, (void*)&path);
         }
         break;
      case LAUNCH_CATEGORY_MODEL2:
         snprintf(path, sizeof(path), FORMAT_STR, LIB_DIR, "modelviewer");
         if (environ_cb(RETRO_ENVIRONMENT_SET_LIBRETRO_PATH, (void*)&path))
         {
            snprintf(path, sizeof(path), "%s/%s", ROM_DIR, "models/Vanille-working/vanille_obj.obj");
            environ_cb(RETRO_ENVIRONMENT_EXEC_ESCAPE, (void*)&path);
         }
         break;
   }
}

static void check_collision_cube()
{
   float cube_origin = cube_stride * ((float)cube_size / -2.0f);
   // emulate cube origin at {0, 0, 0}
   vec3 shifted_player_pos = player_pos;
   shifted_player_pos.z += 100;
   vec3 closest_cube = vec3(0, 0, 0);//round((shifted_player_pos - cube_origin) / cube_stride);
   vec3 closest_cube_pos = vec3(0, 0, 0);//cube_origin + closest_cube * cube_stride;
   vec3 cube_distance = abs(shifted_player_pos - closest_cube_pos);
   vec3 cube_size_max = (vec3)((float)cube_size - 1);
   fprintf(stderr, "cube_origin: %f\n", cube_origin);
   fprintf(stderr, "shifted_player_pos: %f %f %f\n", shifted_player_pos.x, shifted_player_pos.y, shifted_player_pos.z);
   fprintf(stderr, "cube: %f %f %f\n", closest_cube.x, closest_cube.y, closest_cube.z);
   fprintf(stderr, "cube_pos: %f %f %f\n", closest_cube_pos.x, closest_cube_pos.y, closest_cube_pos.z);
   fprintf(stderr, "cube_distance: %f %f %f\n", cube_distance.x, cube_distance.y, cube_distance.z);
   if (check_closest_cube(cube_size_max, closest_cube) &&
         check_cube_distance_per_dimension(cube_distance))
      hit(closest_cube);
}

static void context_reset(void)
{
   fprintf(stderr, "Context reset!\n");

   GL::set_function_cb(hw_render.get_proc_address);
   GL::init_symbol_map();
   compile_program();
   setup_vao();
   tex = load_texture(texpath.c_str());
}

#ifdef ANDROID
#define PICS_HOME "/storage/sdcard1/roms"
#else
#define PICS_HOME "/home/squarepusher/roms"
#endif

static vec3 check_input()
{
   static unsigned select_timeout = 0;
   input_poll_cb();

   int x = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
   int y = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
   x = std::max(std::min(x, 20), -20);
   y = std::max(std::min(y, 20), -20);
   camera_rot_x -= 0.20 * x;
   camera_rot_y -= 0.10 * y;

   camera_rot_y = std::max(std::min(camera_rot_y, 80.0f), -80.0f);

   mat4 look_rot_x = rotate(mat4(1.0), camera_rot_x, vec3(0, 1, 0));
   mat4 look_rot_y = rotate(mat4(1.0), camera_rot_y, vec3(1, 0, 0));
   vec3 look_dir = vec3(look_rot_x * look_rot_y * vec4(0, 0, -1, 0));

   vec3 look_dir_side = vec3(look_rot_x * vec4(1, 0, 0, 0));

   mat3 s = mat3(scale(mat4(1.0), vec3(0.25, 0.25, 0.25)));
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
      player_pos += s * look_dir;
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
      player_pos -= s * look_dir;

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
      player_pos -= s * look_dir_side;
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
      player_pos += s * look_dir_side;

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT) 
         && select_timeout == 0)
   {
      select_timeout = 30;
      switch (launch_category)
      {
#if !defined(ANDROID) && !defined(IOS)
         case LAUNCH_CATEGORY_GAME:
            launch_category = LAUNCH_CATEGORY_MOVIE;
            texpath = std::string(PICS_HOME) + "/lionking.png";
            context_reset();
            break;
         case LAUNCH_CATEGORY_MOVIE:
            launch_category = LAUNCH_CATEGORY_SCENE1;
            texpath = std::string(PICS_HOME) + "/scene1.png";
            context_reset();
            break;
#else
         case LAUNCH_CATEGORY_GAME:
            launch_category = LAUNCH_CATEGORY_SCENE1;
            texpath = std::string(PICS_HOME) + "/scene1.png";
            context_reset();
            break;
#endif
         case LAUNCH_CATEGORY_SCENE1:
            launch_category = LAUNCH_CATEGORY_SCENE2;
            texpath = std::string(PICS_HOME) + "/scene2.png";
            context_reset();
            break;
         case LAUNCH_CATEGORY_SCENE2:
            launch_category = LAUNCH_CATEGORY_MODEL1;
            texpath = std::string(PICS_HOME) + "/model1.png";
            context_reset();
            break;
         case LAUNCH_CATEGORY_MODEL1:
            launch_category = LAUNCH_CATEGORY_MODEL2;
            texpath = std::string(PICS_HOME) + "/model2.png";
            context_reset();
            break;
         default:
            launch_category = LAUNCH_CATEGORY_GAME;
            texpath = std::string(PICS_HOME) + "/tombraider.png";
            context_reset();
      }

      switch (launch_category)
      {
         case LAUNCH_CATEGORY_GAME:
            {
               struct retro_message msg = {
                  "Category: Games",
                  180 };
               environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, (void*)&msg);
            }
            break;
#if !defined(ANDROID) && !defined(IOS)
         case LAUNCH_CATEGORY_MOVIE:
            {
               struct retro_message msg = {
                  "Category: Movies",
                  180 };
               environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, (void*)&msg);
            }
            break;
#endif
         case LAUNCH_CATEGORY_SCENE1:
            {
               struct retro_message msg = {
                  "Category: Scene 1",
                  180 };
               environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, (void*)&msg);
            }
            break;
         case LAUNCH_CATEGORY_SCENE2:
            {
               struct retro_message msg = {
                  "Category: Scene 2",
                  180 };
               environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, (void*)&msg);
            }
            break;
         case LAUNCH_CATEGORY_MODEL1:
            {
               struct retro_message msg = {
                  "Category: Cars",
                  180 };
               environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, (void*)&msg);
            }
            break;
         case LAUNCH_CATEGORY_MODEL2:
            {
               struct retro_message msg = {
                  "Category: Models",
                  180 };
               environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, (void*)&msg);
            }
            break;
      }
   }
   else if (select_timeout != 0)
      select_timeout--;

   check_collision_cube();

   return look_dir;
}


static bool first_init = true;

static void update_variables(void)
{
   struct retro_variable var;

   var.key = "resolution";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      char *pch;
      char str[100];
      snprintf(str, sizeof(str), "%s", var.value);
      
      pch = strtok(str, "x");
      if (pch)
         width = strtoul(pch, NULL, 0);
      pch = strtok(NULL, "x");
      if (pch)
         height = strtoul(pch, NULL, 0);

      fprintf(stderr, "Got size: %u x %u.\n", width, height);
   }
   
   var.key = "cube_size";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      cube_size = atoi(var.value);
      update = true;

      if (!first_init)
         context_reset();
   }

   var.key = "cube_stride";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      cube_stride = atof(var.value);
      update = true;

      if (!first_init)
         context_reset();
   }

   var.key = "launch_category";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      if (strcmp(var.value, "games") == 0)
      {
         launch_category = LAUNCH_CATEGORY_GAME;
         texpath = std::string(PICS_HOME) + "/tombraider.png";
      }
#if !defined(ANDROID) && !defined(IOS)
      else if (strcmp(var.value, "movies") == 0)
      {
         launch_category = LAUNCH_CATEGORY_MOVIE;
         texpath = std::string(PICS_HOME) + "/lionking.png";
      }
#endif
      else if (strcmp(var.value, "scene1") == 0)
      {
         launch_category = LAUNCH_CATEGORY_SCENE1;
         texpath = std::string(PICS_HOME) + "/scene1.png";
      }
      else if (strcmp(var.value, "scene2") == 0)
      {
         launch_category = LAUNCH_CATEGORY_SCENE2;
         texpath = std::string(PICS_HOME) + "/scene2.png";
      }
      else if (strcmp(var.value, "model1") == 0)
      {
         launch_category = LAUNCH_CATEGORY_MODEL1;
         texpath = std::string(PICS_HOME) + "/model1.png";
      }
      else if (strcmp(var.value, "model2") == 0)
      {
         launch_category = LAUNCH_CATEGORY_MODEL2;
         texpath = std::string(PICS_HOME) + "/model2.png";
      }
      update = true;

      if (!first_init)
         context_reset();
   }

}

void retro_run(void)
{
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables();

   vec3 look_dir = check_input();

   SYM(glBindFramebuffer)(GL_FRAMEBUFFER, hw_render.get_current_framebuffer());
   SYM(glClearColor)(0.1, 0.1, 0.1, 1.0);
   SYM(glViewport)(0, 0, width, height);
   SYM(glClear)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   SYM(glUseProgram)(prog);

   SYM(glBindBuffer)(GL_ARRAY_BUFFER, vbo);
   int vloc = SYM(glGetAttribLocation)(prog, "aVertex");
   SYM(glVertexAttribPointer)(vloc, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, vert)));
   SYM(glEnableVertexAttribArray)(vloc);
   int nloc = SYM(glGetAttribLocation)(prog, "aNormal");
   SYM(glVertexAttribPointer)(nloc, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, normal)));
   SYM(glEnableVertexAttribArray)(nloc);
   int tcloc = SYM(glGetAttribLocation)(prog, "aTexCoord");
   SYM(glVertexAttribPointer)(tcloc, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, tex)));
   SYM(glEnableVertexAttribArray)(tcloc);

   SYM(glEnable)(GL_DEPTH_TEST);
   SYM(glEnable)(GL_CULL_FACE);

   int tloc = SYM(glGetUniformLocation)(prog, "uTexture");
   SYM(glUniform1i)(tloc, 0);
   SYM(glActiveTexture)(GL_TEXTURE0);
   SYM(glBindTexture)(GL_TEXTURE_2D, tex);

   int lloc = SYM(glGetUniformLocation)(prog, "light_pos");
   vec3 light_pos(0, 150, 15);
   SYM(glUniform3fv)(lloc, 1, &light_pos[0]);

   vec4 ambient_light(0.2, 0.2, 0.2, 1.0);
   lloc = SYM(glGetUniformLocation)(prog, "ambient_light");
   SYM(glUniform4fv)(lloc, 1, &ambient_light[0]);

   int vploc = SYM(glGetUniformLocation)(prog, "uVP");
   mat4 view = lookAt(player_pos, player_pos + look_dir, vec3(0, 1, 0));
   mat4 proj = scale(mat4(1.0), vec3(1, -1, 1)) * perspective(45.0f, 640.0f / 480.0f, 5.0f, 500.0f);
   mat4 vp = proj * view;
   SYM(glUniformMatrix4fv)(vploc, 1, GL_FALSE, &vp[0][0]);

   int modelloc = SYM(glGetUniformLocation)(prog, "uM");
   mat4 model = mat4(1.0);
   SYM(glUniformMatrix4fv)(modelloc, 1, GL_FALSE, &model[0][0]);

   if (update)
   {
      update = false;
      SYM(glBindBuffer)(GL_ARRAY_BUFFER, vbo);

      std::vector<Cube> cubes;
      cubes.resize(cube_size * cube_size * cube_size);

      for (unsigned x = 0; x < cube_size; x++)
      {
         for (unsigned y = 0; y < cube_size; y++)
         {
            for (unsigned z = 0; z < cube_size; z++)
            {
               Cube &cube = cubes[((cube_size * cube_size * z) + (cube_size * y) + x)];

               float off_x = cube_stride * ((float)x - cube_size / 2);
               float off_y = cube_stride * ((float)y - cube_size / 2);
               float off_z = -100.0f + cube_stride * ((float)z - cube_size / 2);

               for (unsigned v = 0; v < 36; v++)
               {
                  cube.vertices[v] = vertex_data[indices[v]];
                  cube.vertices[v].vert[0] += off_x;
                  cube.vertices[v].vert[1] += off_y;
                  cube.vertices[v].vert[2] += off_z;
               }
            }
         }
      }
      SYM(glBufferData)(GL_ARRAY_BUFFER, cube_size * cube_size * cube_size * sizeof(Cube),
            &cubes[0], GL_STATIC_DRAW);
      SYM(glBindBuffer)(GL_ARRAY_BUFFER, 0);
   }

   SYM(glDrawArrays)(GL_TRIANGLES, 0, 36 * cube_size * cube_size * cube_size);

   SYM(glUseProgram)(0);
   SYM(glBindBuffer)(GL_ARRAY_BUFFER, 0);
   SYM(glDisableVertexAttribArray)(vloc);
   SYM(glDisableVertexAttribArray)(nloc);
   SYM(glDisableVertexAttribArray)(tcloc);
   SYM(glBindTexture)(GL_TEXTURE_2D, 0);

   video_cb(RETRO_HW_FRAME_BUFFER_VALID, width, height, 0);
}


bool retro_load_game(const struct retro_game_info *info)
{
   update_variables();

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      fprintf(stderr, "XRGB8888 is not supported.\n");
      return false;
   }

#ifdef GLES
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES2;
#else
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;
#endif
   hw_render.context_reset = context_reset;
   hw_render.depth = true;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;

   fprintf(stderr, "Loaded game!\n");
   player_pos = vec3(0, 0, 0);
   texpath = info->path;

   first_init = false;

   return true;
}

void retro_unload_game(void)
{}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

void *retro_get_memory_data(unsigned id)
{
   (void)id;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   (void)id;
   return 0;
}

void retro_reset(void)
{}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

