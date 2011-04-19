#include "Utility.h"
#include <cmath>
#include <iostream>
#include "Miro.h"
#include "Scene.h"
#include "Camera.h"
#include "Image.h"
#include "Console.h"
#include "Sphere.h"
#include "DirectionalAreaLight.h"

#ifdef STATS
#include "Stats.h"
#endif

#ifdef OPENMP
#include <omp.h>
#endif 

#ifdef DEBUG_PHOTONS
    #ifdef OPENMP
        #define PHOTON_DEBUG(s) \
        _Pragma("omp critical") \
        cout << s << endl;
    #else
        #define PHOTON_DEBUG(s) cout << s << endl;
    #endif
#else
    #define PHOTON_DEBUG(s) 
#endif

using namespace std;

Scene * g_scene = 0;

void
Scene::openGL(Camera *cam)
{
#ifndef NO_GFX
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    cam->drawGL();

    // draw objects
    for (size_t i = 0; i < m_objects.size(); ++i)
        m_objects[i]->renderGL();

    glutSwapBuffers();
#endif
}

void
Scene::preCalc()
{
    debug("Precalcing objects\n");
    double t1 = -getTime();
    Objects::iterator it;
    for (it = m_objects.begin(); it != m_objects.end(); it++)
    {
        Object* pObject = *it;
        pObject->preCalc();
    }
    Lights::iterator lit;
    for (lit = m_lights.begin(); lit != m_lights.end(); lit++)
    {
        PointLight* pLight = *lit;
        pLight->preCalc();
    }
    t1 += getTime();
    debug("Time spent preprocessing objects and lights: %lf\n", t1);
    
    debug("Building BVH...\n");
    t1 = -getTime();
    m_bvh.build(&m_objects);
    t1 += getTime();
    debug("Done building BVH. Time spent: %lf\n", t1);

    debug("Generating photon map... Number of photons: %d\n", PhotonsPerLightSource);
    t1 = -getTime();

#ifdef PHOTON_MAPPING
    //Generate photon map
    tracePhotons();
	traceCausticPhotons();
#endif

	t1 += getTime();
    debug("Done generating photon map. Time spent: %f\n", t1);

}

inline float tonemapValue(float value, float maxIntensity)
{
    return sigmoid(20*value-2.5);
    //return std::min(pow(value / maxIntensity, 0.35f)*1.1f, 1.0f);
}

void
Scene::raytraceImage(Camera *cam, Image *img)
{
	int depth = TRACE_DEPTH;
    float minIntensity = infinity, maxIntensity = -infinity;


    printf("Rendering Progress: %.3f%%\r", 0.0f);
    fflush(stdout);

    //For tone mapping. The Image class stores the pixels internally as 1 byte integers. We want to store the actual values first.
    int width = img->width(), height = img->height();
    Vector3 *tempImage = new Vector3[height*width];

    double t1 = -getTime();


    // loop over all pixels in the image
    #ifdef OPENMP
    #pragma omp parallel for schedule(dynamic, 2)
    #endif
    for (int i = 0; i < height; ++i)
    {
        float localMaxIntensity = -infinity,
              localMinIntensity = infinity;

        for (int j = 0; j < width; ++j)
        {
            Ray ray;
            Vector3 tempShadeResult;
            Vector3 shadeResult(0.f);

			#if defined (PATH_TRACING) || defined(DOF)
			for (int k = 0; k < TRACE_SAMPLES; ++k)
			{
                ray = cam->eyeRay(j, i, width, height, false);
				if (traceScene(ray, tempShadeResult, depth))
				{
					shadeResult += tempShadeResult;
				}
				#ifdef STATS
				Stats::Primary_Rays++;
				#endif
			}
			shadeResult /= TRACE_SAMPLES; 
            tempImage[i*width+j] = shadeResult;
			#else
            ray = cam->eyeRay(j, i, width, height, false);
			if (traceScene(ray, shadeResult, depth))
			{
                tempImage[i*width+j] = shadeResult;
			}

			#ifdef STATS
			Stats::Primary_Rays++;
			#endif
			#endif // PATH_TRACING
            for (int k = 0; k < 3; k++)
            {
                if (shadeResult[k] > localMaxIntensity)
                    localMaxIntensity = shadeResult[k];
                if (shadeResult[k] < localMinIntensity)
                    localMinIntensity = shadeResult[k];
            }
            #ifdef OPENMP
            #pragma omp critical
			#endif
            {
                if (localMinIntensity < minIntensity) minIntensity = localMinIntensity;
                if (localMaxIntensity > maxIntensity) maxIntensity = localMaxIntensity;
            }

        }
        #ifdef OPENMP
        if (omp_get_thread_num() == 0)
        #endif
        {
            printf("Rendering Progress: %.3f%%\r", i/float(img->height())*100.0f);
            fflush(stdout);
        }
    }
    debug("Performing tone mapping...");
    t1 += getTime();

    #ifdef OPENMP
    #pragma omp parallel for
    #endif
    for (int i = 0; i < height; ++i)
    {
        for (int j = 0; j < width; ++j)
        {
            Vector3 finalColor = tempImage[i*width+j];

            #pragma unroll(3)
            for (int k = 0; k < 3; k++)
            {
                if (finalColor[k] != finalColor[k])
                    finalColor[k] = maxIntensity;
                
                finalColor[k] = tonemapValue(finalColor[k], maxIntensity);
            }
            img->setPixel(j, i, finalColor);
        }
        #ifndef NO_GFX //If not rendering graphics to screen, don't draw scan lines (it will segfault in multithreading mode)
        img->drawScanline(i);
        #endif
    }

    printf("Rendering Progress: 100.000%%\n");
    debug("Done raytracing!\n");
    printf("Time spent raytracing image: %lf seconds.\n", t1);

#ifdef STATS
	Stats tracerStats;
	tracerStats.PrintStats();
#endif
}

bool
Scene::trace(HitInfo& minHit, const Ray& ray, float tMin, float tMax) const
{
    bool result = m_bvh.intersect(minHit, ray, tMin, tMax);
    
    //Trace the unbounded objects (like planes)
    for (int i = 0; i < m_unboundedObjects.size(); i++)
    {
        HitInfo tempMinHit;
        bool ubresult = m_unboundedObjects[i]->intersect(tempMinHit, ray, tMin, tMax);
        if (ubresult && (!result || tempMinHit.t < minHit.t))
        {
            result = true;
            minHit = tempMinHit;
            minHit.object = m_unboundedObjects[i];
        }
    }

    if (result)
    {
        //Bump mapping
        float delta = 0.0001;
//        printf("object %p, material from %p\n", minHit.object, minHit.material);

        if (minHit.material->GetLookupCoordinates() == UV)
        {
            //Take a few samples to calculate the derivative
            tex_coord2d_t center = minHit.object->toUVCoordinates(minHit.P);
            float u = center.u, v = center.v;
            float u1 = minHit.material->bumpHeight2D(tex_coord2d_t(u-delta, v)), 
                  u2 = minHit.material->bumpHeight2D(tex_coord2d_t(u+delta, v)),
                  v1 = minHit.material->bumpHeight2D(tex_coord2d_t(u, v-delta)),
                  v2 = minHit.material->bumpHeight2D(tex_coord2d_t(u, v+delta));
                  
            //Approximate derivatives using central finite differences
            float dx = (u2-u1)/(2*delta),
                  dy = (v2-v1)/(2*delta);
            
            //Find two tangents
            float n[3] = { minHit.N.x, minHit.N.y, minHit.N.z };

            int m = 0;
            if (n[1] > n[0]) m = 1;
            if (n[2] > n[m]) m = 2;
            Vector3 randomVec(m == 2 ? -n[2] : 0, m == 0 ? -n[0] : 0, m == 1 ? -n[1] : 0);
           
            Vector3 t1 = cross(minHit.N, randomVec);
            minHit.N += dx*(cross(minHit.N, t1))-dy*(cross(minHit.N, cross(minHit.N, t1)));
            minHit.N.normalize();
        }   
        //Todo: implement for 3D
        //bumpHeight = minHit.material->bumpHeight3D(tex_coord3d_t(minHit.P.x, minHit.P.y, minHit.P.z));
    }
    return result;
}

bool Scene::traceScene(const Ray& ray, Vector3& shadeResult, int depth)
{
    HitInfo hitInfo;
	shadeResult = Vector3(0.f);
    bool hit = false;
    
    if (depth >= 0)
    {
		// AL: shouldn't decrementing depth be independent if there was a trace hit?
		if (trace(hitInfo, ray))
		{
            hit = true;

			--depth;

			shadeResult = hitInfo.material->shade(ray, hitInfo, *this);

			//if diffuse material, send trace with RandomRay generate by Monte Carlo
			if (hitInfo.material->isDiffuse())
			{
#ifdef PATH_TRACING

				Vector3 diffuseResult;
				Ray diffuseRay = ray.random(hitInfo);

				if (traceScene(diffuseRay, diffuseResult, depth))
				{
					shadeResult += (hitInfo.material->getDiffuse() * diffuseResult);
				}
#endif

#ifdef PHOTON_MAPPING

				float pos[3] = {hitInfo.P.x, hitInfo.P.y, hitInfo.P.z};
				float normal[3] = {hitInfo.N.x, hitInfo.N.y, hitInfo.N.z};
				float irradiance[3] = {0,0,0};
				float caustic[3] = {0,0,0};
            
				m_photonMap.irradiance_estimate(irradiance, pos, normal, PHOTON_MAX_DIST, PHOTON_SAMPLES);
				m_causticMap.irradiance_estimate(caustic, pos, normal, PHOTON_MAX_DIST, PHOTON_SAMPLES);

                //irradiance_estimate does the dividing by PI and all that
				shadeResult += Vector3(irradiance[0]+caustic[0], irradiance[1]+caustic[1], irradiance[2]+caustic[2]);
#endif
			}
			
			//if reflective material, send trace with ReflectRay
			if (hitInfo.material->isReflective())
			{
				Vector3 reflectResult;
				Ray reflectRay = ray.reflect(hitInfo);
				if (traceScene(reflectRay, reflectResult, depth))
				{
					shadeResult += hitInfo.material->getReflection() * reflectResult;
				}
			}

			//if refractive material, send trace with RefractRay
			if (hitInfo.material->isRefractive())
			{
			    float Rs = ray.getReflectionCoefficient(hitInfo); //Coefficient from fresnel

		        if (Rs > 0.01)
		        {
					//Send a reflective ray (Fresnel reflection)
					Vector3 reflectResult;
					Ray reflectRay = ray.reflect(hitInfo);
		            if (traceScene(reflectRay, reflectResult, depth))
			        {
				        shadeResult += hitInfo.material->getRefraction() * reflectResult * Rs;
			        }
		        }
			    
				Vector3 refractResult;
				Ray	refractRay = ray.refract(hitInfo);
				if (traceScene(refractRay, refractResult, depth))
				{
					shadeResult += hitInfo.material->getRefraction() * refractResult * (1.f-Rs);
				}
			}
		}
		else
		{
            shadeResult = getEnvironmentMap(ray);
            hit = true;
		}
	}
    
    return hit;
}


//Shoot out all photons and trace them
void Scene::tracePhotons()
{
    if (PhotonsPerLightSource == 0) 
    {
        m_photonMap.balance();
        return;
    }
    
    printf("Photon Map Progress: %.3f%%\r", 0.0f);
    int totalPhotons = 0; //Total photons emitted
    int photonsAdded = 0; //Photons added to the scene
    
    for (int l = 0; l < m_lights.size(); l++)
    {
        PointLight *light = m_lights[l];
        
        //Temporary hack
        if (dynamic_cast<DirectionalAreaLight*>(light) == 0) continue;
        
        while (photonsAdded < PhotonsPerLightSource)
        {
            #ifdef OPENMP
            #pragma omp parallel for
            #endif
            for (int i = 0; i < 10000; i++)
            {
                if (photonsAdded < PhotonsPerLightSource)
                {
                    //Create a new photon
                    Vector3 power = light->color() * light->wattage(); 
                    DirectionalAreaLight *dl = dynamic_cast<DirectionalAreaLight*>(light);
                    if (dl != 0)
                    {
                         power *= PI * dl->getRadius() * dl->getRadius();
                    }
                    Vector3 dir = light->samplePhotonDirection();
                    Vector3 pos = light->samplePhotonOrigin();
                    int photons = tracePhoton(pos, dir, power, 0);
                    
                    #pragma omp critical
                    {
                        photonsAdded += photons;
                        totalPhotons ++;
                    }
                    
                    if (i % 1000 == 0)
                        printf("Photon Map Progress: %.3f%%\r", 100.0f*((float)photonsAdded+PhotonsPerLightSource*l)/(float)(PhotonsPerLightSource*m_lights.size()));
                }
            }
        }
    }
    m_photonMap.scale_photon_power(1.0f/(float)totalPhotons);
    printf("Photon Map Progress: %.3f%%\n", 100.0f);
    m_photonMap.balance();
    #ifdef VISUALIZE_PHOTON_MAP
    debug("Rebuilding BVH for visualization. Number of objects: %d\n", m_objects.size());
    m_bvh.build(&m_objects);

    #endif
}

//Shoot out all caustic photons and trace them -- brute force method
void Scene::traceCausticPhotons()
{
    if (CausticPhotonsPerLightSource == 0) 
    {
        m_causticMap.balance();
        return;
    }
    
    printf("Caustic Map Progress: %.3f%%\r", 0.0f);
    int totalPhotons = 0; //Total photons emitted
    int photonsAdded = 0; //Photons added to the scene
    
    for (int l = 0; l < m_lights.size(); l++)
    {
        PointLight *light = m_lights[l];
        
        //Temporary hack
        if (dynamic_cast<DirectionalAreaLight*>(light) == 0) continue;
        
        while (photonsAdded < CausticPhotonsPerLightSource)
        {
            #ifdef OPENMP
            #pragma omp parallel for
            #endif
            for (int i = 0; i < 10000; i++)
            {
                if (photonsAdded < CausticPhotonsPerLightSource)
                {
                    //Create a new photon
                    Vector3 power = light->color() * light->wattage(); 
                    DirectionalAreaLight *dl = dynamic_cast<DirectionalAreaLight*>(light);
                    if (dl != 0)
                    {
                         power *= PI * dl->getRadius() * dl->getRadius() / 10.f;
                    }
                    Vector3 dir = light->samplePhotonDirection();
                    Vector3 pos = light->samplePhotonOrigin();
                    int photons = tracePhoton(pos, dir, power, 0, true);
                    
                    #pragma omp critical
                    {
                        photonsAdded += photons;
                        totalPhotons ++;
                    }
                    
                    if (i % 1000 == 0)
                        printf("Caustic Map Progress: %.3f%%\r", 100.0f*((float)photonsAdded+CausticPhotonsPerLightSource*l)/(float)(CausticPhotonsPerLightSource*m_lights.size()));
                }
            }
        }
    }
    m_causticMap.scale_photon_power(1.0f/(float)totalPhotons);
    printf("Caustic Map Progress: %.3f%%\n", 100.0f);
    m_causticMap.balance();
    #ifdef VISUALIZE_PHOTON_MAP
    debug("Rebuilding BVH for visualization. Number of objects: %d\n", m_objects.size());
    m_bvh.build(&m_objects);

    #endif
}

void Scene::ProgressivePhotonPass()
{
	traceProgressivePhotons();
	
	//iterate throuhg all of the scene hitpoints
	for (int n = 0; n < m_hitpoints.size(); ++n)
	{
		HitPoint *hp = m_hitpoints[n];

		float pos[3] = {hp->position.x, hp->position.y, hp->position.z};
		float normal[3] = {hp->normal.x, hp->normal.y, hp->normal.z};
		float irradiance[3] = {0,0,0};
    
		int M = m_photonMap.irradiance_estimate(irradiance, pos, normal, hp->radius, PHOTON_SAMPLES, false);
		
		//only adding a ratio of the newly added photons
		float delta = (hp->accPhotons + PHOTON_ALPHA * M)/(hp->accPhotons + M);
		hp->radius *= sqrt(delta);
		hp->accPhotons += (int)(PHOTON_ALPHA * M);
		
		//not sure about this flux acc, or about calculating the irradiance
		hp->accFlux.x = ( hp->accFlux.x + irradiance[0] * PHOTON_ALPHA ) * delta;
		hp->accFlux.y = ( hp->accFlux.y + irradiance[1] * PHOTON_ALPHA ) * delta;
		hp->accFlux.z = ( hp->accFlux.z + irradiance[2] * PHOTON_ALPHA ) * delta;

		printf("radius: %f  accPhotons: %d irradiance x: %f ", hp->radius, hp->accPhotons, hp->accFlux.x / PI / pow(hp->radius, 2) / m_photonsEmitted);
	}

	m_photonMap.empty();
} 

//Shoot out all photons and trace them
void Scene::traceProgressivePhotons()
{
    if (PhotonsPerLightSource == 0) 
    {
        m_photonMap.balance();
        return;
    }
    
    int totalPhotons = 0; //Total photons emitted
    int photonsAdded = 0; //Photons added to the scene
    
    for (int l = 0; l < m_lights.size(); l++)
    {
        PointLight *light = m_lights[l];
        
        while (photonsAdded < PhotonsPerLightSource)
        {
            #ifdef OPENMP
            #pragma omp parallel for
            #endif
            for (int i = 0; i < 10000; i++)
            {
                if (photonsAdded < PhotonsPerLightSource)
                {
                    //Create a new photon
                    Vector3 power = light->color() * light->wattage(); 
                    Vector3 dir = light->samplePhotonDirection();
                    Vector3 pos = light->samplePhotonOrigin();
                    int photons = tracePhoton(pos, dir, power, 0);
                    
                    #pragma omp critical
                    {
                        photonsAdded += photons;
                        totalPhotons ++;
                    }
                }
            }
        }

		m_photonsEmitted += PhotonsPerLightSource;
    }
	// do not scale photons in progressive photon mapping
    // m_photonMap.scale_photon_power(1.0f/(float)totalPhotons);
    m_photonMap.balance();
    #ifdef VISUALIZE_PHOTON_MAP
    debug("Rebuilding BVH for visualization. Number of objects: %d\n", m_objects.size());
    m_bvh.build(&m_objects);

    #endif
}

//Trace a single photon through the scene
int Scene::tracePhoton(const Vector3& position, const Vector3& direction, const Vector3& power, int depth, bool bCausticRay)
{
    PHOTON_DEBUG(endl << "tracePhoton(): pos " << position << ", dir " << direction << ", pwr " << power << ", depth " << depth);
    if (depth > TRACE_DEPTH_PHOTONS) return 0;

    //Create a ray to trace the scene with
    Ray ray(position+epsilon*direction, direction);
    HitInfo hit;

	++depth;
    if (trace(hit, ray, 0.0f, MIRO_TMAX))
    {
        //Do "russian roulette but not really"
        //Choose a random kind of ray - transmission, diffuse or reflective. Or absorb.
        //[ --diffuse-- | --specular (refl.)-- | --transmission-- | --absorb-- ]
        float prob[3], rnd = frand();
        Vector3 diffuseColor;
        if (hit.material->GetLookupCoordinates() == UV)
            diffuseColor = hit.material->diffuse2D(hit.object->toUVCoordinates(hit.P));
        else
            diffuseColor = hit.material->diffuse3D(tex_coord3d_t(hit.P.x, hit.P.y, hit.P.z));

        prob[0] = diffuseColor.average();
        prob[1] = prob[0] + hit.material->getReflection().average();
        prob[2] = prob[1] + hit.material->getRefraction().average();

        PHOTON_DEBUG("rnd = " << rnd << " F[reflect] = " << prob[0] << " F[refract] = " << prob[1] << "F[absorb] = " << prob[2]);

        if (rnd > prob[2])
        {
            //Absorb. Do nothing.
            PHOTON_DEBUG("Absorbed.");
            return 0;
        }

        if (rnd < prob[0])
        {
            PHOTON_DEBUG("Diffuse contribution.");
            int nPhotons = 0;
            //Diffuse.
            //only store indirect lighting -- but store direct lighting for progressive mapping
            //if (depth > 1)
            {
                float pos[3] = {hit.P.x, hit.P.y, hit.P.z}, dir[3] = {direction.x, direction.y, direction.z}, pwr[3] = {power.x, power.y, power.z};
#               ifdef OPENMP
#               pragma omp critical
#               endif
                {

                    PHOTON_DEBUG("Storing photon at " << hit.P << ". Surface normal " << hit.N << " Is caustic photon: " << bCausticRay);
                    if (bCausticRay)
						m_causticMap.store(pwr, pos, dir);
					else
						m_photonMap.store(pwr, pos, dir);

					nPhotons++;

#                   ifdef VISUALIZE_PHOTON_MAP
                    Sphere* sp = new Sphere;
                    sp->setCenter(hit.P); sp->setRadius(0.02f);
                    Vector3 ref = power; //Use the normalized power as the reflectance for visualization.
                    ref.normalize(); sp->setMaterial(new Phong(ref)); addObject(sp);
#                   endif
                }
            }
            /*else
            {
			    //Caustic Rays only send rays from specular surfaces
			    if (bCausticRay)
				    return 0;
		    }*/

#ifdef STATS
			Stats::Photon_Bounces++;
#endif
            //Shoot out a new diffuse photon
            Ray r = ray.diffuse(hit);
            HitInfo diffHit;
            PHOTON_DEBUG("Tracing diffuse photon");
            return nPhotons + tracePhoton(r.o, r.d, diffuseColor*power/prob[0], depth, bCausticRay);
        }
        else if (rnd < prob[1])
        {
			//only caustics should count this first bounce
			if (!bCausticRay && depth == 1)
				return 0;

#ifdef STATS
			Stats::Photon_Bounces++;
#endif
            //Reflect.
            Ray refl = ray.reflect(hit);
            PHOTON_DEBUG("Tracing reflected photon");
            return tracePhoton(hit.P, refl.d, power, depth, bCausticRay);
        }
        else if (rnd < prob[2])
        {
			//only caustics should count this first bounce
			if (!bCausticRay && depth == 1)
				return 0;

#ifdef STATS
			Stats::Photon_Bounces++;
#endif
            //Transmit (refract)
            float Rs = ray.getReflectionCoefficient(hit); //Coefficient from fresnel
			
			//Fresnel reflection
			if (frand() < Rs)
			{
                Ray refl = ray.reflect(hit);
                PHOTON_DEBUG("Tracing reflected photon (Fresnel reflection)");
                return tracePhoton(hit.P, refl.d, power, depth, bCausticRay);
			}
			else
			{
                Ray refr = ray.refract(hit);
                PHOTON_DEBUG("Tracing refracted photon");
                return tracePhoton(hit.P, refr.d, power, depth, bCausticRay);
            }
        }
    }
#   ifdef DEBUG_PHOTONS
    else { PHOTON_DEBUG("Missed scene."); }
#   endif
	return 0;
}

Vector3
Scene::getEnvironmentMap(const Ray & ray)
{
	Vector3 envResult;
	//Environment mapping here
	if (m_environment != 0)
	{
		tex_coord2d_t coords;
		float phi = atan2(ray.d.x, ray.d.z) + m_environmentRotation.x + PI; //Phi is in [0, 2PI]
		float theta = asin(ray.d.y) + m_environmentRotation.y;    //
        if (theta > PI/2.0f) 
        {
            phi += PI;
            theta -= 2.0f*(theta-PI/2.0f);
        }
		if (phi > 2.0f*PI) phi -= (2.0f*PI); // Force phi to be in [0, 2PI]
	
		//Calculate texture coordinates for where the ray hits the "sphere"
		coords.u = phi / (2.0f * PI);
		coords.v = theta / PI + 0.5;
		//And just look up the shading value in the texture.
        if (!ray.isDiffuse)
    		envResult = m_environment->lookup2D(coords);
        else
            envResult = m_environment->lowresLookup2D(coords);
	}
	else
	{
		envResult = m_bgColor; 
	}
	return envResult;
}
