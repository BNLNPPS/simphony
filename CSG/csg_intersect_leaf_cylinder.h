#pragma once
/**
intersect_leaf_cylinder : a much simpler approach than intersect_leaf_oldcylinder
-------------------------------------------------------------------------------------------

The two cylinder imps were compared with tests/CSGIntersectComparisonTest.cc.
Surface distance comparisons show the new imp is more precise and does
not suffer from near-axial spurious intersects beyond the ends.  

intersect_leaf_cylinder

   * simple as possible approach, minimize the flops
   * axial special case removed, might need to put back if find some motivation to do that

intersect_leaf_oldcylinder

   * pseudo-general approach, based on implementation from book RTCD  
   * had axial special case bolted on for unrecorded reason, some glitch presumably 


There are four possible t

* 2 from curved sheet, obtained from solving quadratic, that must be within z1 z2 range
* 2 from endcaps that must be within r2 range  

Finding the intersect means finding the smallest t from the four that exceeds t_min  

Current approach keeps changing t_cand, could instead collect all four potential t 
into a float4 and then pick from that ? 

**/

LEAF_FUNC
void intersect_leaf_cylinder( bool& valid_isect, float4& isect, const quad& q0, const quad& q1, const float t_min, const float3& ray_origin, const float3& ray_direction )
{
    const float& r = q0.f.w;
    const float& z1 = q1.f.x;
    const float& z2 = q1.f.y;
    // Optional phi-wedge clip: q0.f.x=startPhi, q0.f.y=deltaPhi (rad).
    // deltaPhi==0 (or >=2π) means no clip — original full-cylinder behaviour.
    const float  startPhi = q0.f.x;
    const float  deltaPhi = q0.f.y;
    const bool   has_phi_clip = (deltaPhi > 0.f && deltaPhi < 2.f * M_PIf);
    const float& ox = ray_origin.x;
    const float& oy = ray_origin.y;
    const float& oz = ray_origin.z;
    const float& vx = ray_direction.x;
    const float& vy = ray_direction.y;
    const float& vz = ray_direction.z;

    const float r2 = r * r;
    const float a = vx * vx + vy * vy; // see CSG/sympy_cylinder.py
    const float b = ox * vx + oy * vy;
    const float c = ox * ox + oy * oy - r2;

    float t_near, t_far, disc, sdisc;
    robust_quadratic_roots_disqualifying(t_min, t_near, t_far, disc, sdisc, a, b, c); //  Solving:  a t^2 + 2 b t +  c = 0
    float z_near = oz + t_near * vz;
    float z_far = oz + t_far * vz;

    const float t_z1cap = (z1 - oz) / vz;
    const float r2_z1cap = (ox + t_z1cap * vx) * (ox + t_z1cap * vx) + (oy + t_z1cap * vy) * (oy + t_z1cap * vy);

    const float t_z2cap = (z2 - oz) / vz;
    const float r2_z2cap = (ox + t_z2cap * vx) * (ox + t_z2cap * vx) + (oy + t_z2cap * vy) * (oy + t_z2cap * vy);

#ifdef DEBUG
    // printf("// t_z1cap %10.4f r2_z1cap %10.4f \n", t_z1cap, r2_z1cap );
    // printf("// t_z2cap %10.4f r2_z2cap %10.4f \n", t_z2cap, r2_z2cap );
#endif

    auto phi_in_wedge = [&](float xh, float yh) -> bool {
        if (!has_phi_clip)
            return true;
        float       phi = atan2f(yh, xh);
        const float twoPi = 2.f * M_PIf;
        while (phi < startPhi) phi += twoPi;
        return (phi - startPhi) <= deltaPhi;
    };
    // Side-surface candidates also need to be inside the wedge.
    bool t_near_ok = (z_near > z1 && z_near < z2) && phi_in_wedge(ox + t_near * vx, oy + t_near * vy);
    bool t_far_ok = (z_far > z1 && z_far < z2) && phi_in_wedge(ox + t_far * vx, oy + t_far * vy);
    // Cap candidates need their hit point's r AND phi inside.
    bool t_z1cap_ok = (r2_z1cap <= r2) && phi_in_wedge(ox + t_z1cap * vx, oy + t_z1cap * vy);
    bool t_z2cap_ok = (r2_z2cap <= r2) && phi_in_wedge(ox + t_z2cap * vx, oy + t_z2cap * vy);

    // Flat radial wedge-wall candidates at phi=startPhi and phi=startPhi+deltaPhi.
    // The two half-planes contain the z-axis. A hit must land within
    //   r_h^2 <= r^2,  z1 < z_h < z2,  and on the +r side of the wall
    // (i.e. not the antipodal half-plane through the same line).
    // Outward normal at startPhi:        n1 = ( sin(startPhi), -cos(startPhi), 0)
    // Outward normal at startPhi+deltaPhi: n2 = (-sin(endPhi),   cos(endPhi),  0)
    float t_phi1 = CUDART_INF_F, t_phi2 = CUDART_INF_F;
    bool  t_phi1_ok = false, t_phi2_ok = false;
    float n1x = 0.f, n1y = 0.f, n2x = 0.f, n2y = 0.f;
#ifndef CSG_CYL_NO_PHI_WALLS
    if (has_phi_clip)
    {
        const float endPhi = startPhi + deltaPhi;
        const float s1 = sinf(startPhi), c1 = cosf(startPhi);
        const float s2 = sinf(endPhi), c2 = cosf(endPhi);
        n1x = s1;
        n1y = -c1;
        n2x = -s2;
        n2y = c2;

        // phi=startPhi plane: { x*s1 - y*c1 = 0 } solved for t
        const float denom1 = vx * s1 - vy * c1;
        if (fabsf(denom1) > 1.e-12f)
        {
            t_phi1 = -(ox * s1 - oy * c1) / denom1;
            const float xh = ox + t_phi1 * vx;
            const float yh = oy + t_phi1 * vy;
            const float zh = oz + t_phi1 * vz;
            const float r2_h = xh * xh + yh * yh;
            const bool  on_pos_r = (xh * c1 + yh * s1) >= 0.f; // not antipodal half
            t_phi1_ok = on_pos_r && (r2_h <= r2) && (zh > z1) && (zh < z2);
        }
        // phi=endPhi plane: { x*s2 - y*c2 = 0 } solved for t
        const float denom2 = vx * s2 - vy * c2;
        if (fabsf(denom2) > 1.e-12f)
        {
            t_phi2 = -(ox * s2 - oy * c2) / denom2;
            const float xh = ox + t_phi2 * vx;
            const float yh = oy + t_phi2 * vy;
            const float zh = oz + t_phi2 * vz;
            const float r2_h = xh * xh + yh * yh;
            const bool  on_pos_r = (xh * c2 + yh * s2) >= 0.f;
            t_phi2_ok = on_pos_r && (r2_h <= r2) && (zh > z1) && (zh < z2);
        }
    }
#endif // CSG_CYL_NO_PHI_WALLS

    float t_cand = CUDART_INF_F;
    if (t_near > t_min && t_near_ok && t_near < t_cand)
        t_cand = t_near;
    if (t_far > t_min && t_far_ok && t_far < t_cand)
        t_cand = t_far;
    if (t_z1cap > t_min && t_z1cap_ok && t_z1cap < t_cand)
        t_cand = t_z1cap;
    if (t_z2cap > t_min && t_z2cap_ok && t_z2cap < t_cand)
        t_cand = t_z2cap;
    if (t_phi1 > t_min && t_phi1_ok && t_phi1 < t_cand)
        t_cand = t_phi1;
    if (t_phi2 > t_min && t_phi2_ok && t_phi2 < t_cand)
        t_cand = t_phi2;

    valid_isect = t_cand > t_min && t_cand < CUDART_INF_F;
    if(valid_isect)
    {
        bool sheet = (t_cand == t_near || t_cand == t_far);
        bool cap = (t_cand == t_z1cap || t_cand == t_z2cap);
        bool phi1 = (t_cand == t_phi1);
        if (sheet)
        {
            isect.x = (ox + t_cand * vx) / r;
            isect.y = (oy + t_cand * vy) / r;
            isect.z = 0.f;
        }
        else if (cap)
        {
            isect.x = 0.f;
            isect.y = 0.f;
            isect.z = (t_cand == t_z1cap ? -1.f : 1.f);
        }
        else // phi-wall
        {
            isect.x = phi1 ? n1x : n2x;
            isect.y = phi1 ? n1y : n2y;
            isect.z = 0.f;
        }
        isect.w = t_cand;
    }
}





/**
distance_leaf_cylinder
------------------------

* https://www.iquilezles.org/www/articles/distfunctions/distfunctions.htm

Capped Cylinder - exact

float sdCappedCylinder( vec3 p, float h, float r )
{
  vec2 d = abs(vec2(length(p.xz),p.y)) - vec2(h,r); 
  // dont follow  would expect h <-> r with radius to be on the first dimension and height on second
     
  return min(max(d.x,d.y),0.0) + length(max(d,0.0));
}


                      p
                      +
                      | 
                      |
                      | 
  - - - +---r----+----+---+ - - - - - -
        |        :        |
        h        :        +------+ p    
        |        :        |
        |        :        |
        +--------+--------+
        |        :        |
        |        :        |
        |        :        |
        |        :        |
  - - - +--------+--------+ - - - - - - - 





The SDF rules for CSG combinations::

    CSG union(l,r)     ->  min(l,r)
    CSG intersect(l,r) ->  max(l,r)
    CSG difference(l,r) -> max(l,-r)    [aka subtraction, corresponds to intersecting with a complemented rhs]


**/


LEAF_FUNC
float distance_leaf_cylinder( const float3& pos, const quad& q0, const quad& q1 )
{
    const float radius = q0.f.w;
    const float z1 = q1.f.x;
    const float z2 = q1.f.y; // z2 > z1
    const float startPhi = q0.f.x;
    const float deltaPhi = q0.f.y;

    float sd_capslab = fmaxf(pos.z - z2, z1 - pos.z);
    float sd_infcyl = sqrtf(pos.x * pos.x + pos.y * pos.y) - radius;
    float sd = fmaxf(sd_capslab, sd_infcyl);

    if (deltaPhi > 0.f && deltaPhi < 2.f * M_PIf)
    {
        float       phi = atan2f(pos.y, pos.x);
        const float twoPi = 2.f * M_PIf;
        while (phi < startPhi) phi += twoPi;
        float dphi = phi - startPhi;
        if (dphi > deltaPhi)
        {
            float r_xy = sqrtf(pos.x * pos.x + pos.y * pos.y);
            float d_to_edge = r_xy * fminf(dphi - deltaPhi, twoPi - dphi);
            sd = fmaxf(sd, d_to_edge);
        }
    }

#ifdef DEBUG
    printf("//distance_leaf_cylinder sd %10.4f \n", sd);
#endif
    return sd;
}


