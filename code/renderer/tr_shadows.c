/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
#include "tr_local.h"


/*

  for a projection shadow:

  point[x] += light vector * ( z - shadow plane )
  point[y] +=
  point[z] = shadow plane

  1 0 light[x] / light[z]

*/

typedef struct {
	int		i2;
	int		facing;
} edgeDef_t;

#define	MAX_EDGE_DEFS	32

static void RB_DrawShadowQuads( void );		// WALKER: below

static	edgeDef_t	edgeDefs[SHADER_MAX_VERTEXES][MAX_EDGE_DEFS];
static	int			numEdgeDefs[SHADER_MAX_VERTEXES];
static	int			facing[SHADER_MAX_INDEXES/3];

/*
WALKER: vertices at UV seams and hard edges are duplicated in the robot model.
Unwelded, every such edge looked like a silhouette edge, and thousands of
extra shadow quads were drawn per robot per frame, each in its own
glBegin/glEnd: the frame rate fell with every robot in view. Edges are now
matched by vertex position (weldIndex), and the silhouette quads of a surface
are collected once and drawn as one batch per stencil pass.
*/
#define	WELD_HASH_SIZE	1024
static	int			weldIndex[SHADER_MAX_VERTEXES];
static	int			weldHashHead[WELD_HASH_SIZE];
static	int			weldHashNext[SHADER_MAX_VERTEXES];
static	int			silhouette[SHADER_MAX_INDEXES][2];
static	int			numSilhouette;

static void R_WeldShadowVertexes( void ) {
	int		i, bucket, other;
	float	*xyz;

	for ( i = 0 ; i < WELD_HASH_SIZE ; i++ ) {
		weldHashHead[i] = -1;
	}
	for ( i = 0 ; i < tess.numVertexes ; i++ ) {
		xyz = tess.xyz[i];
		bucket = ( (int)floor( xyz[0] * 8 ) * 73856093 ^ (int)floor( xyz[1] * 8 ) * 19349663 ^ (int)floor( xyz[2] * 8 ) * 83492791 ) & ( WELD_HASH_SIZE - 1 );
		weldIndex[i] = i;
		for ( other = weldHashHead[bucket] ; other >= 0 ; other = weldHashNext[other] ) {
			if ( fabs( tess.xyz[other][0] - xyz[0] ) < 0.01f && fabs( tess.xyz[other][1] - xyz[1] ) < 0.01f
				&& fabs( tess.xyz[other][2] - xyz[2] ) < 0.01f ) {
				weldIndex[i] = weldIndex[other];
				break;
			}
		}
		weldHashNext[i] = weldHashHead[bucket];
		weldHashHead[bucket] = i;
	}
}

void R_AddEdgeDef( int i1, int i2, int facing ) {
	int		c;

	c = numEdgeDefs[ i1 ];
	if ( c == MAX_EDGE_DEFS ) {
		return;		// overflow
	}
	edgeDefs[ i1 ][ c ].i2 = i2;
	edgeDefs[ i1 ][ c ].facing = facing;

	numEdgeDefs[ i1 ]++;
}

void R_RenderShadowEdges( void ) {
	int		i;

#if 0
	int		numTris;

	// dumb way -- render every triangle's edges
	numTris = tess.numIndexes / 3;

	for ( i = 0 ; i < numTris ; i++ ) {
		int		i1, i2, i3;

		if ( !facing[i] ) {
			continue;
		}

		i1 = tess.indexes[ i*3 + 0 ];
		i2 = tess.indexes[ i*3 + 1 ];
		i3 = tess.indexes[ i*3 + 2 ];

		qglBegin( GL_TRIANGLE_STRIP );
		qglVertex3fv( tess.xyz[ i1 ] );
		qglVertex3fv( tess.xyz[ i1 + tess.numVertexes ] );
		qglVertex3fv( tess.xyz[ i2 ] );
		qglVertex3fv( tess.xyz[ i2 + tess.numVertexes ] );
		qglVertex3fv( tess.xyz[ i3 ] );
		qglVertex3fv( tess.xyz[ i3 + tess.numVertexes ] );
		qglVertex3fv( tess.xyz[ i1 ] );
		qglVertex3fv( tess.xyz[ i1 + tess.numVertexes ] );
		qglEnd();
	}
#else
	int		c, c2;
	int		j, k;
	int		i2;
	int		c_edges, c_rejected;
	int		hit[2];

	// an edge is NOT a silhouette edge if its face doesn't face the light,
	// or if it has a reverse paired edge that also faces the light.
	// A well behaved polyhedron would have exactly two faces for each edge,
	// but lots of models have dangling edges or overfanned edges
	c_edges = 0;
	c_rejected = 0;

	if ( numSilhouette < 0 ) {
		// collect once per surface (both stencil passes use the same list)
		numSilhouette = 0;
		for ( i = 0 ; i < tess.numVertexes ; i++ ) {
			c = numEdgeDefs[ i ];
			for ( j = 0 ; j < c ; j++ ) {
				if ( !edgeDefs[ i ][ j ].facing ) {
					continue;
				}

				hit[0] = 0;
				hit[1] = 0;

				i2 = edgeDefs[ i ][ j ].i2;
				c2 = numEdgeDefs[ i2 ];
				for ( k = 0 ; k < c2 ; k++ ) {
					if ( edgeDefs[ i2 ][ k ].i2 == i ) {
						hit[ edgeDefs[ i2 ][ k ].facing ]++;
					}
				}

				// if it doesn't share the edge with another front facing
				// triangle, it is a sil edge
				if ( hit[ 1 ] == 0 && numSilhouette < SHADER_MAX_INDEXES ) {
					silhouette[ numSilhouette ][0] = i;
					silhouette[ numSilhouette ][1] = i2;
					numSilhouette++;
					c_edges++;
				} else {
					c_rejected++;
				}
			}
		}
	}

	// edge indexes are welded ones: draw with the original vertex of the same position
	backEnd.pc.c_drawCalls++;	// WALKER: counted for the benchmark
	qglBegin( GL_QUADS );
	for ( i = 0 ; i < numSilhouette ; i++ ) {
		int		first = silhouette[i][0], second = silhouette[i][1];
		qglVertex3fv( tess.xyz[ first ] );
		qglVertex3fv( tess.xyz[ first + tess.numVertexes ] );
		qglVertex3fv( tess.xyz[ second + tess.numVertexes ] );
		qglVertex3fv( tess.xyz[ second ] );
	}
	qglEnd();
#endif
}

/*
=================
RB_ShadowTessEnd

triangleFromEdge[ v1 ][ v2 ]


  set triangle from edge( v1, v2, tri )
  if ( facing[ triangleFromEdge[ v1 ][ v2 ] ] && !facing[ triangleFromEdge[ v2 ][ v1 ] ) {
  }
=================
*/
void RB_ShadowTessEnd( void ) {
	int		i;
	int		numTris;
	vec3_t	lightDir;

	// WALKER: the quads of the surfaces with their topology are collected in world
	// coordinates and drawn once for the view (RB_FlushShadowQuads)
	if ( !tess.numIndexes ) {
		return;
	}

	// we can only do this if we have enough space in the vertex buffers
	if ( tess.numVertexes >= SHADER_MAX_VERTEXES / 2 ) {
		return;
	}

	if ( glConfig.stencilBits < 4 ) {
		return;
	}

	VectorCopy( backEnd.currentEntity->lightDir, lightDir );
	// WALKER: shadows always fall away from the sun, whatever the dynamic lights do
	if ( VectorLength( tr.sunLight ) > 0 ) {
		lightDir[0] = DotProduct( tr.sunDirection, backEnd.currentEntity->e.axis[0] );
		lightDir[1] = DotProduct( tr.sunDirection, backEnd.currentEntity->e.axis[1] );
		lightDir[2] = DotProduct( tr.sunDirection, backEnd.currentEntity->e.axis[2] );
	}

	// project vertexes away from light direction
	for ( i = 0 ; i < tess.numVertexes ; i++ ) {
		VectorMA( tess.xyz[i], -512, lightDir, tess.xyz[i+tess.numVertexes] );
	}

	// decide which triangles face the light
	Com_Memset( numEdgeDefs, 0, 4 * tess.numVertexes );
	R_WeldShadowVertexes();
	numSilhouette = -1;

	numTris = tess.numIndexes / 3;
	for ( i = 0 ; i < numTris ; i++ ) {
		int		i1, i2, i3;
		vec3_t	d1, d2, normal;
		float	*v1, *v2, *v3;
		float	d;

		i1 = tess.indexes[ i*3 + 0 ];
		i2 = tess.indexes[ i*3 + 1 ];
		i3 = tess.indexes[ i*3 + 2 ];

		v1 = tess.xyz[ i1 ];
		v2 = tess.xyz[ i2 ];
		v3 = tess.xyz[ i3 ];

		VectorSubtract( v2, v1, d1 );
		VectorSubtract( v3, v1, d2 );
		CrossProduct( d1, d2, normal );

		d = DotProduct( normal, lightDir );
		if ( d > 0 ) {
			facing[ i ] = 1;
		} else {
			facing[ i ] = 0;
		}

		// create the edges (between welded vertexes)
		i1 = weldIndex[ i1 ];
		i2 = weldIndex[ i2 ];
		i3 = weldIndex[ i3 ];
		R_AddEdgeDef( i1, i2, facing[ i ] );
		R_AddEdgeDef( i2, i3, facing[ i ] );
		R_AddEdgeDef( i3, i1, facing[ i ] );
	}

	// draw the silhouette edges

	GL_Bind( tr.whiteImage );
	qglEnable( GL_CULL_FACE );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	qglColor3f( 0.2f, 0.2f, 0.2f );

	// don't write to the color buffer
	qglColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );

	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );

	// mirrors have the culling order reversed
	if ( backEnd.viewParms.isMirror ) {
		qglCullFace( GL_FRONT );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_INCR );

		R_RenderShadowEdges();

		qglCullFace( GL_BACK );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_DECR );

		R_RenderShadowEdges();
	} else {
		qglCullFace( GL_BACK );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_INCR );

		R_RenderShadowEdges();

		qglCullFace( GL_FRONT );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_DECR );

		R_RenderShadowEdges();
	}


	// reenable writing to the color buffer
	qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
}


/*
===========================================================================
WALKER: shadow volumes of the models, cheaper (26.11.2026)

The robot surfaces are flat shaded: every triangle has its own three vertexes
(K7 legs: 7626 vertexes for 4088 triangles). The old way blended all of them
on the processor every frame, welded them again by position through a hash,
and searched the silhouette edges through per vertex lists. Now the topology
is worked out once when the model loads: the vertexes of one position in all
frames become one point, each edge knows its two triangles. Each frame only
the points are blended (about a quarter of the vertexes), the facing of the
triangles is computed and the edges whose triangles face differently give the
quads. The quads of all surfaces of an entity go to one array and are drawn
with two glDrawArrays (both stencil passes) instead of glBegin per surface.
===========================================================================
*/

typedef struct shadowMesh_s {
	const md3Surface_t		*surface;
	int						numPoints;
	int						*pointVertex;	// a vertex of the point (its frames are read)
	int						numTris;
	int						*triPoints;		// three points a triangle
	int						numEdges;
	int						*edges;			// point A, point B (as triangle 1 goes), triangle 1, triangle 2 or -1
	struct shadowMesh_s		*next;
} shadowMesh_t;

#define	SHADOW_MESH_HASH	1024
static shadowMesh_t		*shadowMeshHash[SHADOW_MESH_HASH];

#define	SHADOW_MAX_QUAD_VERTS	131072	// all robots of a view (1.5 MB)
static float			shadowQuadVerts[SHADOW_MAX_QUAD_VERTS][3];
static int				shadowNumQuadVerts;

static float			shadowPoints[MD3_MAX_VERTS][3];
static byte				shadowFacing[MD3_MAX_TRIANGLES];

static int ShadowMeshHashKey( const md3Surface_t *surface ) {
	return ( (size_t)surface >> 4 ) & ( SHADOW_MESH_HASH - 1 );
}

static shadowMesh_t *R_ShadowMeshFind( const md3Surface_t *surface ) {
	shadowMesh_t	*mesh;

	for ( mesh = shadowMeshHash[ShadowMeshHashKey( surface )] ; mesh ; mesh = mesh->next ) {
		if ( mesh->surface == surface ) {
			return mesh;
		}
	}
	return NULL;
}

/*
=================
R_ShadowBuildMesh

at the load of a model: points and edges of each surface (hunk memory, the
same life as the model)
=================
*/
void R_ShadowBuildMesh( md3Header_t *header ) {
	md3Surface_t	*surface;
	int				surfNum;

	if ( !header ) {
		return;
	}
	surface = (md3Surface_t *)( (byte *)header + header->ofsSurfaces );
	for ( surfNum = 0 ; surfNum < header->numSurfaces ; surfNum++,
		surface = (md3Surface_t *)( (byte *)surface + surface->ofsEnd ) ) {
		int				numVerts = surface->numVerts, numTris = surface->numTriangles;
		int				numFrames = surface->numFrames;
		short			*xyzNormals = (short *)( (byte *)surface + surface->ofsXyzNormals );
		int				*triangles = (int *)( (byte *)surface + surface->ofsTriangles );
		int				*vertexPoint, *hashHead, *hashNext, *edgeHead, *edgeNext, *edgePaired;
		int				vertex, other, tri, corner, frame, bucket;
		shadowMesh_t	*mesh;

		if ( numVerts <= 0 || numTris <= 0 || numVerts > MD3_MAX_VERTS || numTris > MD3_MAX_TRIANGLES
			|| R_ShadowMeshFind( surface ) ) {
			continue;
		}
		mesh = ri.Hunk_Alloc( sizeof( *mesh ), h_low );
		mesh->surface = surface;
		mesh->pointVertex = ri.Hunk_Alloc( numVerts * sizeof( int ), h_low );
		mesh->triPoints = ri.Hunk_Alloc( numTris * 3 * sizeof( int ), h_low );
		mesh->edges = ri.Hunk_Alloc( numTris * 3 * 4 * sizeof( int ), h_low );
		mesh->numTris = numTris;

		vertexPoint = ri.Hunk_AllocateTempMemory( numVerts * sizeof( int ) );
		hashHead = ri.Hunk_AllocateTempMemory( WELD_HASH_SIZE * sizeof( int ) );
		hashNext = ri.Hunk_AllocateTempMemory( numVerts * sizeof( int ) );

		// points: vertexes at the same place in every frame
		for ( bucket = 0 ; bucket < WELD_HASH_SIZE ; bucket++ ) {
			hashHead[bucket] = -1;
		}
		for ( vertex = 0 ; vertex < numVerts ; vertex++ ) {
			short	*xyz = xyzNormals + vertex * 4;

			bucket = ( xyz[0] * 73856093 ^ xyz[1] * 19349663 ^ xyz[2] * 83492791 ) & ( WELD_HASH_SIZE - 1 );
			vertexPoint[vertex] = -1;
			for ( other = hashHead[bucket] ; other >= 0 ; other = hashNext[other] ) {
				for ( frame = 0 ; frame < numFrames ; frame++ ) {
					short	*first = xyzNormals + ( frame * numVerts + vertex ) * 4;
					short	*second = xyzNormals + ( frame * numVerts + other ) * 4;
					if ( first[0] != second[0] || first[1] != second[1] || first[2] != second[2] ) {
						break;
					}
				}
				if ( frame == numFrames ) {
					vertexPoint[vertex] = vertexPoint[other];
					break;
				}
			}
			if ( vertexPoint[vertex] < 0 ) {
				vertexPoint[vertex] = mesh->numPoints;
				mesh->pointVertex[mesh->numPoints++] = vertex;
				hashNext[vertex] = hashHead[bucket];
				hashHead[bucket] = vertex;
			}
		}
		for ( tri = 0 ; tri < numTris * 3 ; tri++ ) {
			mesh->triPoints[tri] = vertexPoint[triangles[tri]];
		}
		ri.Hunk_FreeTempMemory( hashNext );
		ri.Hunk_FreeTempMemory( hashHead );
		ri.Hunk_FreeTempMemory( vertexPoint );

		// edges: a directed edge A->B of a triangle pairs with a free B->A of another
		edgeHead = ri.Hunk_AllocateTempMemory( mesh->numPoints * sizeof( int ) );
		edgeNext = ri.Hunk_AllocateTempMemory( numTris * 3 * sizeof( int ) );
		edgePaired = ri.Hunk_AllocateTempMemory( numTris * 3 * sizeof( int ) );
		for ( vertex = 0 ; vertex < mesh->numPoints ; vertex++ ) {
			edgeHead[vertex] = -1;		// edges listed by their point A
		}
		for ( tri = 0 ; tri < numTris ; tri++ ) {
			for ( corner = 0 ; corner < 3 ; corner++ ) {
				int		pointA = mesh->triPoints[tri * 3 + corner];
				int		pointB = mesh->triPoints[tri * 3 + ( corner + 1 ) % 3];
				int		edge;

				if ( pointA == pointB ) {
					continue;		// collapsed
				}
				for ( edge = edgeHead[pointB] ; edge >= 0 ; edge = edgeNext[edge] ) {
					if ( !edgePaired[edge] && mesh->edges[edge * 4 + 1] == pointA ) {
						break;
					}
				}
				if ( edge >= 0 ) {
					mesh->edges[edge * 4 + 3] = tri;
					edgePaired[edge] = 1;
					continue;
				}
				edge = mesh->numEdges++;
				mesh->edges[edge * 4 + 0] = pointA;
				mesh->edges[edge * 4 + 1] = pointB;
				mesh->edges[edge * 4 + 2] = tri;
				mesh->edges[edge * 4 + 3] = -1;
				edgePaired[edge] = 0;
				edgeNext[edge] = edgeHead[pointA];
				edgeHead[pointA] = edge;
			}
		}
		ri.Hunk_FreeTempMemory( edgePaired );
		ri.Hunk_FreeTempMemory( edgeNext );
		ri.Hunk_FreeTempMemory( edgeHead );

		bucket = ShadowMeshHashKey( surface );
		mesh->next = shadowMeshHash[bucket];
		shadowMeshHash[bucket] = mesh;
	}
}

void R_ShadowShutdownMeshes( void ) {
	Com_Memset( shadowMeshHash, 0, sizeof( shadowMeshHash ) );
	shadowNumQuadVerts = 0;
}

static void RB_ShadowLightDir( vec3_t lightDir ) {
	VectorCopy( backEnd.currentEntity->lightDir, lightDir );
	// shadows always fall away from the sun, whatever the dynamic lights do;
	// in the hangar away from its key light (the light direction of the entity)
	if ( VectorLength( tr.sunLight ) > 0 && !( backEnd.refdef.rdflags & RDF_STUDIOLIGHT ) ) {
		lightDir[0] = DotProduct( tr.sunDirection, backEnd.currentEntity->e.axis[0] );
		lightDir[1] = DotProduct( tr.sunDirection, backEnd.currentEntity->e.axis[1] );
		lightDir[2] = DotProduct( tr.sunDirection, backEnd.currentEntity->e.axis[2] );
	}
}

/*
=================
RB_DrawShadowQuads

both stencil passes over the collected quads of the entity
=================
*/
static void RB_DrawShadowQuads( void ) {
	if ( !shadowNumQuadVerts ) {
		return;
	}
	GL_Bind( tr.whiteImage );
	qglEnable( GL_CULL_FACE );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	qglColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );

	// only the vertex array may stay on: the others are as long as the tess arrays
	if ( qglActiveTextureARB ) {
		GL_SelectTexture( 1 );
		qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
		GL_SelectTexture( 0 );
	}
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	qglDisableClientState( GL_COLOR_ARRAY );
	qglLoadMatrixf( backEnd.viewParms.world.modelMatrix );		// the quads are in world coordinates
	qglVertexPointer( 3, GL_FLOAT, 0, shadowQuadVerts );

	qglCullFace( backEnd.viewParms.isMirror ? GL_FRONT : GL_BACK );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_INCR );
	qglDrawArrays( GL_QUADS, 0, shadowNumQuadVerts );
	qglCullFace( backEnd.viewParms.isMirror ? GL_BACK : GL_FRONT );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_DECR );
	qglDrawArrays( GL_QUADS, 0, shadowNumQuadVerts );
	backEnd.pc.c_drawCalls += 2;

	qglVertexPointer( 3, GL_FLOAT, 16, tess.xyz );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	qglLoadMatrixf( backEnd.or.modelMatrix );		// back to the current entity
	glState.faceCulling = -1;		// the next GL_Cull sets the culling again
	shadowNumQuadVerts = 0;
}

/*
=================
RB_FlushShadowQuads

WALKER: the shadow volumes of all models of the view: two draws (both stencil
passes) at the end of the view instead of two for every part of every robot
(each part is an entity: legs, torso, head, three guns)
=================
*/
void RB_FlushShadowQuads( void ) {
	RB_DrawShadowQuads();
}

// a point of the current entity into world coordinates (as its model matrix)
static void RB_ShadowToWorld( const vec3_t local, float *world ) {
	world[0] = backEnd.or.origin[0] + local[0] * backEnd.or.axis[0][0] + local[1] * backEnd.or.axis[1][0] + local[2] * backEnd.or.axis[2][0];
	world[1] = backEnd.or.origin[1] + local[0] * backEnd.or.axis[0][1] + local[1] * backEnd.or.axis[1][1] + local[2] * backEnd.or.axis[2][1];
	world[2] = backEnd.or.origin[2] + local[0] * backEnd.or.axis[0][2] + local[1] * backEnd.or.axis[1][2] + local[2] * backEnd.or.axis[2][2];
}

/*
=================
RB_MeshShadow

a surface of the shadow shader: its silhouette quads into the array of the
entity; qfalse when the surface has no topology (the old way then)
=================
*/
qboolean RB_MeshShadow( md3Surface_t *surface ) {
	shadowMesh_t	*mesh;
	short			*newXyz, *oldXyz;
	float			newScale, oldScale, backlerp;
	vec3_t			lightDir, offset;
	int				point, tri, edge, frame, oldFrame;

	if ( glConfig.stencilBits < 4 ) {
		return qtrue;
	}
	mesh = R_ShadowMeshFind( surface );
	if ( !mesh ) {
		return qfalse;
	}
	frame = backEnd.currentEntity->e.frame;
	oldFrame = backEnd.currentEntity->e.oldframe;
	if ( frame < 0 || frame >= surface->numFrames ) {
		frame = 0;
	}
	if ( oldFrame < 0 || oldFrame >= surface->numFrames ) {
		oldFrame = frame;
	}
	backlerp = frame == oldFrame ? 0 : backEnd.currentEntity->e.backlerp;
	newScale = MD3_XYZ_SCALE * ( 1.0f - backlerp );
	oldScale = MD3_XYZ_SCALE * backlerp;
	newXyz = (short *)( (byte *)surface + surface->ofsXyzNormals ) + frame * surface->numVerts * 4;
	oldXyz = (short *)( (byte *)surface + surface->ofsXyzNormals ) + oldFrame * surface->numVerts * 4;

	// the points of this frame
	for ( point = 0 ; point < mesh->numPoints ; point++ ) {
		int		vertex = mesh->pointVertex[point] * 4;
		shadowPoints[point][0] = newXyz[vertex + 0] * newScale + oldXyz[vertex + 0] * oldScale;
		shadowPoints[point][1] = newXyz[vertex + 1] * newScale + oldXyz[vertex + 1] * oldScale;
		shadowPoints[point][2] = newXyz[vertex + 2] * newScale + oldXyz[vertex + 2] * oldScale;
	}

	// the triangles facing the light
	RB_ShadowLightDir( lightDir );
	for ( tri = 0 ; tri < mesh->numTris ; tri++ ) {
		float	*first = shadowPoints[mesh->triPoints[tri * 3 + 0]];
		float	*second = shadowPoints[mesh->triPoints[tri * 3 + 1]];
		float	*third = shadowPoints[mesh->triPoints[tri * 3 + 2]];
		vec3_t	side1, side2, normal;

		VectorSubtract( second, first, side1 );
		VectorSubtract( third, first, side2 );
		CrossProduct( side1, side2, normal );
		shadowFacing[tri] = DotProduct( normal, lightDir ) > 0;
	}

	// the edges between a facing and a not facing triangle (or a facing one alone)
	VectorScale( lightDir, -512, offset );
	for ( edge = 0 ; edge < mesh->numEdges ; edge++ ) {
		int		*def = mesh->edges + edge * 4;
		int		facing1 = shadowFacing[def[2]];
		int		facing2 = def[3] >= 0 ? shadowFacing[def[3]] : 0;
		float	*from, *to;
		vec3_t	fromFar, toFar;

		if ( facing1 == facing2 ) {
			continue;
		}
		if ( shadowNumQuadVerts + 4 > SHADOW_MAX_QUAD_VERTS ) {
			RB_DrawShadowQuads();
		}
		// as the facing triangle goes round; into world coordinates
		from = shadowPoints[facing1 ? def[0] : def[1]];
		to = shadowPoints[facing1 ? def[1] : def[0]];
		VectorAdd( from, offset, fromFar );
		VectorAdd( to, offset, toFar );
		RB_ShadowToWorld( from, shadowQuadVerts[shadowNumQuadVerts + 0] );
		RB_ShadowToWorld( fromFar, shadowQuadVerts[shadowNumQuadVerts + 1] );
		RB_ShadowToWorld( toFar, shadowQuadVerts[shadowNumQuadVerts + 2] );
		RB_ShadowToWorld( to, shadowQuadVerts[shadowNumQuadVerts + 3] );
		shadowNumQuadVerts += 4;
	}
	return qtrue;
}


/*
=================
RB_ShadowFinish

Darken everything that is is a shadow volume.
We have to delay this until everything has been shadowed,
because otherwise shadows from different body parts would
overlap and double darken.
=================
*/
void RB_ShadowFinish( qboolean sunShadows ) {
	if ( r_shadows->integer != 2 && !sunShadows && !( backEnd.refdef.rdflags & RDF_STUDIOLIGHT ) ) {
		return;
	}
	if ( glConfig.stencilBits < 4 ) {
		return;
	}
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_NOTEQUAL, 0, 255 );

	qglDisable (GL_CLIP_PLANE0);
	qglDisable (GL_CULL_FACE);

	// WALKER: a plain multiply by 0.6 (the rock shadows on the water darken as much):
	// no second texture unit, no fog and no texture left over from the last shader
	if ( qglActiveTextureARB ) {
		GL_SelectTexture( 1 );
		qglDisable( GL_TEXTURE_2D );
		GL_SelectTexture( 0 );
	}
	qglDisable( GL_FOG );
	GL_Bind( tr.whiteImage );
	GL_TexEnv( GL_MODULATE );

    qglLoadIdentity ();

	// WALKER: in the hangar as dark as the sliders make it (only the ambient light is left
	// in a shadow, tr_light.c)
	if ( backEnd.refdef.rdflags & RDF_STUDIOLIGHT ) {
		float	factor = R_StudioShadowFactor();
		qglColor3f( factor, factor, factor );
	} else {
		qglColor3f( 0.6f, 0.6f, 0.6f );
	}
	GL_State( GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO );

//	qglColor3f( 1, 0, 0 );
//	GL_State( GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );

	backEnd.pc.c_drawCalls++;	// WALKER: counted for the benchmark
	qglBegin( GL_QUADS );
	qglVertex3f( -100, 100, -10 );
	qglVertex3f( 100, 100, -10 );
	qglVertex3f( 100, -100, -10 );
	qglVertex3f( -100, -100, -10 );
	qglEnd ();

	qglColor4f(1,1,1,1);
	qglDisable( GL_STENCIL_TEST );
}


/*
=================
RB_ProjectionShadowDeform

=================
*/
void RB_ProjectionShadowDeform( void ) {
	float	*xyz;
	int		i;
	float	h;
	vec3_t	ground;
	vec3_t	light;
	float	groundDist;
	float	d;
	vec3_t	lightDir;

	xyz = ( float * ) tess.xyz;

	ground[0] = backEnd.or.axis[0][2];
	ground[1] = backEnd.or.axis[1][2];
	ground[2] = backEnd.or.axis[2][2];

	groundDist = backEnd.or.origin[2] - backEnd.currentEntity->e.shadowPlane;

	VectorCopy( backEnd.currentEntity->lightDir, lightDir );
	// WALKER: shadows always fall away from the sun, whatever the dynamic lights do
	if ( VectorLength( tr.sunLight ) > 0 ) {
		lightDir[0] = DotProduct( tr.sunDirection, backEnd.currentEntity->e.axis[0] );
		lightDir[1] = DotProduct( tr.sunDirection, backEnd.currentEntity->e.axis[1] );
		lightDir[2] = DotProduct( tr.sunDirection, backEnd.currentEntity->e.axis[2] );
	}
	d = DotProduct( lightDir, ground );
	// don't let the shadows get too long or go negative
	if ( d < 0.5 ) {
		VectorMA( lightDir, (0.5 - d), ground, lightDir );
		d = DotProduct( lightDir, ground );
	}
	d = 1.0 / d;

	light[0] = lightDir[0] * d;
	light[1] = lightDir[1] * d;
	light[2] = lightDir[2] * d;

	for ( i = 0; i < tess.numVertexes; i++, xyz += 4 ) {
		h = DotProduct( xyz, ground ) + groundDist;

		xyz[0] -= light[0] * h;
		xyz[1] -= light[1] * h;
		xyz[2] -= light[2] * h;
	}
}
