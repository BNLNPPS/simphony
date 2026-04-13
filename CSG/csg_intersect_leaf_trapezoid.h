#pragma once
/**
csg_intersect_leaf_trapezoid.h
================================

Parametric trapezoid (G4Trd) intersection and distance functions.

A G4Trd is defined by 5 half-lengths stored in q0/q1:

    q0.f.x = dx1  (x half-length at z = -dz)
    q0.f.y = dy1  (y half-length at z = -dz)
    q0.f.z = dz   (z half-length)
    q0.f.w = dx2  (x half-length at z = +dz)
    q1.f.x = dy2  (y half-length at z = +dz)

The solid is bounded by 6 face planes computed analytically from
these parameters, using the same slab-based convex intersection
algorithm as csg_intersect_leaf_convexpolyhedron.h (RTCD p199).

**/

LEAF_FUNC
float distance_leaf_trapezoid( const float3& pos, const quad& q0, const quad& q1 )
{
    const float dx1 = q0.f.x ;
    const float dy1 = q0.f.y ;
    const float dz  = q0.f.z ;
    const float dx2 = q0.f.w ;
    const float dy2 = q1.f.x ;

    const float two_dz = 2.f * dz ;

    // +X face: outward normal direction (2dz, 0, dx1-dx2)
    const float ddx = dx2 - dx1 ;
    const float inv_Lx = 1.f / sqrtf( two_dz*two_dz + ddx*ddx ) ;
    const float nx_px =  two_dz * inv_Lx ;
    const float nz_px = -ddx * inv_Lx ;     // = (dx1-dx2) * inv_Lx
    const float d_px  =  dz * (dx1 + dx2) * inv_Lx ;

    // +Y face: outward normal direction (0, 2dz, dy1-dy2)
    const float ddy = dy2 - dy1 ;
    const float inv_Ly = 1.f / sqrtf( two_dz*two_dz + ddy*ddy ) ;
    const float ny_py =  two_dz * inv_Ly ;
    const float nz_py = -ddy * inv_Ly ;     // = (dy1-dy2) * inv_Ly
    const float d_py  =  dz * (dy1 + dy2) * inv_Ly ;

    // signed distance to each face: dot(n, pos) - d  (positive = outside)
    // NB: opposing faces share the same z-component in their normals
    float sd = 0.f ;
    sd =          nx_px * pos.x                + nz_px * pos.z - d_px ;  // +X: n=( nx_px, 0, nz_px)
    sd = fmaxf(sd, -nx_px * pos.x              + nz_px * pos.z - d_px ); // -X: n=(-nx_px, 0, nz_px)
    sd = fmaxf(sd,              ny_py * pos.y   + nz_py * pos.z - d_py ); // +Y: n=(0,  ny_py, nz_py)
    sd = fmaxf(sd,             -ny_py * pos.y   + nz_py * pos.z - d_py ); // -Y: n=(0, -ny_py, nz_py)
    sd = fmaxf(sd,  pos.z - dz );                                         // +Z
    sd = fmaxf(sd, -pos.z - dz );                                         // -Z

    return sd ;
}


LEAF_FUNC
void intersect_leaf_trapezoid( bool& valid_isect, float4& isect, const quad& q0, const quad& q1, const float t_min , const float3& o, const float3& d )
{
    const float dx1 = q0.f.x ;
    const float dy1 = q0.f.y ;
    const float dz  = q0.f.z ;
    const float dx2 = q0.f.w ;
    const float dy2 = q1.f.x ;

    const float two_dz = 2.f * dz ;

    // Compute normalized plane normals and distances for 6 faces.
    // Only 2 sqrt operations needed; the +/-Z faces are trivial.

    const float ddx = dx2 - dx1 ;
    const float inv_Lx = 1.f / sqrtf( two_dz*two_dz + ddx*ddx ) ;
    const float nx_px =  two_dz * inv_Lx ;   // +X face normal x-component
    const float nz_px = -ddx * inv_Lx ;       // +X face normal z-component (= (dx1-dx2)/Lx)
    const float d_px  =  dz * (dx1 + dx2) * inv_Lx ;  // distance from origin

    const float ddy = dy2 - dy1 ;
    const float inv_Ly = 1.f / sqrtf( two_dz*two_dz + ddy*ddy ) ;
    const float ny_py =  two_dz * inv_Ly ;   // +Y face normal y-component
    const float nz_py = -ddy * inv_Ly ;       // +Y face normal z-component (= (dy1-dy2)/Ly)
    const float d_py  =  dz * (dy1 + dy2) * inv_Ly ;

    float t0 = -CUDART_INF_F ;  // latest entry
    float t1 =  CUDART_INF_F ;  // earliest exit

    float3 t0_normal = make_float3(0.f) ;
    float3 t1_normal = make_float3(0.f) ;

    // Slab intersection for each pair of opposing faces (RTCD p199)
    // For plane with normal n and distance dp:
    //   nd = dot(n, d)    — negative means entering
    //   no = dot(n, o)
    //   t_cand = (dp - no) / nd = -dist / nd

    float nd, no, dist, t_cand ;

    // +X face: n = (nx_px, 0, nz_px)
    nd = nx_px * d.x + nz_px * d.z ;
    no = nx_px * o.x + nz_px * o.z ;
    dist = no - d_px ;
    if( nd == 0.f )
    {
        if( dist > 0.f ){ valid_isect = false ; return ; }
    }
    else
    {
        t_cand = -dist / nd ;
        if( nd < 0.f ){ if(t_cand > t0){ t0 = t_cand ; t0_normal = make_float3( nx_px, 0.f, nz_px) ; } }
        else           { if(t_cand < t1){ t1 = t_cand ; t1_normal = make_float3( nx_px, 0.f, nz_px) ; } }
    }

    // -X face: n = (-nx_px, 0, nz_px)  — z-component same as +X
    nd = -nx_px * d.x + nz_px * d.z ;
    no = -nx_px * o.x + nz_px * o.z ;
    dist = no - d_px ;
    if( nd == 0.f )
    {
        if( dist > 0.f ){ valid_isect = false ; return ; }
    }
    else
    {
        t_cand = -dist / nd ;
        if( nd < 0.f ){ if(t_cand > t0){ t0 = t_cand ; t0_normal = make_float3(-nx_px, 0.f, nz_px) ; } }
        else           { if(t_cand < t1){ t1 = t_cand ; t1_normal = make_float3(-nx_px, 0.f, nz_px) ; } }
    }

    // +Y face: n = (0, ny_py, nz_py)
    nd = ny_py * d.y + nz_py * d.z ;
    no = ny_py * o.y + nz_py * o.z ;
    dist = no - d_py ;
    if( nd == 0.f )
    {
        if( dist > 0.f ){ valid_isect = false ; return ; }
    }
    else
    {
        t_cand = -dist / nd ;
        if( nd < 0.f ){ if(t_cand > t0){ t0 = t_cand ; t0_normal = make_float3(0.f, ny_py, nz_py) ; } }
        else           { if(t_cand < t1){ t1 = t_cand ; t1_normal = make_float3(0.f, ny_py, nz_py) ; } }
    }

    // -Y face: n = (0, -ny_py, nz_py)  — z-component same as +Y
    nd = -ny_py * d.y + nz_py * d.z ;
    no = -ny_py * o.y + nz_py * o.z ;
    dist = no - d_py ;
    if( nd == 0.f )
    {
        if( dist > 0.f ){ valid_isect = false ; return ; }
    }
    else
    {
        t_cand = -dist / nd ;
        if( nd < 0.f ){ if(t_cand > t0){ t0 = t_cand ; t0_normal = make_float3(0.f,-ny_py, nz_py) ; } }
        else           { if(t_cand < t1){ t1 = t_cand ; t1_normal = make_float3(0.f,-ny_py, nz_py) ; } }
    }

    // +Z face: n = (0, 0, 1), d = dz
    nd = d.z ;
    no = o.z ;
    dist = no - dz ;
    if( nd == 0.f )
    {
        if( dist > 0.f ){ valid_isect = false ; return ; }
    }
    else
    {
        t_cand = -dist / nd ;
        if( nd < 0.f ){ if(t_cand > t0){ t0 = t_cand ; t0_normal = make_float3(0.f, 0.f, 1.f) ; } }
        else           { if(t_cand < t1){ t1 = t_cand ; t1_normal = make_float3(0.f, 0.f, 1.f) ; } }
    }

    // -Z face: n = (0, 0, -1), d = dz
    nd = -d.z ;
    no = -o.z ;
    dist = no - dz ;
    if( nd == 0.f )
    {
        if( dist > 0.f ){ valid_isect = false ; return ; }
    }
    else
    {
        t_cand = -dist / nd ;
        if( nd < 0.f ){ if(t_cand > t0){ t0 = t_cand ; t0_normal = make_float3(0.f, 0.f,-1.f) ; } }
        else           { if(t_cand < t1){ t1 = t_cand ; t1_normal = make_float3(0.f, 0.f,-1.f) ; } }
    }

    valid_isect = t0 < t1 ;
    if(valid_isect)
    {
        if( t0 > t_min )
        {
            isect.x = t0_normal.x ;
            isect.y = t0_normal.y ;
            isect.z = t0_normal.z ;
            isect.w = t0 ;
        }
        else if( t1 > t_min )
        {
            isect.x = t1_normal.x ;
            isect.y = t1_normal.y ;
            isect.z = t1_normal.z ;
            isect.w = t1 ;
        }
    }
}
