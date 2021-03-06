#ifdef PHOTON_MAPPING

#ifndef CSE168_PSCENE_H_INCLUDED
#define CSE168_PSCENE_H_INCLUDED

#include "Miro.h"
#include "Object.h"
#include "PointLight.h"
#include "BVH.h"
#include "Texture.h"
#include "PhotonMap.h"
#include "PointMap.h"

class Camera;
class Image;

struct Path
{
	Vector3 Origin;
	Vector3 Direction;
	double u[TRACE_DEPTH_PHOTONS*2]; 

	Path(const Vector3& inOrigin, const Vector3& inDirection)
		:Origin(inOrigin), Direction(inDirection)
	{
		init_random();
	}

	Path()
	{
		init_random();
	}

	void init_random()
	{
		for (int i = 0; i < TRACE_DEPTH_PHOTONS*2; ++i)
		{
			u[i] = frand();
		}
	}
};

typedef std::vector<Point*> Points;

class Scene
{
public:
	Scene() 
		: m_pointMap(W*H), m_environment(0), m_bgColor(Vector3(0.0f)), m_photonsEmitted(0), m_photonsUniform(0), max_radius(INITIAL_RADIUS)
	{}
	// TODO: need right image dimensions
    void addObject(Object* pObj)        
    { 
        if (pObj->isBounded()) m_objects.push_back(pObj);
        else m_unboundedObjects.push_back(pObj);
		if (pObj->getMaterial()->isRefractive() || pObj->getMaterial()->isReflective()) m_specObjects.push_back(pObj);
    }
    const Objects* objects() const      {return &m_objects;}
    const Objects* specObjects() const      {return &m_specObjects;}

    void addLight(PointLight* pObj)     {m_lights.push_back(pObj);}
    const Lights* lights() const        {return &m_lights;}

	void addPoint(const Vector3& inPosition, const Vector3& inNormal, const Vector3& inDir, const float inBRDF, const float inRadius, const bool inbLight, int x, int y)	
	{
		if (!inbLight)
		{
			Point* hp = m_pointMap.store(inPosition, inNormal, inDir, inRadius, inBRDF, inbLight, x, y);
			if (hp != NULL)
				m_Points.push_back(hp);
		}
		else 
        {
			m_Points.push_back(new Point());
            m_Points.back()->i = y;
            m_Points.back()->j = x;
        }
	}
	//const Points* Points() const	{return &m_Points;}

    void generatePhotonMap();
	void AdaptivePhotonPasses();

    void preCalc();
    void openGL(Camera *cam);

    void raytraceImage(Camera *cam, Image *img);
    bool trace(HitInfo& minHit, const Ray& ray,
               float tMin = 0.0f, float tMax = MIRO_TMAX) const;
	bool traceScene(const Ray& ray, Vector3 contribution, int depth, int x, int y);

	void UpdatePhotonStats();
	void RenderPhotonStats(Vector3 *tempImage, const int width, const int height);
	bool UpdateMeasurementPoints(const Vector3& pos, const Vector3& normal, const Vector3& power);
    int tracePhoton(const Path& path, const Vector3& position, const Vector3& direction, const Vector3& power, int depth);
	long int GetPhotonsEmitted() { return m_photonsEmitted; }

	void setEnvironment(Texture* environment) { m_environment = environment; }
	Vector3 getEnvironmentMap(const Ray & ray);

    void setBgColor(Vector3 color) { m_bgColor = color; }

    void setEnvironmentRotation(float phi, float theta) { m_environmentRotation.x = phi; m_environmentRotation.y = theta; }

protected:
    VectorR2 m_environmentRotation;
    Objects m_objects;
    Objects m_specObjects;
    Objects m_unboundedObjects;
	Point_map m_pointMap;
    BVH m_bvh;
    Lights m_lights;
	Points m_Points;
    Texture * m_environment; //Environment map
    Vector3 m_bgColor;       //Background color (for when environment map is not available)

    static const int MaxLights = 10;

	long int m_photonsEmitted;
	long int m_photonsUniform;

	float max_radius;
};

extern Scene * g_scene;

#endif // CSE168_SCENE_H_INCLUDED
#endif
