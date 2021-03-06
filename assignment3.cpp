#include "assignment3.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <math.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include "Miro.h"
#include "includes.h"
#include "Emissive.h"
#include "Utility.h"

SquareLight* g_l;

using namespace std;
using std::min;
using std::max;

void BuildSquare(const Vector3& min, const Vector3& max, const Vector3& normal, const Material* mat)
{
    TriangleMesh * square1 = new TriangleMesh;
    square1->createSingleTriangle();
    if (min.z != max.z)
    {
        square1->setV1(Vector3( min.x, min.y, min.z));
        square1->setV2(Vector3( max.x, max.y, max.z));
        square1->setV3(Vector3( max.x, max.y, min.z));
    }
    else
    {
        square1->setV1(Vector3( min.x, min.y, min.z));
        square1->setV2(Vector3( max.x, max.y, max.z));
        square1->setV3(Vector3( max.x, min.y, min.z));
    }
    square1->setN1(normal);
    square1->setN2(normal);
    square1->setN3(normal);
    
    Triangle* t = new Triangle;
    t->setIndex(0);
    t->setMesh(square1);
    t->setMaterial(mat);
    g_scene->addObject(t);

	TriangleMesh * square2 = new TriangleMesh;
    square2->createSingleTriangle();
    if (min.z != max.z)
    {
        square2->setV1(Vector3( min.x, min.y, min.z));
        square2->setV2(Vector3( min.x, min.y, max.z));
        square2->setV3(Vector3( max.x, max.y, max.z));
    }
    else
    {
        square2->setV1(Vector3( min.x, min.y, min.z));
        square2->setV2(Vector3( min.x, max.y, max.z));
        square2->setV3(Vector3( max.x, max.y, max.z));
    }
    square2->setN1(normal);
    square2->setN2(normal);
    square2->setN3(normal);

    Triangle* t2 = new Triangle;
    t2->setIndex(0);
    t2->setMesh(square2);
    t2->setMaterial(mat); 
    g_scene->addObject(t2);
}

void 
makeTask3Scene()
{
    g_image->resize(W, H);

    // set up the camera
    g_camera->setBGColor(Vector3(0.0f, 0.0f, 0.0f));
    g_camera->setEye(Vector3(0, 0, -2));
    g_camera->setLookAt(Vector3(0, 0, 0));
    g_camera->setUp(Vector3(0, 1, 0));
    g_camera->setFOV(90);
    g_scene->setBgColor(Vector3(0));

    //real squarelight
    SquareLight *l = new SquareLight;
    g_l = l;
    l->setDimensions(20,2);
    l->setPosition(Vector3(0,2,0));
    l->setNormal(Vector3(0,-1,0));
    l->setUdir(Vector3(1,0,0));
    l->setWattage(1000);
    l->setColor(Vector3(1.f));

    g_scene->addObject(l);
	g_scene->addLight(l);
    
    double e = 0.0005;
    Material* gray = new Phong(Vector3(0.75));
#ifdef PHOTON_MAPPING
    e = 0;
#endif

	// Floor
	BuildSquare(Vector3(-1,-1,-1), Vector3(1,-1,1), Vector3(0,1,0), gray);
	
	// Back Wall
	BuildSquare(Vector3(-1,-1,1), Vector3(1,1,1), Vector3(0,0,-1), gray);

	// Left Wall
	BuildSquare(Vector3(-1-e,-1,-1), Vector3(-1,1,1), Vector3(1,0,0), new Phong(Vector3(0), Vector3(0.75)));
#ifdef HACKER3
    //Hackerpoint: Red wall
	BuildSquare(Vector3(1+e,-1,-1), Vector3(1-e,1,1), Vector3(-1,0,0), new Phong(Vector3(0.75,0,0)));
#else
	// Right Wall
	BuildSquare(Vector3(1+e,-1,-1), Vector3(1-e,1,1), Vector3(-1,0,0), gray);
#endif
//	// Ceiling
	BuildSquare(Vector3(0.05,1,-1), Vector3(1,1,1), Vector3(0,-1,0), gray);
	BuildSquare(Vector3(-0.95,1,-1), Vector3(0,1,1), Vector3(0,-1,0), gray);

    //Hackerpoint: Sphere
#ifdef HACKER2
    Sphere* sp = new Sphere();
    sp->setCenter(Vector3(0,0,0));
    sp->setRadius(0.5);
    sp->setMaterial(new Phong(Vector3(0), Vector3(0), Vector3(1), 1, 1.5));
    g_scene->addObject(sp);
#endif
    
    g_scene->preCalc();
}

sample sampleBidirectionalPath(const path& eyepath, const path& lightpath, int w, int h)
{
    //Debug
    static int nrays = 0;
    nrays++;

    sample out;
    out.value = Vector3(0);
    out.hit = true;

    Ray eye_ray = g_camera->eyeRay((int)(eyepath.u[0]*(double)W), (int)(eyepath.u[1]*(double)H), W, H, false);
    
    Ray light_ray(g_l->getPhotonOrigin(lightpath.u[0], lightpath.u[1]), g_l->samplePhotonDirection(lightpath.u[2], lightpath.u[3]));

    int eye_depth = EYE_PATH_LENGTH;
    int light_depth = LIGHT_PATH_LENGTH;
    int pathpos = 4; //next random number to be used is at index 4 (light path)

    HitInfo hitInfo;
    PointLight* l;
    Vector3 contribution(1./PI); //Current contribution from an eye point

    int light_points = 0;   //Number of hit points for the light path
    int eye_points = 0; //Number of hit points for the eye path
    hit_point lighthits[LIGHT_PATH_LENGTH+1]; //Hit points for light path (there is one hit point at the lightsource itself, so +1)
    hit_point eyehits[EYE_PATH_LENGTH];       //Hit points for the eye path
//    Vector3 flux(g_l->wattage()/PI);
    Vector3 flux(g_l->wattage());

    //First point is at the light source
    lighthits[0].x = light_ray.o;
    lighthits[0].N = g_l->getNormal();
//    lighthits[0].contrib = flux/g_l->area();
//    lighthits[0].contrib = g_l->radiance(Vector3(0), Vector3(0));
    lighthits[0].contrib = flux;
    lighthits[0].theta_out = light_ray.d;
    light_points = 1;
    
    int bounces = 0;

    //Path trace from light source
    while (light_depth > 0)
    {
        if (g_scene->trace(hitInfo, light_ray, 0, MIRO_TMAX))
        {
            //Did we hit a light? If so, bail out
            l = dynamic_cast<PointLight*>(hitInfo.object);
            if (l != NULL)
            {
                light_depth = -1;
            }
            //hit reflective surface => reflect and trace again
            else if (hitInfo.material->isReflective())
            {
                light_ray = light_ray.reflect(hitInfo);
                flux = flux * hitInfo.material->getReflection();
            }
            //Hit a refractive surface?
            else if (hitInfo.material->isRefractive())
            {
                //Push the hit point inside (or outside if on the way out) the refractive object
                hitInfo.P += light_ray.d*epsilon*2.;
                light_ray = light_ray.refract(hitInfo);
                flux = flux * hitInfo.material->getRefraction();
            }
            //Did we hit a diffuse object?
            else
            {
                if (dot(light_ray.d, hitInfo.N) < 0)
                {
                    light_ray = light_ray.diffuse(hitInfo, lightpath.u[pathpos], lightpath.u[pathpos+1]);
                    pathpos += 2;
                    lighthits[light_points].x = hitInfo.P;
                    lighthits[light_points].contrib = flux;
                    lighthits[light_points].N = hitInfo.N;
                    lighthits[light_points].reflectance = hitInfo.material->getDiffuse();
                    lighthits[light_points].theta_out = light_ray.d;
                    light_points++;
                    flux = flux * hitInfo.material->getDiffuse();
                }
                else
                {
                    //Hit on wrong side of the geometry, so terminate ray
                    light_depth = -1;
                }
            }
            light_depth--;
        }
        //Missed the scene
        else 
        {
            light_depth = -1;
        }
    }
    
    pathpos = 2;
    double weight = 1./(double)(EYE_PATH_LENGTH+LIGHT_PATH_LENGTH);
    int pathcount[EYE_PATH_LENGTH+LIGHT_PATH_LENGTH+2]; //Number of paths of each length [index i has the number of paths of length i+1]

    memset(pathcount, 0, sizeof(int)*(EYE_PATH_LENGTH+LIGHT_PATH_LENGTH+2));

    //Path trace from eye
    while (eye_depth > 0)
    {
        if (g_scene->trace(hitInfo, eye_ray, 0, MIRO_TMAX))
        {
            //Did we hit the light, bail out (it's sampled elsewhere)
            l = dynamic_cast<PointLight*>(hitInfo.object);
            if (l != NULL)
            {
                eye_depth = -1;
                //If eye_points == 0  then we have a direct path (or specular path) to the LS
                if (eye_points == 0)
                    out.direct = true;
            }
            //hit reflective surface => reflect and trace again
            else if (hitInfo.material->isReflective())
            {
                eye_ray = eye_ray.reflect(hitInfo);
                contribution = contribution * hitInfo.material->getReflection();
            }
            //Hit a refractive surface?
            else if (hitInfo.material->isRefractive())
            {
                //Push the hit point inside (or outside if on the way out) the refractive object
                hitInfo.P += eye_ray.d*epsilon*2.;
                eye_ray = eye_ray.refract(hitInfo);
                contribution = contribution * hitInfo.material->getRefraction();
            }
            //Did we hit a diffuse object?
            else
            {
                //Next ray is a diffuse reflection ray
                eye_ray = eye_ray.diffuse(hitInfo, eyepath.u[pathpos], eyepath.u[pathpos+1]);

                contribution = contribution * hitInfo.material->getDiffuse();

                eyehits[eye_points].x = hitInfo.P;
                eyehits[eye_points].N = hitInfo.N;
                eyehits[eye_points].contrib = contribution;
                eyehits[eye_points].theta_out = eye_ray.d;
                eye_points++;


                //Count paths that start with eye_points vertices from the eye
                for (int i = 0; i < light_points; i++)
                {
                    pathcount[i+eye_points]++;
                }

                pathpos += 2;
            }
            eye_depth--;
        }
        //Missed the scene
        else 
        {
            eye_depth = -1;
        }
    }


    if (!out.direct && eye_points > 0)
    {
        lighthits[light_points].theta_out = eyehits[eye_points-1].x - lighthits[light_points-1].x;
        lighthits[light_points].theta_out.normalize();

        double pLight[LIGHT_PATH_LENGTH+2],
               pEye[EYE_PATH_LENGTH+1];
        
        //Probability ratios p[i+1]/p[i]
        //First dimension gives the length
//        double r[LIGHT_PATH_LENGTH+EYE_PATH_LENGTH+1][LIGHT_PATH_LENGTH+EYE_PATH_LENGTH+1];

//        for (int l = 1; l <= light_points+eye_points; l++)
//        {
//            //l = path length-1
//            hit_point* prev = &lighthits[0],  //x_i-1
//                     * current = (light_points > 1 ? &lighthits[1] : &eyehits[0]); //x_i
//            r[l][0] = (1./g_l->area()) 
//                 / (abs(pow(dot(prev->N, prev->theta_out), 2) * dot(current->N, prev->theta_out) / (current->x - prev->x).length2())/PI);
//
//            for (int i = 1; i < l-1; i++)
//            {
//                hit_point* next = (light_points > i+1 ? &lighthits[i+1] : &eyehits[i+1-light_points]);
//                r[l][i] = abs(pow(dot(prev->N, prev->theta_out), 2) * dot(current->N, prev->theta_out) * (next->x - current->x).length2())
//                        / abs(pow(dot(current->N, current->theta_out), 2) * dot(next->N, current->theta_out) * (prev->x - current->x).length2());
//
//                prev = current;
//                current = next;
//            }
//            current = (eye_points > 1 ? &eyehits[1] : &lighthits[light_points-1]);
//            hit_point* next = &eyehits[0];
//
//            r[l][l] = abs(pow(dot(current->N, current->theta_out), 2) * dot(next->N, current->theta_out) / (next->x - current->x).length2()) / PI;
////            cout << r[l][l] << " " << (next->x - current->x).length2() <<  endl;
//        }

        double p_sum[LIGHT_PATH_LENGTH+EYE_PATH_LENGTH+1]; //sum of probabilties for each path length
        memset(p_sum, 0, sizeof(double)*(LIGHT_PATH_LENGTH+EYE_PATH_LENGTH+1));
//
        pLight[0] = 1;
        pLight[1] = 1./g_l->area();

        //Compute probabilities for creating the paths that we did
        for (int i = 2; i < light_points+1; i++)
        {
            pLight[i] = pLight[i-1] 
                      * abs(dot(lighthits[i-2].theta_out, lighthits[i-2].N)) / PI
                      * abs(dot(lighthits[i-2].N, lighthits[i-2].theta_out) * dot(lighthits[i-1].N, lighthits[i-2].theta_out))
                      / (lighthits[i-2].x-lighthits[i-1].x).length2(); 
        }

        pEye[0] = 1;
        pEye[1] = 1.;

        for (int j = 2; j < eye_points+1; j++)
        {
            pEye[j] = pEye[j-1] 
                    * abs(dot(eyehits[j-2].theta_out, eyehits[j-2].N)) / PI 
                    * abs(dot(eyehits[j-2].N, eyehits[j-2].theta_out) * dot(eyehits[j-1].N, eyehits[j-2].theta_out))
                    / (eyehits[j-2].x-eyehits[j-1].x).length2();
        }

//        cout << "light points: " << light_points << " Eye points: " << eye_points << endl;
        //Compute the summed probabilities of generating paths of length 2 or more
//        for (int l = 1; l < eye_points+light_points; l++)
//        {
//        p_sum[l] = 0;
        for (int i = 0; i < light_points; i++)
        {
            for (int j = 0; j < eye_points; j++)
            {
//                p_sum[i+j+1] += pLight[i+1]*pEye[j+1];
                p_sum[i+j+1] += pow(pLight[i+1]*pEye[j+1],2);
            }
        }
        

        double weights[LIGHT_PATH_LENGTH+EYE_PATH_LENGTH+5];
        memset(weights, 0, (LIGHT_PATH_LENGTH+EYE_PATH_LENGTH+5)*sizeof(double));

        HitInfo hitTmp;
        out.value = Vector3(0);
        for (int i = 0; i < light_points; i++)
        {
            for (int j = 0; j < eye_points; j++)
            {
                //need to compute sum(p_s/p_i)
                int s = i+1, t = j+1, path_length = i+j+1;
                double p = 1;

                weight = p;

//                for (int k = s-1; k >= 0; k--)
//                {
//                    p *= r[path_length][k]; //p <- p_s/p_k
//                    weight += pow(p, 2);
//                    cout << "p=" << p << endl;
//                }
//
//                p = 1;
//                for (int k = s; k < path_length; k++)
//                {
//                    p /= r[path_length][k]; //p <- p_s/p_k
//                    weight += pow(p, 2);
//                }
//
//                weight = 1./weight;

                Vector3 l = lighthits[i].x-eyehits[j].x;
                double length = l.length();

                //Weight is 1/n where n is the number of paths that has eye_points eye points as prefixes and i light points as suffixes
//                weight = 1./(double)(pathcount[i+j+1]);
//                weight = pLight[i+1]*pEye[j+1]/p_sum[i+j+1];
                weight = pow(pLight[i+1]*pEye[j+1],2)/p_sum[i+j+1];
                weights[i+j+1] += weight;

                l /= length;
                Ray shadow(eyehits[j].x, l);
                if (!g_scene->trace(hitTmp, shadow, 0, length-epsilon))
                {
                    double G = abs(dot(l, eyehits[j].N)) * abs(dot(l, lighthits[i].N)) / (eyehits[j].x-lighthits[i].x).length2();
//                    G = min(G, 100.);
//                    if (G > 100) 
//                        cout << G << endl;
                    //               brdf(eye point)               flux                   incid. angle to eye point   incid.angle to light point
                    Vector3 result = weight * eyehits[j].contrib * lighthits[i].contrib * G / (PI*PI);
                    if (i > 0)
                        result *= lighthits[i].reflectance;

//                    if (result.average() > 1000) cout << result.average() << endl;

                    out.value += result;
//                    cout << result << endl;
                }
            }
        }
//        for (int i = 1; i < eye_points+light_points; i++)
//        {
//            cout << "weight sum path length " << i+1 << ": " << weights[i] << endl;
//        }
//        cout << endl;
    }
    else
    {
//        out.value = contribution*g_l->radiance(Vector3(0),Vector3(0,-1,0));
        out.value = Vector3(0);
    }

    return out;
}

sample samplePath(const path& p, int w, int h)
{
    sample out;
    Ray ray = g_camera->eyeRay((int)(p.u[0]*(double)W), (int)(p.u[1]*(double)H), W, H, false);

    int depth = PATH_LENGTH;

    int pathpos = 2; //next random number to be used is at index 2

    HitInfo hitInfo;
    PointLight* l;
    Vector3 contribution(1./PI,1./PI,1./PI); //Current contribution (decreases if surface we hit has reflectance < 1)

    //Path trace
    while (depth > 0 && !out.hit)
    {
        if (g_scene->trace(hitInfo, ray, 0, MIRO_TMAX))
        {
            //Did we hit the light?
            l = dynamic_cast<PointLight*>(hitInfo.object);
            if (l != NULL)
            {
                out.value = contribution*l->radiance(hitInfo.P, ray.d);
                out.hit = true;
            }
            //hit reflective surface => reflect and trace again
            else if (hitInfo.material->isReflective())
            {
                ray = ray.reflect(hitInfo);
                contribution = contribution * hitInfo.material->getReflection();
            }
            //Hit a refractive surface?
            else if (hitInfo.material->isRefractive())
            {
                //Push the hit point inside (or outside if on the way out) the refractive object
                hitInfo.P += ray.d*epsilon*2.;

			    float Rs = ray.getReflectionCoefficient(hitInfo); //Coefficient from fresnel

                if (frand() < Rs)
                {
					//Send a reflective ray (Fresnel reflection)
					ray = ray.reflect(hitInfo);
                    contribution = contribution * hitInfo.material->getRefraction();
		        }
                else
                {
                    ray = ray.refract(hitInfo);
                    contribution = contribution * hitInfo.material->getRefraction();
                }
            }
            //Did we hit a diffuse object?
            else
            {
                contribution = contribution * hitInfo.material->getDiffuse();
                ray = ray.diffuse(hitInfo, p.u[pathpos], p.u[pathpos+1]);
                pathpos += 2;
            }
            depth--;
        }
        //Missed the scene
        else 
        {
            out.hit = false;
            out.value = Vector3(0);
            break;
        }
    }
    return out;
}

double mutate_value(double s1, double s2)
{
    double dv = s2*exp(-log(s2/s1)*frand());

    if (frand() < 0.5)
        return -dv;
    else
        return dv;
}

void mutate_path(const path &p0, path &p1)
{
    //Mutation magnitudes
//    double dpos = 1, dtheta = .125, dphi = .125;
    double dpos = 1, dtheta = .5, dphi = .5;
    double max_mutation = 1./64.;
    double min_mutation = 1./1024.;

    if (frand() < p_large)
    {
        p1.init_random();
    }
    else
    {
        //Mutate position
        p1.u[0] = p0.u[0] + mutate_value(min_mutation, max_mutation) * dpos;
        if (p1.u[0] >= 1.) p1.u[0] -= 1;
        else if (p1.u[0] < 0) p1.u[0] += 1;

        p1.u[1] = p0.u[1] + mutate_value(min_mutation, max_mutation) * dpos;
        if (p1.u[1] > 1) p1.u[1] -= 1;
        else if (p1.u[1] < 0) p1.u[1] += 1;

        //Mutate directions
        for (int i = 0; i < PATH_LENGTH; i++)
        {
            int j = 2+i*2;
            p1.u[j] = p0.u[j] + mutate_value(min_mutation, max_mutation)*dtheta;
            if (p1.u[j] < 0) p1.u[j] += 1;
            else if (p1.u[j] > 1) p1.u[j] -= 1;

            p1.u[j+1] = p0.u[j+1] + mutate_value(min_mutation, max_mutation)*dtheta;
            if (p1.u[j+1] < 0) p1.u[j+1] += 1;
            else if (p1.u[j+1] > 1) p1.u[j+1] -= 1;
        }

    }
}

void a3task3()
{
    const int Nseeds = 1000000;
    const long Nsamples = 100000000;
    //Record the error after every this many samples
    const int error_interval = 100000;

    Vector3 img[W][H];
    memset(img, 0, W*H*sizeof(Vector3));

    cout << "Metropolis sampling" << endl;
    cout << Nsamples / (W*H) << " samples per pixel." << endl;

#ifdef HACKER2
    const char* version = "sphere";
#elif defined (HACKER3)
    const char* version = "red";
#else
    const char* version = "gray";
#endif

    vector<vector<Vector3> > ptracing_results;

    {
        //Load image from task 1
        ifstream ptracing;
        char filename[100];
        sprintf(filename, "pathtracing_%s.raw\0", version);
        ptracing.open(filename, ios::binary);

        int w_pt, h_pt;
        long double b_pt = 0;
        if (!ptracing.is_open())
        {
            w_pt = W; h_pt = H;
        }
        ptracing.read((char*)&w_pt, 4);
        ptracing.read((char*)&h_pt, 4);
        cout << "Loading path tracing results [width=" << w_pt << ", height " << h_pt << "]" << endl;
        for (int i = 0; i < h_pt; i++)
        {
            ptracing_results.push_back(vector<Vector3>());
            for (int j = 0; j < w_pt; j++)
            {
                Vector3 pix(0);
                if (ptracing.is_open())
                {
                    ptracing.read((char*)&pix.x, sizeof(float));
                    ptracing.read((char*)&pix.y, sizeof(float));
                    ptracing.read((char*)&pix.z, sizeof(float));
                }
                b_pt += pix.average();
                ptracing_results[i].push_back(pix);
            }
        }
        b_pt /= (long double)(w_pt*h_pt);
        cout << "Done reading path tracing results. b = " << b_pt << endl;
    }
    cout << "Generating path seeds..." << endl;

    long double b = 0;

    path p_init;
    p_init.I = 0;

    stringstream msq_out;

    //Generate path seeds
    for (int i = 1; i <= Nseeds; i++)
    {
        path p_tmp;

        p_tmp.init_random();

        sample s = samplePath(p_tmp, W, H);

        if (s.hit)
        {
            long double I = s.value.average();
            b += I*PI;

            //is it a better path?
            if (p_init.I < I)
                path_copy(p_init, p_tmp);
        }
    }

    b /= Nseeds;

    cout << "b=" << b << endl;

    double b_result = 0;
    double msq = 0;
    //Initialize msq
    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
        {
            double res = ptracing_results[y][x].average();
            msq += res*res;
        }
    }
    msq /= (double)(H*W);


    path p0;
    p0.I = 0;
    path_copy(p0, p_init);

    for (long i = 1; i <= Nsamples; i++)
    {
        if (i % (Nsamples/1000) == 0)
        {
            printf("\rProgress: %f%% MSQ: %lf", (float)(100.*(double)i/(double)Nsamples), msq);
            fflush(stdout);
        }

        //Compute error vs. reference
        if (i % error_interval == 0)
        {
            msq = 0;
            bool writeImage = false;
            if (i == 100000 || i == 1000000 || i == 10000000 || i == 100000000)
                writeImage = true;

            for (int y = 0; y < H; y++)
            {
                for (int x = 0; x < W; x++)
                {
                    //Compute pixel value
                    Vector3 result = img[x][y]*b*(double)W*(double)H/(double)i;
                    msq += pow((ptracing_results[y][x] - result).average(), 2);

                    if (writeImage)
                    {
                        //Gamma correct
                        for (int i = 0; i < 3; i++)
                        {
                            result[i] = pow(result[i], 1.f/2.2f);
                        }
                        g_image->setPixel(x,y,result);
                    }
                }
            }

            msq /= (double)(W*H);
            msq_out << msq << endl;

            if (writeImage)
            {
                char filename[100];

                sprintf(filename, "metropolis_%s_%ld.ppm\0", version, i);
                cout << "\nWriting " << filename << "..." << endl;
                g_image->writePPM(filename);
            }
        }

        //Mutate path. 
        path p1;
        mutate_path(p0, p1);

        sample s = samplePath(p1, W, H);

        p1.I = s.value.average();
        p1.F = s.value;

        double accept;

        int x0 = (int)(p0.u[0]*(double)W), y0 = (int)(p0.u[1]*(double)H);
        int x1 = (int)(p1.u[0]*(double)W), y1 = (int)(p1.u[1]*(double)H);
        if (x0 == W) x0--;
        if (y0 == H) y0--;
        if (x1 == W) x1--;
        if (y1 == H) y1--;

        //Add contribution to pixels
        accept = std::min(p1.I / p0.I, 1.);
        if (p0.I > 0)
            img[x0][y0] += (1.-accept)*(p0.F / p0.I);
        if (p1.I > 0)
            img[x1][y1] += accept * (p1.F / p1.I);

        if (frand() < accept)
        {
            path_copy(p0, p1);
        }

    }

    printf("\n");
    for (int x = 0; x < W; x++)
    {
        for (int y = 0; y < H; y++)
        {
            //Compute pixel value
            Vector3 result = img[x][y]*b*(double)W*(double)H/(double)Nsamples;
            b_result += result.average();

            //Gamma correct
            for (int i = 0; i < 3; i++)
            {
                result[i] = pow(result[i], 1.f/2.2f);
            }
            g_image->setPixel(x,y,result);
        }
    }

    cout << "Resulting b: " << b_result/(double)W/(double)H << endl;

    //write msq
    {
        ofstream msq_outfile;
        char filename[100];
        sprintf(filename, "metropolis_%s_msq.dat\0", version);
        msq_outfile.open(filename);
        msq_outfile << msq_out.str().c_str();
    }

}

////Bidirectional path tracing
void a3hacker1()
{
    const int Nseeds = 1000000;
    const long Nsamples = 100000000;

    //Record the error after every this many samples
    const int error_interval = 100000;

    Vector3 img[W][H];
    Vector3 direct_img[W][H]; //Gather eye-light paths here
    memset(img, 0, W*H*sizeof(Vector3));
    memset(direct_img, 0, W*H*sizeof(Vector3));

    cout << "Bidirectional metropolis path tracing" << endl;
    cout << Nsamples / (W*H) << " samples per pixel." << endl;

#ifdef HACKER2
    const char* version = "sphere";
#elif defined (HACKER3)
    const char* version = "red";
#else
    const char* version = "gray";
#endif

    vector<vector<Vector3> > ptracing_results;

    {
        //Load image from task 1
        ifstream ptracing;
        char filename[100];
        sprintf(filename, "pathtracing_%s.raw\0", version);
        ptracing.open(filename, ios::binary);

        int w_pt, h_pt;
        long double b_pt = 0;
        if (!ptracing.is_open())
        {
            w_pt = W; h_pt = H;
        }
        ptracing.read((char*)&w_pt, 4);
        ptracing.read((char*)&h_pt, 4);
        cout << "Loading path tracing results [width=" << w_pt << ", height " << h_pt << "]" << endl;
        for (int i = 0; i < h_pt; i++)
        {
            ptracing_results.push_back(vector<Vector3>());
            for (int j = 0; j < w_pt; j++)
            {
                Vector3 pix(0);
                if (ptracing.is_open())
                {
                    ptracing.read((char*)&pix.x, sizeof(float));
                    ptracing.read((char*)&pix.y, sizeof(float));
                    ptracing.read((char*)&pix.z, sizeof(float));
                }
                b_pt += pix.average();
                ptracing_results[i].push_back(pix);
            }
        }
        b_pt /= (long double)(w_pt*h_pt);
        cout << "Done reading path tracing results. b = " << b_pt << endl;
    }

    long double b = 0;

    path p_init_eye, p_init_light;
    p_init_eye.I = 0;
    p_init_light.I = 0;

    stringstream msq_out;

    double direct_b = 0;
    //Explore L-S*-E paths
    cout << "Computing direct Eye-Light hits.." << endl;
    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
        {
            HitInfo hitInfo;
            Ray ray = g_camera->eyeRay(x, y, W, H, false);
            int depth = PATH_LENGTH;
            Vector3 contrib = Vector3(1);
            while (depth > 0)
            {
                if (g_scene->trace(hitInfo, ray, 0, MIRO_TMAX))
                {
                    PointLight *l = dynamic_cast<PointLight*>(hitInfo.object);
                    if (l != 0)
                    {
                        direct_img[x][y] = l->radiance(hitInfo.P, ray.d)*contrib;
//                        cout << l->radiance(hitInfo.P, ray.d) << endl;
                        depth = -1;
                    }
                    else if (hitInfo.material->isReflective())
                    {
                        ray = ray.reflect(hitInfo);
                        contrib = contrib*hitInfo.material->getReflection();
                    }
                }
                depth--;
            }
            direct_b += direct_img[x][y].average();
        }
    }
    direct_b /= (double)(W*H);

    cout << "Generating path seeds..." << endl;
    //Generate path seeds
    for (int i = 1; i <= Nseeds; i++)
    {
        path p_tmp_eye, p_tmp_light;

        p_tmp_eye.init_random();
        p_tmp_light.init_random();

        sample s = sampleBidirectionalPath(p_tmp_eye, p_tmp_light, W, H);

        if (s.hit)
        {
            long double I = s.value.average();
            b += I*PI;

            //is it a better path?
            if (p_init_eye.I < I)
            {
                path_copy(p_init_eye, p_tmp_eye);
                path_copy(p_init_light, p_tmp_light);
                p_init_eye.I = I;
                cout << "Best: " << I << endl;
            }
        }
    }

    b /= Nseeds;
    cout << "b=" << b+direct_b << endl;

    double b_result = 0;
    double msq = 0;
    //Initialize msq
    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
        {
            double res = (ptracing_results[y][x] - direct_img[x][y]).average();
            msq += res*res;
        }
    }
    msq /= (double)(H*W);


    path p0_eye, p0_light;

    p0_eye.I = 0;
    path_copy(p0_eye, p_init_eye);
    path_copy(p0_light, p_init_light);

    for (long i = 1; i <= Nsamples; i++)
    {
        if (i % (Nsamples/1000) == 0)
        {
            printf("\rProgress: %f%% MSQ: %lf", (float)(100.*(double)i/(double)Nsamples), msq);
            fflush(stdout);
        }

        //Compute error vs. reference
        if (i % error_interval == 0)
        {
            msq = 0;
            bool writeImage = false;
            if (i == 100000 || i == 1000000 || i % 10000000 == 0 || i == 100000000)
                writeImage = true;

            for (int y = 0; y < H; y++)
            {
                for (int x = 0; x < W; x++)
                {
                    //Compute pixel value
                    Vector3 result = img[x][y]*b*(double)W*(double)H/(double)i + direct_img[x][y];
                    msq += pow((ptracing_results[y][x] - result).average(), 2);

                    if (writeImage)
                    {
                        //Gamma correct
                        for (int i = 0; i < 3; i++)
                        {
                            result[i] = pow(abs(result[i]), 1.f/2.2f);
                        }
                        g_image->setPixel(x,y,result);
                    }
                }
            }

            msq /= (double)(W*H);
            msq_out << msq << endl;

            if (writeImage)
            {
                char filename[100];

                sprintf(filename, "bidirectional_%s_%ld.ppm\0", version, i);
                cout << "\nWriting " << filename << "..." << endl;
                g_image->writePPM(filename);
            }
        }

        //Mutate path. 
        path p1_eye, p1_light;
        mutate_path(p0_eye, p1_eye);
        mutate_path(p0_light, p1_light);

        sample s = sampleBidirectionalPath(p1_eye, p1_light, W, H);

        p1_eye.I = s.value.average();
        p1_eye.F = s.value;

        double accept;

        int x0 = (int)(p0_eye.u[0]*(double)W), y0 = (int)(p0_eye.u[1]*(double)H);
        int x1 = (int)(p1_eye.u[0]*(double)W), y1 = (int)(p1_eye.u[1]*(double)H);
        if (x0 == W) x0--;
        if (y0 == H) y0--;
        if (x1 == W) x1--;
        if (y1 == H) y1--;

        //Add contribution to pixels
        accept = std::min(p1_eye.I / p0_eye.I, 1.);
        if (p0_eye.I > 0)
            img[x0][y0] += (1.-accept)*(p0_eye.F / p0_eye.I);
        if (p1_eye.I > 0)
            img[x1][y1] += accept * (p1_eye.F / p1_eye.I);

        if (frand() < accept)
        {
            path_copy(p0_eye, p1_eye);
            path_copy(p0_light, p1_light);
        }
    }

    printf("\n");
    for (int x = 0; x < W; x++)
    {
        for (int y = 0; y < H; y++)
        {
            //Compute pixel value
            Vector3 result = img[x][y]*b*(double)W*(double)H/(double)Nsamples + direct_img[x][y];
            b_result += result.average();

            //Gamma correct
            for (int i = 0; i < 3; i++)
            {
                result[i] = pow(result[i], 1.f/2.2f);
            }
            g_image->setPixel(x,y,result);
        }
    }

    cout << "Resulting b: " << (b_result+direct_b)/(double)W/(double)H << endl;

    //write msq
    {
        ofstream msq_outfile;
        char filename[100];
        sprintf(filename, "bidirectional_%s_msq.dat\0", version);
        msq_outfile.open(filename);
        msq_outfile << msq_out.str().c_str();
    }

}
