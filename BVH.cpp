#include <algorithm>
#include "Triangle.h"
#include "SSE.h"
#include "BVH.h"
#include "Ray.h"
#include "Console.h"

#ifdef STATS
#include "Stats.h"
#endif

using namespace std;

void getCornerPoints(Corner (&outCorners)[2], Objects * objs)
{
    for (int i = 0; i < 3; i++)
    {
        outCorners[0][i] = infinity;
        outCorners[1][i] = -infinity;
    }

    for (size_t i = 0; i < objs->size(); ++i)
    {
        //Ignore infinitely spanning objects like planes
        if (!(*objs)[i]->isBounded()) continue;

        Vector3 objMax = (*objs)[i]->coordsMax(),
                objMin = (*objs)[i]->coordsMin();
    
        for (int j = 0; j < 3; j++)
        {
            if (outCorners[1][j] < objMax[j])
                outCorners[1][j] = objMax[j];
            if (outCorners[0][j] > objMin[j])
                outCorners[0][j] = objMin[j];
        }
    }
}


float getArea(const Corner (&corners)[2])
{
    float area = 0;
    for (int d = 0; d < 3; d++)
    {
        int dim1 = (d+1)%3, dim2 = (d+2)%3;
        area += (corners[1][dim1]-corners[0][dim1])*(corners[1][dim2]-corners[0][dim2]); 
    }
    
    return 2*area;
}

inline float getCost(const Corner (&corners)[2], Objects &objects)
    {
        //If the number of objects is 0, corners might have funny values.
        if (objects.size() == 0) return 0;
        return ((float)objects.size()) * getArea(corners);
    }

void
BVH::build(Objects * objs, int depth)
{
#ifdef STATS
    Stats::BVH_Nodes += 1;
#endif

    // construct the bounding volume hierarchy
    //Find the bounds of this node 
    if (m_corners[0][0] == infinity)
    {
        getCornerPoints(m_corners, objs); 
    }

    //Expand the box by a small epsilon in case it's bounding a flat triangle or similar.
    for (int i = 0; i < 3; i++)
    {
        m_corners[0][i] -= epsilon;
        m_corners[1][i] += epsilon;
    }

    //Check if we're done
    if (objs->size() <= OBJECTS_PER_LEAF || depth >= MAX_TREE_DEPTH)
    {
        m_objects = objs;
        m_isLeaf = true;

#ifdef STATS
		Stats::BVH_LeafNodes++;
#endif 

        #ifdef __SSE4_1__
        //Build the triangle cache for SSE
        int nTriangles = 0;
        for (int i = 0; i < objs->size(); i++)
        {
            if (dynamic_cast<Triangle*>((*objs)[i]) != 0) nTriangles++;
        }

        m_triangleCache = new std::vector<SSETriangleCache>;
        int tr = 0;
        for (int i = 0; i < objs->size(); i++)
        {
            Triangle *t = dynamic_cast<Triangle*>((*objs)[i]);
            if (t == 0) continue;
            else
            {
                objs->erase(objs->begin() + i);
                i--;
            }
            if (tr/4 >= m_triangleCache->size())
            {
                m_triangleCache->push_back(SSETriangleCache());
            }
            (*m_triangleCache)[tr/4].triangles[tr%4] = t;
            (*m_triangleCache)[tr/4].nTriangles++;
            tr ++;
        }

        // Put the vertices and normals into SSE vectors
        for (int i = 0; i < m_triangleCache->size(); i++)
        {
            SSETriangleCache & c = (*m_triangleCache)[i];

            //3 dimensions, up to 4*3 floats (3 components per vertex). For each dimension, store (A1x, A2x, A3x, A4x, B1x, B2x, B3x...)
            float verts[3][12];       
            float normals[3][12]; 

#pragma unroll(4)
            for (int t = 0; t < c.nTriangles; t++)
            {
                TriangleMesh* m = c.triangles[t]->getMesh();
                TriangleMesh::TupleI3 vInd = m->vIndices()[c.triangles[t]->getIndex()];
                TriangleMesh::TupleI3 nInd = m->nIndices()[c.triangles[t]->getIndex()];

                //for each vertex (A, B, C)
#pragma unroll(3)
                for (int k = 0; k < 3; k++)
                {
                    //for each dimension
#pragma unroll(3)
                    for (int i = 0; i < 3; i++)
                    {
                        {
                            verts[i][k*4+t] = m->vertices()[vInd.v[k]][i];
                            normals[i][k*4+t] = m->normals()[nInd.v[k]][i];
                        }
                    }
                }
            }

            //For each dimension, load the A and calculate B-A and C-A.
#pragma unroll(3)
            for (int i = 0; i < 3; i++)
            {
                c.A.v[i] = _mm_loadu_ps(&verts[i][0]);
                c.nA.v[i] = _mm_loadu_ps(&normals[i][0]);
                c.nB.v[i] = _mm_loadu_ps(&normals[i][4]);
                c.nC.v[i] = _mm_loadu_ps(&normals[i][8]);
                c.BmA.v[i] = _mm_sub_ps(_mm_loadu_ps(&verts[i][4]), c.A.v[i]);
                c.CmA.v[i] = _mm_sub_ps(_mm_loadu_ps(&verts[i][8]), c.A.v[i]);
            }

            //Do the cross product
            c.normal = SSEmultiCross(c.BmA, c.CmA);
        }
#endif
    }
    else
    {
        //Split the node
        //and recurse into children
        float bestCost = infinity, bestPosition;
        int bestDim = 0; Vector3 bestCorners[2][2];
        const int maxSearchDepth = 32; //Max search depth for binary search.
        m_isLeaf = false;

        //Do a binary search for the best splitting position for each dimension. Pick the dimension with the best split.
        for (int dim = 0; dim < 3; dim++)
        {
            float current = (m_corners[1][dim] + m_corners[0][dim])/2.0f, beg = m_corners[0][dim], end = m_corners[1][dim];
            bool done = false;
            int nocheckBoundary[2] = {0, 0};
            Objects children[2];
            Corner corners[2][2];

            //Put objects into the "left" and "right" node respectively, based on their position in the current dimension
            for (int i = 0; i < objs->size(); i++)
            {
                if ((*objs)[i]->center()[dim] < current)
                    children[0].push_back((*objs)[i]);
                else
                    children[1].push_back((*objs)[i]);
            }

            getCornerPoints(corners[0], &children[0]);
            getCornerPoints(corners[1], &children[1]);

            for (int searchDepth = 0; searchDepth < maxSearchDepth; searchDepth++)
            {
                //Get the cost for both sides, to determine which side to reduce
                float costLeft = getCost(corners[0], children[0]), costRight = getCost(corners[1], children[1]);

                //Is this the best cost so far? If so, store it.
                if (costLeft+costRight < bestCost)
                {
                    bestCost = costLeft+costRight;
                    bestDim = dim;
                    bestPosition = current;
                    for (int i = 0; i < 2; i++)
                    {
                        for (int j = 0; j < 3; j++)
                        {
                            bestCorners[0][i][j] = corners[0][i][j];
                            bestCorners[1][i][j] = corners[1][i][j];
                        }
                    }
                }
                //If the left node has a higher cost, we want to reduce that area. If not, we want to reduce the right area.
                //We also know that the side that we are reducing must have decreasing or equal max corners, and increasing or equal min corners.
                //Vica versa for the area we are increasing.
                //Also, we know that the existing objects in the area we are increasing won't ever be needed to be checked again.
                bool canShrink = false; //Indicates if it's possible that one of the boxes can be shrunk
                int largest = (costLeft > costRight ? 0 : 1);
                int smallest = (largest+1)%2;

                //If the largest element is the "left" one, we have found an upper bound for our plane, which is "current".
                if (largest == 0)
                    end = current;
                //Otherwise, we've found a lower bound for the plane.
                else
                    beg = current;
                current = (beg+end)/2;

                //"Mark" the smaller array so that the items in there at the moment won't be checked again.
                //We already determined that the area is too small, so that is certain.
                nocheckBoundary[smallest] = children[smallest].size();

                int moveCount = 0;
                //Move the objects from the more costly child to the other one 
                for (int i = children[largest].size() - 1; i >=  nocheckBoundary[largest]; i--)
                {
                    //Need two conditions here because if the more costly child is the "left" side,
                    //we want to check if the object's center is larger than the current boundary.
                    //Otherwise we want to check if it's smaller.
                    if ((largest == 0 && children[largest][i]->center()[dim] > current) || 
                            (largest == 1 && children[largest][i]->center()[dim] < current)) 
                    {
                        moveCount++;
                        //Get the bounds of the object being moved
                        Vector3 cmax = children[largest][i]->coordsMax();
                        Vector3 cmin = children[largest][i]->coordsMin();

                        //Check if we need to increase the bounds of the right side after inserting the object
                        for (int j = 0; j < 3; j++)
                        {
                            if (cmax[j] > corners[smallest][1][j])
                            {
                                corners[smallest][1][j] = cmax[j];
                            }
                            if (cmin[j] < corners[smallest][0][j])
                            {
                                corners[smallest][0][j] = cmin[j];
                            }

                            if (!canShrink)
                            {
                                //Check if it's possible that the left side can be shrunk, that is, if the object we're moving was on the border
                                if (cmax[j] >= corners[largest][1][j]-epsilon || cmin[j] <= corners[largest][0][j]+epsilon)
                                {
                                    canShrink = true;
                                }
                            }
                        }

                        children[smallest].push_back(children[largest][i]);
                        std::swap(children[largest][i], children[largest][children[largest].size()-1]);
                        children[largest].pop_back();
                    }
                }

                //The large side must be rechecked for boundaries because it might have shrunk
                if (canShrink)
                {
                    getCornerPoints(corners[largest], &children[largest]);
                }
            }

            float costLeft = getCost(corners[0], children[0]), costRight = getCost(corners[1], children[1]);
            if (costLeft+costRight < bestCost)
            {
                bestCost = costLeft+costRight;
                bestDim = dim;
                bestPosition = current;
                for (int i = 0; i < 2; i++)
                {
                    for (int j = 0; j < 3; j++)
                    {
                        bestCorners[0][i][j] = corners[0][i][j];
                        bestCorners[1][i][j] = corners[1][i][j];
                    }
                }
            }

        }    

        //Add child nodes
        m_children = new vector<BVH*>;
        Objects* left, * right;
        left = new Objects; right = new Objects;


		//Split the object array according to the best splitting plane we found
        for (int i = 0; i < objs->size(); i++)
        {
            if ((*objs)[i]->center()[bestDim] < bestPosition)
                left->push_back((*objs)[i]);
            else
                right->push_back((*objs)[i]);
        }

        for (int i = 0; i < 2; i++)
        {
            Objects* current = (i == 0 ? left : right);
            m_children->push_back(new BVH);
            for (int j = 0; j < 3; j++)
            {
                m_children->back()->m_corners[0][j] = bestCorners[i][0][j];
                m_children->back()->m_corners[1][j] = bestCorners[i][1][j];
            }
            (*m_children)[i]->build(current, depth+1);

            // If the new node is an internal one, free the object list since it wasn't used.
            if (!(*m_children)[i]->m_isLeaf) 
            {
                delete current;
           }
        }
    }
}

#ifdef __SSE4_1__
    int SSEintersectTriangles(SSETriangleCache &cache, HitInfo& hitInfo, const Ray &ray, float tMin, float tMax)
    {
        //Define some constants that are used throughout.
        static const __m128 _one = _mm_set1_ps(1.0f);
        static const __m128 _zero = _mm_setzero_ps();
        static const __m128 _minus_epsilon = _mm_set1_ps(-epsilon);
        static const __m128 _one_plus_epsilon = _mm_set1_ps(1.0f+epsilon);
        float outT[4], outP[3][4], outN[3][4];

        //Each __m128 contains the coordinates for 4 triangles and there is 3 dimensions.
        //const SSEVectorTuple3 &rayD = ray.d_SSE, &rayO = ray.o_SSE;
        SSEVectorTuple3 RomA;
        const __m128 &rayD = ray.d_SSE, &rayO = ray.o_SSE;

        //Calculate ray.o - A, as this is used to calculate both t, beta and gamma.
        #pragma unroll(3)
        for (int i = 0; i < 3; i++)
        {
            RomA.v[i] = _mm_sub_ps(_mm_shuffle_ps(rayO, rayO, _MM_SHUFFLE(i,i,i,i)), cache.A.v[i]);
        }

        __m128 ddotn, t, beta, gamma, alpha;
        
        //Only going to use the reciprocal
        ddotn = _mm_rcp_ps(SSEmultiDot13(rayD, cache.normal));

        //Calculate t, beta and gamma
        t = _mm_mul_ps(SSEmultiDot33(RomA, cache.normal), ddotn); 
        beta = _mm_mul_ps(SSEmultiDot13(rayD, SSEmultiCross(RomA, cache.CmA)), ddotn); 
        gamma = _mm_mul_ps(SSEmultiDot13(rayD, SSEmultiCross(cache.BmA, RomA)), ddotn); 

        //Test t, beta and gamma
        int mask = _mm_movemask_ps(_mm_and_ps(_mm_cmpgt_ps(beta, _minus_epsilon),
                              _mm_and_ps(_mm_cmpgt_ps(gamma, _minus_epsilon),
                                         _mm_and_ps(_mm_cmplt_ps(_mm_add_ps(gamma, beta), _one_plus_epsilon),
                                                    _mm_and_ps(_mm_cmpgt_ps(t, _mm_set1_ps(tMin)), _mm_cmplt_ps(t, _mm_set1_ps(tMax)))))));

        if (mask == 0) return -1;
        
        _mm_storeu_ps(outT, t);

        //Find the lowest t > tMin
        int best = -1;
        for (int i = 0; i < cache.nTriangles; i++)
        {
            if ((mask & (1 << i)) == 0)
                continue;

            if (best == -1 || outT[i] < outT[best]) best = i;
        }

        if (best == -1) return -1;


        alpha = _mm_sub_ps(_mm_sub_ps(_one, beta), gamma);

        #pragma unroll(3)
        for (int i = 0; i < 3; i++)
        {
            //Any way to improve this, as we only need 1/3rd of the values?
            _mm_storeu_ps(outP[i], _mm_add_ps(cache.A.v[i], _mm_add_ps(_mm_mul_ps(beta, cache.BmA.v[i]), _mm_mul_ps(gamma, cache.CmA.v[i]))));
            _mm_storeu_ps(outN[i], _mm_add_ps(_mm_mul_ps(alpha, cache.nA.v[i]), _mm_add_ps(_mm_mul_ps(beta, cache.nB.v[i]), _mm_mul_ps(gamma, cache.nC.v[i]))));
        }

        hitInfo.t = outT[best];

        hitInfo.P = Vector3(outP[0][best], outP[1][best], outP[2][best]);
        hitInfo.N = Vector3(outN[0][best], outN[1][best], outN[2][best]);

        return best;
    }

    inline bool intersectTriangleList(SSETriangleCache &cache, HitInfo& minHit, const Ray& ray, float tMin)
    {
        HitInfo tempMinHit;

        int best = SSEintersectTriangles(cache, tempMinHit, ray, tMin, minHit.t);
        if (best != -1)
        {       
            if (tempMinHit.t < minHit.t)
            {
                minHit = tempMinHit;

                //Just call intersect to get the material
                cache.triangles[best]->intersect(minHit, ray, tMin, minHit.t);
                 
                //Update object reference
                minHit.object = cache.triangles[best];
            }
            return true;
        }
        return false;
    }
#endif

//Intersect the root node
bool
BVH::intersect(HitInfo& minHit, const Ray& ray, float tMin, float tMax) const
{
    // Traverse the BVH to perform ray-intersection acceleration.
    bool hit = false;
    HitInfo tempMinHit;
    minHit.t = tMax;

    // intersect with root node bounding box
    float minOverlap = -infinity, maxOverlap = infinity;
    float t[2];
    for (int i = 0; i < 3; ++i)
    {
        t[0] = (m_corners[0][i] - ray.o[i]) / ray.d[i];
        t[1] = (m_corners[1][i] - ray.o[i]) / ray.d[i];
        int m = t[0] > t[1]; //m is the index of the smaller boundary

        //m^1 is the larger boundary (0 xor 1 = 1, 1 xor 1 = 0)
        if (t[m] > minOverlap) minOverlap = t[m];
        if (t[m^1] < maxOverlap) maxOverlap = t[m^1];
    }

#ifdef STATS
	Stats::Ray_Box_Intersect++;
#endif

    // return false if there is no overlap
    if (minOverlap > maxOverlap || minOverlap > tMax || maxOverlap < tMin)
        return false;

    return intersectChildren(minHit, ray, tMin, tMax);
}

bool
BVH::intersectChildren(HitInfo& minHit, const Ray& ray, float tMin, float tMax) const
{
    // Traverse the BVH to perform ray-intersection acceleration.
    bool hit = false;
    HitInfo tempMinHit;
    minHit.t = tMax;
    if (m_isLeaf)
    {
        //For SSE, we have already put a lot of stuff in our cache datastructure, so just call the triangle list intersection        
#ifdef __SSE4_1__
        for (int i = 0; i < m_triangleCache->size(); i++)
        {
            if (intersectTriangleList((*m_triangleCache)[i], minHit, ray, tMin))
                hit = true;
#ifdef STATS
            Stats::Ray_Tri_Intersect += m_triangleCache->at(i).nTriangles;
#endif

        }
#endif

        for (size_t i = 0; i < m_objects->size(); ++i)
        {
#ifdef STATS
            if (dynamic_cast<Triangle*>((*m_objects)[i]) != 0) Stats::Ray_Tri_Intersect++;
#endif
            if ((*m_objects)[i]->intersect(tempMinHit, ray, tMin, minHit.t))
            {
                if (tempMinHit.t < minHit.t)
                {
                    hit = true;
                    minHit = tempMinHit;

                    //Update object reference
                    minHit.object = (*m_objects)[i];
                }
            }
        }
        return hit;
    }

#ifdef __SSE4_1__
    BVH* children[2] = {m_children->at(0), m_children->at(1)};

    //Calculate all the intersection times
    __m128 t[4] = {_mm_mul_ps(_mm_sub_ps(children[0]->m_corners_SSE[0], ray.o_SSE), ray.d_SSE_rcp),
                   _mm_mul_ps(_mm_sub_ps(children[0]->m_corners_SSE[1], ray.o_SSE), ray.d_SSE_rcp),
                   _mm_mul_ps(_mm_sub_ps(children[1]->m_corners_SSE[0], ray.o_SSE), ray.d_SSE_rcp),
                   _mm_mul_ps(_mm_sub_ps(children[1]->m_corners_SSE[1], ray.o_SSE), ray.d_SSE_rcp)};

    //Then sort them
    __m128 tmin[2] = {_mm_min_ps(t[0], t[1]), _mm_min_ps(t[2], t[3])};
    __m128 tmax[2] = {_mm_max_ps(t[0], t[1]), _mm_max_ps(t[2], t[3])};
    
    //Some magic inline assembly to find the actual overlapping regions.
    //In short, we find the minimum of the maximum values, and the maximum of the minimum values and store them in tmin[0].
    asm(
        "maxss  4+%[tmin0], %[r];\n" //stores max of tmin[0][0] and tmin[0][1] in tmin[0][0]
        "maxss  8+%[tmin0], %[r];\n" //stores max of tmin[0][0] and tmin[0][2] in tmin[0][0]
        "pslldq $4, %[r];\n"          //shift the value we found to the right by 4 bytes. Result of previous ops are now in tmin[0][1]
        "movss   %[tmax0r], %[r];\n"   //move tmax[0][0] into tmin[0][0]
        "minss  4+%[tmax0], %[r];\n" //stores min of tmax[0][1] and tmin[0][0] in tmin[0][0]
        "minss  8+%[tmax0], %[r];\n" //stores min of tmax[0][2] and tmin[0][0] in tmin[0][0]
        "pslldq $4, %[r];\n"          //shift the values we found to the right by 4 bytes. Result of previous ops are now in tmin[0][1..2]
        "movss   %[tmin1r], %[r];\n"   //move tmin[1][0] into tmin[0][0]
        "maxss  4+%[tmin1], %[r];\n" //stores max of tmin[0][0] and tmin[1][1] in tmin[0][0]
        "maxss  8+%[tmin1], %[r];\n" //stores max of tmin[0][0] and tmin[1][2] in tmin[0][0]
        "pslldq $4, %[r];\n"          //shift the values we found to the right by 4 bytes. Result of previous ops are now in tmin[0][1..3]
        "movss   %[tmax1r], %[r];\n"   //move tmax[1][0] into tmin[0][0]
        "minss  4+%[tmax1], %[r];\n" //stores max of tmin[0][0] and tmin[1][1] in tmin[0][0]
        "minss  8+%[tmax1], %[r];\n" //stores max of tmin[0][0] and tmin[1][2] in tmin[0][0]
        //Registers (note that movss requires registers and not memory locations if the higher order values are to be preserved)
        : [r] "+x" (tmin[0]), [tmin1r] "+x" (tmin[1]), [tmax0r] "+x" (tmax[0]), [tmax1r] "+x" (tmax[1]) 
        //Memory operands 
        : [tmin0] "m" (tmin[0]), [tmin1] "m" (tmin[1]), [tmax0] "m" (tmax[0]), [tmax1] "m" (tmax[1]) 
    ); 

    //We now have our results in tmin[0]

    //Indices in out:
    //3: minOverlap[0]
    //2: maxOverlap[0]
    //1: minOverlap[1]
    //0: maxOverlap[1]
    float out[4];

    _mm_storeu_ps(out, tmin[0]);

    //Check the closest box first, that is, the box with the closest minOverlap value.
    int ind = 0;
    if (out[3] < out[1])
        ind = 2;

    if (!(out[ind+1] > out[ind] || out[ind+1] > minHit.t || out[ind] < tMin))
    {
        //Intersect with child.
        if (children[1 - (ind >> 1)]->intersectChildren(tempMinHit, ray, tMin, minHit.t))
        {
            minHit = tempMinHit;
            hit = true;
        }
    }
    //Update the index to point to the second child to test.
    //The & operation is just a cheap modulo.
    ind = (ind + 2) & 3;
    if (!(out[ind+1] > out[ind] || out[ind+1] > minHit.t || out[ind] < tMin))
    {
        if (children[1 - (ind >> 1)]->intersectChildren(tempMinHit, ray, tMin, minHit.t))
        {
            minHit = tempMinHit;
            hit = true;
        }
    }

#else
    float minT = infinity;
    float minTother = infinity;
    int minIndex = -1;
    int otherIndex = -1;

    //Check intersection with children
    for (int i = 0; i < 2; i++)
    {
        //Check intersection
        const BVH* child = m_children->at(i);
        float minOverlap = -infinity, maxOverlap = infinity;
        float t[2];
        for (int j = 0; j < 3; ++j)
        {
            t[0] = (child->m_corners[0][j] - ray.o[j]) / ray.d[j];
            t[1] = (child->m_corners[1][j] - ray.o[j]) / ray.d[j];
            int m = t[0] > t[1]; //m is the index of the smaller boundary

            //m^1 is the larger boundary (0 xor 1 = 1, 1 xor 1 = 0)
            if (t[m] > minOverlap) minOverlap = t[m];
            if (t[m^1] < maxOverlap) maxOverlap = t[m^1];
        }
        if (minOverlap > maxOverlap || minOverlap > tMax || maxOverlap < tMin)
            continue;

        if (minT > minOverlap)
        {
            minTother = minT;
            otherIndex = minIndex;
            minT = minOverlap;
            minIndex = i;
        }
        else if (minTother > minOverlap)
        {
            otherIndex = i;
            minTother = minOverlap;
        }
    }
    if (minIndex == -1) return false;

	if (minIndex == -1)
		return false;

    //Intersect in order from closest to furthest away, to eliminate some box intersection tests
#ifdef STATS
    Stats::Ray_Box_Intersect += 1;
#endif
    if (m_children->at(minIndex)->intersectChildren(tempMinHit, ray, tMin, minHit.t))
    {
        minHit = tempMinHit;
        hit = true;
    } 
    
  //  if (otherIndex != -1)
    {
#ifdef STATS
        Stats::Ray_Box_Intersect += 1;
#endif
        //if (m_children->at(minIndex^1)->intersectChildren(tempMinHit, ray, tMin, minHit.t))
        if (m_children->at(minIndex^1)->intersectChildren(tempMinHit, ray, tMin, minHit.t))
        {
            minHit = tempMinHit;
            hit = true;
        } 
    }

    
#endif


    return hit;
}
