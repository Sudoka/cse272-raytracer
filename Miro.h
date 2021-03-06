#ifndef __MIRO_H__
#define __MIRO_H__

#if defined (ALTERNATIVE) && ! defined (NO_GFX)
#define NO_GFX
#endif


#define W 512
#define H 512

const float MIRO_TMAX = 1e12f;
const float epsilon   = 1e-4f;
const float PI = 3.1415926535897932384626433832795028841972f;
const float DegToRad = PI/180.0f;
const float RadToDeg = 180.0f/PI;
const int TRACE_DEPTH = 8;
const int TRACE_DEPTH_PHOTONS = 8;
const int TRACE_SAMPLES = 10000;
const float PHOTON_MAX_DIST = 1e10;
const float PHOTON_SAMPLES = 500.f;
//const float PHOTON_ALPHA = 0.7f;
const float PHOTON_ALPHA = 0.7f;
const float DOF_APERTURE = .20f;
const float DOF_FOCUS_PLANE = 15.3f;
const float SURFACE_SAMPLES = 1e-4f;
const float INITIAL_RADIUS = 0.25;
//const float DOF_FOCUS_PLANE = 25.23f;


//C++ std lib includes
#include <cstdlib>
#include "OpenGL.h"
#include <cstdio>
#include <iostream>
#include <limits>
#include <cmath>
#include <string>


typedef struct tex_coord2d_s
{
	tex_coord2d_s() : u(0), v(0) {}
	tex_coord2d_s(float _u, float _v) : u(_u), v(_v) {}
	float u, v;
} tex_coord2d_t;

typedef struct tex_coord3d_s
{
	tex_coord3d_s() : u(0), v(0), w(0) {}
	tex_coord3d_s(float _u, float _v, float _w) : u(_u), v(_v), w(_w) {}
	float u, v, w;
} tex_coord3d_t;

class Ray;
class HitInfo;

class Object;
class Sphere;
class Triangle;
class TriangleMesh;
class Instance;

class PointLight;

class Camera;
class Image;
class Scene;
class Material;

extern void ParseFile(FILE* fp);
extern void initOpenGL();
extern Camera* g_camera;
extern Scene* g_scene;
extern Image* g_image;

const float infinity = std::numeric_limits<float>::infinity();

#endif
