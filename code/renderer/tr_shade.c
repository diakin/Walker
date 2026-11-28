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
// tr_shade.c

#include <stddef.h>	// WALKER: offsetof
#include "tr_local.h"

/*

  THIS ENTIRE FILE IS BACK END

  This file deals with applying shaders to surface data in the tess struct.
*/

/*
================
R_ArrayElementDiscrete

This is just for OpenGL conformance testing, it should never be the fastest
================
*/
static void APIENTRY R_ArrayElementDiscrete( GLint index ) {
	qglColor4ubv( tess.svars.colors[ index ] );
	if ( glState.currenttmu ) {
		qglMultiTexCoord2fARB( 0, tess.svars.texcoords[ 0 ][ index ][0], tess.svars.texcoords[ 0 ][ index ][1] );
		qglMultiTexCoord2fARB( 1, tess.svars.texcoords[ 1 ][ index ][0], tess.svars.texcoords[ 1 ][ index ][1] );
	} else {
		qglTexCoord2fv( tess.svars.texcoords[ 0 ][ index ] );
	}
	qglVertex3fv( tess.xyz[ index ] );
}

/*
===================
R_DrawStripElements

===================
*/
static int		c_vertexes;		// for seeing how long our average strips are
static int		c_begins;
static void R_DrawStripElements( int numIndexes, const glIndex_t *indexes, void ( APIENTRY *element )(GLint) ) {
	int i;
	int last[3] = { -1, -1, -1 };
	qboolean even;

	c_begins++;

	if ( numIndexes <= 0 ) {
		return;
	}

	qglBegin( GL_TRIANGLE_STRIP );

	// prime the strip
	element( indexes[0] );
	element( indexes[1] );
	element( indexes[2] );
	c_vertexes += 3;

	last[0] = indexes[0];
	last[1] = indexes[1];
	last[2] = indexes[2];

	even = qfalse;

	for ( i = 3; i < numIndexes; i += 3 )
	{
		// odd numbered triangle in potential strip
		if ( !even )
		{
			// check previous triangle to see if we're continuing a strip
			if ( ( indexes[i+0] == last[2] ) && ( indexes[i+1] == last[1] ) )
			{
				element( indexes[i+2] );
				c_vertexes++;
				assert( indexes[i+2] < tess.numVertexes );
				even = qtrue;
			}
			// otherwise we're done with this strip so finish it and start
			// a new one
			else
			{
				qglEnd();

				qglBegin( GL_TRIANGLE_STRIP );
				c_begins++;

				element( indexes[i+0] );
				element( indexes[i+1] );
				element( indexes[i+2] );

				c_vertexes += 3;

				even = qfalse;
			}
		}
		else
		{
			// check previous triangle to see if we're continuing a strip
			if ( ( last[2] == indexes[i+1] ) && ( last[0] == indexes[i+0] ) )
			{
				element( indexes[i+2] );
				c_vertexes++;

				even = qfalse;
			}
			// otherwise we're done with this strip so finish it and start
			// a new one
			else
			{
				qglEnd();

				qglBegin( GL_TRIANGLE_STRIP );
				c_begins++;

				element( indexes[i+0] );
				element( indexes[i+1] );
				element( indexes[i+2] );
				c_vertexes += 3;

				even = qfalse;
			}
		}

		// cache the last three vertices
		last[0] = indexes[i+0];
		last[1] = indexes[i+1];
		last[2] = indexes[i+2];
	}

	qglEnd();
}



/*
==================
R_DrawElements

Optionally performs our own glDrawElements that looks for strip conditions
instead of using the single glDrawElements call that may be inefficient
without compiled vertex arrays.
==================
*/
static void R_DrawElements( int numIndexes, const glIndex_t *indexes ) {
	int		primitives;

	backEnd.pc.c_drawCalls++;

	primitives = r_primitives->integer;

	// default is to use triangles if compiled vertex arrays are present
	if ( primitives == 0 ) {
		if ( qglLockArraysEXT ) {
			primitives = 2;
		} else {
			primitives = 1;
		}
	}


	if ( primitives == 2 ) {
		qglDrawElements( GL_TRIANGLES, 
						numIndexes,
						GL_INDEX_TYPE,
						indexes );
		return;
	}

	if ( primitives == 1 ) {
		R_DrawStripElements( numIndexes,  indexes, qglArrayElement );
		return;
	}
	
	if ( primitives == 3 ) {
		R_DrawStripElements( numIndexes,  indexes, R_ArrayElementDiscrete );
		return;
	}

	// anything else will cause no drawing
}


/*
=============================================================

SURFACE SHADERS

=============================================================
*/

shaderCommands_t	tess;
static qboolean	setArraysOnce;

/*
=================
R_BindAnimatedImage

=================
*/
static void R_BindAnimatedImage( textureBundle_t *bundle ) {
	int		index;

	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic(bundle->videoMapHandle);
		ri.CIN_UploadCinematic(bundle->videoMapHandle);
		return;
	}

	if ( bundle->numImageAnimations <= 1 ) {
		GL_Bind( bundle->image[0] );
		return;
	}

	// it is necessary to do this messy calc to make sure animations line up
	// exactly with waveforms of the same frequency
	index = myftol( tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE );
	index >>= FUNCTABLE_SIZE2;

	if ( index < 0 ) {
		index = 0;	// may happen with shader time offsets
	}
	index %= bundle->numImageAnimations;

	GL_Bind( bundle->image[ index ] );
}

/*
================
DrawTris

Draws triangle outlines for debugging
================
*/
static void DrawTris (shaderCommands_t *input) {
	GL_Bind( tr.whiteImage );
	qglColor3f (1,1,1);

	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );
	qglDepthRange( 0, 0 );

	qglDisableClientState (GL_COLOR_ARRAY);
	qglDisableClientState (GL_TEXTURE_COORD_ARRAY);

	qglVertexPointer (3, GL_FLOAT, 16, input->xyz);	// padded for SIMD

	if (qglLockArraysEXT) {
		qglLockArraysEXT(0, input->numVertexes);
		GLimp_LogComment( "glLockArraysEXT\n" );
	}

	R_DrawElements( input->numIndexes, input->indexes );

	if (qglUnlockArraysEXT) {
		qglUnlockArraysEXT();
		GLimp_LogComment( "glUnlockArraysEXT\n" );
	}
	qglDepthRange( 0, 1 );
}


/*
================
DrawNormals

Draws vertex normals for debugging
================
*/
static void DrawNormals (shaderCommands_t *input) {
	int		i;
	vec3_t	temp;

	GL_Bind( tr.whiteImage );
	qglColor3f (1,1,1);
	qglDepthRange( 0, 0 );	// never occluded
	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );

	qglBegin (GL_LINES);
	for (i = 0 ; i < input->numVertexes ; i++) {
		qglVertex3fv (input->xyz[i]);
		VectorMA (input->xyz[i], 2, input->normal[i], temp);
		qglVertex3fv (temp);
	}
	qglEnd ();

	qglDepthRange( 0, 1 );
}

/*
==============
RB_BeginSurface

We must set some things up before beginning any tesselation,
because a surface may be forced to perform a RB_End due
to overflow.
==============
*/
/*
===================
WALKER: drawing from the map's buffers (tr_vbo.c)
===================
*/

static qboolean	RB_HardwareFogBegin( void );
static void		RB_HardwareFogStage( int stateBits );
static void		RB_HardwareFogEnd( void );

// a stage whose colours and texture coordinates OpenGL can take as they are
static qboolean RB_StageVBOCapable( shaderStage_t *stage ) {
	qboolean	vertexColors;
	int			bundleNum, modNum;

	switch ( stage->rgbGen ) {
	case CGEN_IDENTITY:
	case CGEN_IDENTITY_LIGHTING:
	case CGEN_CONST:
	case CGEN_WAVEFORM:
		vertexColors = qfalse;
		break;
	case CGEN_EXACT_VERTEX:
		vertexColors = qtrue;
		break;
	case CGEN_VERTEX:
		if ( tr.identityLight != 1 ) {
			return qfalse;		// scaled per vertex
		}
		vertexColors = qtrue;
		break;
	default:
		return qfalse;
	}
	switch ( stage->alphaGen ) {
	case AGEN_SKIP:
	case AGEN_IDENTITY:		// with vertex colours: the vertex alpha (255 in maps)
		break;
	case AGEN_CONST:
	case AGEN_WAVEFORM:
		if ( vertexColors ) {
			return qfalse;
		}
		break;
	case AGEN_VERTEX:
		if ( !vertexColors ) {
			return qfalse;
		}
		break;
	default:
		return qfalse;
	}

	for ( bundleNum = 0; bundleNum < NUM_TEXTURE_BUNDLES; bundleNum++ ) {
		textureBundle_t	*bundle = &stage->bundle[bundleNum];

		if ( bundleNum > 0 && !bundle->image[0] ) {
			break;
		}
		if ( bundle->tcGen != TCGEN_TEXTURE && bundle->tcGen != TCGEN_LIGHTMAP && bundle->tcGen != TCGEN_VECTOR ) {
			return qfalse;
		}
		for ( modNum = 0; modNum < bundle->numTexMods; modNum++ ) {
			texMod_t	type = bundle->texMods[modNum].type;
			if ( type == TMOD_NONE ) {
				break;
			}
			if ( type != TMOD_SCALE && type != TMOD_SCROLL && type != TMOD_TRANSFORM && type != TMOD_ROTATE ) {
				return qfalse;
			}
		}
	}
	return qtrue;
}

static qboolean RB_ShaderVBOCheck( shader_t *shader ) {
	int		stageNum, numStages = 0;

	shader->vboCapable = -1;
	if ( shader == tr.shadowShader || shader->isSky || shader->numUnfoggedPasses <= 0
		|| shader->optimalStageIteratorFunc == RB_StageIteratorSky ) {
		return qfalse;
	}
	for ( stageNum = 0; stageNum < MAX_SHADER_STAGES; stageNum++ ) {
		if ( !shader->stages[stageNum] ) {
			break;
		}
		if ( !RB_StageVBOCapable( shader->stages[stageNum] ) ) {
			return qfalse;
		}
		numStages++;
	}
	if ( !numStages ) {
		return qfalse;
	}
	// deformVertexes lodshift alone (buildings in two detail levels): it moves nothing
	// outside the band of r_lodDistance, so a surface wholly outside it can come from the
	// buffers (RB_VBOLodShiftStill), the others the old way
	if ( shader->numDeforms == 1 && shader->deforms[0].deformation == DEFORM_LODSHIFT ) {
		shader->vboCapable = 2;
		return qtrue;
	}
	if ( shader->numDeforms ) {
		return qfalse;
	}
	shader->vboCapable = 1;
	return qtrue;
}

// a surface of a lodshift shader that the deform leaves where it is in this view
qboolean RB_VBOLodShiftStill( vec3_t bounds[2] ) {
	return R_LodShiftStill( &tess.shader->deforms[0], bounds );
}

qboolean R_LodShiftStill( const deformStage_t *deform, vec3_t bounds[2] ) {
	float			band = r_lodBand->value < 1 ? 1 : r_lodBand->value;
	float			start = r_lodDistance->value - band * 0.5f;
	vec3_t			origin;
	float			radius, distance;

	VectorAdd( bounds[0], bounds[1], origin );
	VectorScale( origin, 0.5f, origin );
	radius = RadiusFromBounds( bounds[0], bounds[1] );
	distance = Distance( origin, backEnd.viewParms.or.origin );

	if ( R_LodShiftDepth( deform ) == 0 ) {
		return qtrue;
	}
	if ( deform->moveVector[0] ) {
		return distance - radius >= start + band;	// far level: still beyond the band
	}
	return distance + radius <= start;				// near level: still before it
}

// 0: the old way, 1: from the buffers, 2: from the buffers where lodshift is still
static int RB_ShaderVBOCapable( shader_t *shader ) {
	if ( !shader->vboCapable && !RB_ShaderVBOCheck( shader ) && r_vbo->integer == 2 && shader->lightmapIndex != LIGHTMAP_2D ) {
		ri.Printf( PRINT_ALL, "VBO: the old way: %s\n", shader->name );	// r_vbo 2: map shaders left out, once each
	}
	return shader->vboCapable > 0 ? shader->vboCapable : 0;
}

// this batch: the shader, and no fog pass or colours faded by the fog on the processor
static int RB_VBOAllowed( shader_t *shader, int fogNum ) {
	int		capable;

	if ( !R_VBOWorldActive() || backEnd.projection2D ) {
		return 0;
	}
	capable = RB_ShaderVBOCapable( shader );
	if ( !capable ) {
		return 0;
	}
	if ( fogNum && r_drawfog->integer && !backEnd.viewParms.isPortal ) {
		// only the OpenGL fog of RB_HardwareFogBegin, it works on the buffers as well
		if ( r_drawfog->integer != 1 || !shader->fogPass || !tr.world || tr.world->fogs[fogNum].hasSurface ) {
			return 0;
		}
	}
	return capable;
}

void RB_AddVBORange( int firstIndex, int numIndexes ) {
	const void	*offset = (const void *)( (size_t)firstIndex * sizeof( glIndex_t ) );

	if ( tess.numVboRanges > 0 ) {
		int		last = tess.numVboRanges - 1;
		// joins the previous range when it goes straight on
		if ( (const byte *)tess.vboOffsets[last] + tess.vboCounts[last] * sizeof( glIndex_t ) == (const byte *)offset ) {
			tess.vboCounts[last] += numIndexes;
			tess.vboIndexes += numIndexes;
			return;
		}
	}
	if ( tess.numVboRanges >= MAX_VBO_RANGES ) {
		shader_t	*shader = tess.shader;
		int			fogNum = tess.fogNum;
		RB_EndSurface();
		RB_BeginSurface( shader, fogNum );
	}
	tess.vboOffsets[tess.numVboRanges] = offset;
	tess.vboCounts[tess.numVboRanges] = numIndexes;
	tess.numVboRanges++;
	tess.vboIndexes += numIndexes;
}

// one colour for the stage; qfalse: the colours of the vertexes
static qboolean RB_VBOStageColor( shaderStage_t *stage, byte *color ) {
	int		savedVertexes = tess.numVertexes;
	byte	wave[4][4];

	switch ( stage->rgbGen ) {
	case CGEN_IDENTITY:
		color[0] = color[1] = color[2] = color[3] = 255;
		break;
	case CGEN_IDENTITY_LIGHTING:
		color[0] = color[1] = color[2] = color[3] = tr.identityLightByte;
		break;
	case CGEN_CONST:
		*(int *)color = *(int *)stage->constantColor;
		break;
	case CGEN_WAVEFORM:
		tess.numVertexes = 1;	// the same colour for every vertex: work it out once
		RB_CalcWaveColor( &stage->rgbWave, wave[0] );
		tess.numVertexes = savedVertexes;
		*(int *)color = *(int *)wave[0];
		break;
	default:
		return qfalse;
	}
	switch ( stage->alphaGen ) {
	case AGEN_IDENTITY:
		if ( stage->rgbGen != CGEN_IDENTITY ) {
			color[3] = 255;
		}
		break;
	case AGEN_CONST:
		color[3] = stage->constantColor[3];
		break;
	case AGEN_WAVEFORM:
		tess.numVertexes = 1;
		*(int *)wave[0] = *(int *)color;
		RB_CalcWaveAlpha( &stage->alphaWave, wave[0] );
		tess.numVertexes = savedVertexes;
		color[3] = wave[0][3];
		break;
	default:
		break;
	}
	return qtrue;
}

// texture coordinates of a bundle on the selected unit: from the buffer or texgen,
// the tcMods in the texture matrix; qtrue: the matrix was changed
static qboolean RB_VBOTexCoords( textureBundle_t *bundle ) {
	float	matrix[16];
	float	a00 = 1, a01 = 0, a10 = 0, a11 = 1, tx = 0, ty = 0;
	int		modNum;

	if ( bundle->tcGen == TCGEN_VECTOR ) {
		float	planeS[4], planeT[4];
		VectorCopy( bundle->tcGenVectors[0], planeS );
		VectorCopy( bundle->tcGenVectors[1], planeT );
		planeS[3] = planeT[3] = 0;
		qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
		qglTexGeni( GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
		qglTexGeni( GL_T, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
		qglTexGenfv( GL_S, GL_OBJECT_PLANE, planeS );
		qglTexGenfv( GL_T, GL_OBJECT_PLANE, planeT );
		qglEnable( GL_TEXTURE_GEN_S );
		qglEnable( GL_TEXTURE_GEN_T );
	} else {
		qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
		qglTexCoordPointer( 2, GL_FLOAT, sizeof( vboVertex_t ),
			bundle->tcGen == TCGEN_LIGHTMAP ? (void *)offsetof( vboVertex_t, lightmap ) : (void *)offsetof( vboVertex_t, st ) );
	}

	if ( !bundle->numTexMods ) {
		return qfalse;
	}
	// st' = A st + T after every tcMod in turn, as ComputeTexCoords does them
	for ( modNum = 0; modNum < bundle->numTexMods; modNum++ ) {
		texModInfo_t	*mod = &bundle->texMods[modNum];
		float			m00, m01, m10, m11, mx, my, n00, n01, n10, n11, nx, ny;

		if ( mod->type == TMOD_NONE ) {
			break;
		}
		switch ( mod->type ) {
		case TMOD_SCALE:
			m00 = mod->scale[0]; m01 = 0; m10 = 0; m11 = mod->scale[1]; mx = my = 0;
			break;
		case TMOD_SCROLL:
			mx = mod->scroll[0] * tess.shaderTime;
			my = mod->scroll[1] * tess.shaderTime;
			mx -= floor( mx );
			my -= floor( my );
			m00 = 1; m01 = 0; m10 = 0; m11 = 1;
			break;
		case TMOD_TRANSFORM:
			// s' = s * m[0][0] + t * m[1][0] + trans[0], t' = s * m[0][1] + t * m[1][1] + trans[1]
			m00 = mod->matrix[0][0]; m01 = mod->matrix[1][0]; mx = mod->translate[0];
			m10 = mod->matrix[0][1]; m11 = mod->matrix[1][1]; my = mod->translate[1];
			break;
		case TMOD_ROTATE:
			{
				float	degs = -mod->rotateSpeed * tess.shaderTime;
				int		tableIndex = degs * ( FUNCTABLE_SIZE / 360.0f );
				float	sinValue = tr.sinTable[tableIndex & FUNCTABLE_MASK];
				float	cosValue = tr.sinTable[( tableIndex + FUNCTABLE_SIZE / 4 ) & FUNCTABLE_MASK];
				m00 = cosValue; m01 = -sinValue; mx = 0.5f - 0.5f * cosValue + 0.5f * sinValue;
				m10 = sinValue; m11 = cosValue; my = 0.5f - 0.5f * sinValue - 0.5f * cosValue;
			}
			break;
		default:
			m00 = 1; m01 = 0; m10 = 0; m11 = 1; mx = my = 0;
			break;
		}
		n00 = m00 * a00 + m01 * a10;	n01 = m00 * a01 + m01 * a11;
		n10 = m10 * a00 + m11 * a10;	n11 = m10 * a01 + m11 * a11;
		nx = m00 * tx + m01 * ty + mx;	ny = m10 * tx + m11 * ty + my;
		a00 = n00; a01 = n01; a10 = n10; a11 = n11; tx = nx; ty = ny;
	}

	Com_Memset( matrix, 0, sizeof( matrix ) );
	matrix[0] = a00;	matrix[4] = a01;	matrix[12] = tx;
	matrix[1] = a10;	matrix[5] = a11;	matrix[13] = ty;
	matrix[10] = 1;
	matrix[15] = 1;
	qglMatrixMode( GL_TEXTURE );
	qglLoadMatrixf( matrix );
	qglMatrixMode( GL_MODELVIEW );
	return qtrue;
}

static void RB_VBOResetTexCoords( textureBundle_t *bundle, qboolean matrixChanged ) {
	if ( bundle->tcGen == TCGEN_VECTOR ) {
		qglDisable( GL_TEXTURE_GEN_S );
		qglDisable( GL_TEXTURE_GEN_T );
	}
	if ( matrixChanged ) {
		qglMatrixMode( GL_TEXTURE );
		qglLoadIdentity();
		qglMatrixMode( GL_MODELVIEW );
	}
}

static void RB_DrawWorldVBO( void ) {
	shader_t	*shader = tess.shader;
	int			stageNum;
	byte		color[4];
	qboolean	shiftProgram;

	GL_Cull( shader->cullType );
	if ( shader->polygonOffset ) {
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	// the second unit's texture coordinates may still be on (DrawMultitextured leaves
	// them on) with tess memory behind them: with indexes up to the size of the whole map
	// the driver would read far past that array
	GL_SelectTexture( 1 );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 0 );

	R_VBOBindWorld();
	qglVertexPointer( 3, GL_FLOAT, sizeof( vboVertex_t ), (void *)offsetof( vboVertex_t, xyz ) );

	// deformVertexes lodshift: the move by the vertex program (tr_glsl.c)
	shiftProgram = shader->vboCapable == 2 && GLSL_LodShiftAvailable();
	if ( shiftProgram ) {
		GLSL_LodShiftBegin( &shader->deforms[0] );
	}

	RB_HardwareFogBegin();
	for ( stageNum = 0; stageNum < MAX_SHADER_STAGES; stageNum++ ) {
		shaderStage_t	*stage = tess.xstages[stageNum];
		qboolean		matrix0, matrix1 = qfalse;

		if ( !stage ) {
			break;
		}

		if ( RB_VBOStageColor( stage, color ) ) {
			qglDisableClientState( GL_COLOR_ARRAY );
			qglColor4ubv( color );
		} else {
			qglEnableClientState( GL_COLOR_ARRAY );
			qglColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( vboVertex_t ), (void *)offsetof( vboVertex_t, color ) );
		}
		RB_HardwareFogStage( stage->stateBits );
		GL_State( stage->stateBits );

		GL_SelectTexture( 0 );
		if ( stage->bundle[0].vertexLightmap && ( (r_vertexLight->integer && !r_uiFullScreen->integer) || glConfig.hardwareType == GLHW_PERMEDIA2 ) && r_lightmap->integer ) {
			GL_Bind( tr.whiteImage );
		} else {
			R_BindAnimatedImage( &stage->bundle[0] );
		}
		matrix0 = RB_VBOTexCoords( &stage->bundle[0] );
		if ( shiftProgram ) {
			GLSL_LodShiftUnit( 0, stage->bundle[0].tcGen == TCGEN_VECTOR, stage->bundle[0].tcGenVectors[0], stage->bundle[0].tcGenVectors[1] );
			GLSL_LodShiftUnit( 1, qfalse, NULL, NULL );
		}

		if ( stage->bundle[1].image[0] ) {
			GL_SelectTexture( 1 );
			qglEnable( GL_TEXTURE_2D );
			GL_TexEnv( r_lightmap->integer ? GL_REPLACE : shader->multitextureEnv );
			R_BindAnimatedImage( &stage->bundle[1] );
			matrix1 = RB_VBOTexCoords( &stage->bundle[1] );
			if ( shiftProgram ) {
				GLSL_LodShiftUnit( 1, stage->bundle[1].tcGen == TCGEN_VECTOR, stage->bundle[1].tcGenVectors[0], stage->bundle[1].tcGenVectors[1] );
			}
		}

		R_VBODrawRanges( tess.numVboRanges, tess.vboCounts, tess.vboOffsets );

		if ( stage->bundle[1].image[0] ) {
			RB_VBOResetTexCoords( &stage->bundle[1], matrix1 );
			qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[1] );	// not into the buffer once it is unbound
			qglDisableClientState( GL_TEXTURE_COORD_ARRAY );	// see above; DrawMultitextured turns it on itself
			qglDisable( GL_TEXTURE_2D );
			GL_SelectTexture( 0 );
		}
		RB_VBOResetTexCoords( &stage->bundle[0], matrix0 );

		if ( r_lightmap->integer && ( stage->bundle[0].isLightmap || stage->bundle[1].isLightmap || stage->bundle[0].vertexLightmap ) ) {
			break;
		}
	}
	RB_HardwareFogEnd();
	if ( shiftProgram ) {
		GLSL_LodShiftEnd();
	}

	// the old way finds the arrays it expects, in its own memory
	R_VBOUnbind();
	qglVertexPointer( 3, GL_FLOAT, 16, tess.xyz );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );
	qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors );

	if ( shader->polygonOffset ) {
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
}


/*
===================
WALKER: models in video memory (tr_vbo.c, tr_glsl.c GLSL_Mesh*)
===================
*/
static qboolean	RB_HardwareFogOn( void );

// a stage of a model shader the program can draw
static qboolean RB_StageMeshCapable( shaderStage_t *stage ) {
	int		modNum;

	if ( stage->bundle[1].image[0] || stage->bundle[0].vertexLightmap || stage->bundle[0].isVideoMap ) {
		return qfalse;
	}
	if ( stage->bundle[0].tcGen != TCGEN_TEXTURE && stage->bundle[0].tcGen != TCGEN_ENVIRONMENT_MAPPED ) {
		return qfalse;
	}
	for ( modNum = 0; modNum < stage->bundle[0].numTexMods; modNum++ ) {
		if ( stage->bundle[0].texMods[modNum].type != TMOD_NONE ) {
			return qfalse;
		}
	}
	switch ( stage->rgbGen ) {
	case CGEN_LIGHTING_DIFFUSE:
	case CGEN_IDENTITY:
	case CGEN_IDENTITY_LIGHTING:
	case CGEN_CONST:
	case CGEN_WAVEFORM:
	case CGEN_ENTITY:
		break;
	default:
		return qfalse;
	}
	switch ( stage->alphaGen ) {
	case AGEN_SKIP:
	case AGEN_IDENTITY:
	case AGEN_CONST:
	case AGEN_WAVEFORM:
	case AGEN_ENTITY:
	case AGEN_LIGHTING_SPECULAR:
		break;
	default:
		return qfalse;
	}
	return qtrue;
}

static qboolean RB_ShaderMeshCapable( shader_t *shader ) {
	int		stageNum, numStages = 0;

	if ( shader->meshCapable ) {
		return shader->meshCapable > 0;
	}
	shader->meshCapable = -1;
	if ( shader == tr.shadowShader || shader->isSky || shader->numDeforms || shader->numUnfoggedPasses <= 0
		|| shader->optimalStageIteratorFunc == RB_StageIteratorSky ) {
		return qfalse;
	}
	for ( stageNum = 0; stageNum < MAX_SHADER_STAGES; stageNum++ ) {
		if ( !shader->stages[stageNum] ) {
			break;
		}
		if ( !RB_StageMeshCapable( shader->stages[stageNum] ) ) {
			if ( r_vbo->integer == 2 ) {
				ri.Printf( PRINT_ALL, "VBO: model shader the old way: %s\n", shader->name );
			}
			return qfalse;
		}
		numStages++;
	}
	if ( !numStages ) {
		return qfalse;
	}
	shader->meshCapable = 1;
	return qtrue;
}

static qboolean RB_MeshAllowed( shader_t *shader, int fogNum ) {
	if ( !r_vbo->integer || backEnd.projection2D || !GLSL_MeshAvailable() || !RB_ShaderMeshCapable( shader ) ) {
		return qfalse;
	}
	if ( fogNum && r_drawfog->integer && !backEnd.viewParms.isPortal ) {
		if ( r_drawfog->integer != 1 || !shader->fogPass || !tr.world || tr.world->fogs[fogNum].hasSurface ) {
			return qfalse;		// the fog pass needs the vertexes
		}
	}
	return qtrue;
}

// the colour of a stage for the program; *diffuse / *specular: computed per vertex instead
static void RB_MeshStageColor( shaderStage_t *stage, byte *color, qboolean *diffuse, qboolean *specular ) {
	int		savedVertexes = tess.numVertexes;
	byte	wave[4];

	*diffuse = qfalse;
	*specular = qfalse;
	switch ( stage->rgbGen ) {
	case CGEN_LIGHTING_DIFFUSE:
		*diffuse = qtrue;
		color[0] = color[1] = color[2] = color[3] = 255;
		break;
	case CGEN_IDENTITY:
		color[0] = color[1] = color[2] = color[3] = 255;
		break;
	case CGEN_IDENTITY_LIGHTING:
		color[0] = color[1] = color[2] = color[3] = tr.identityLightByte;
		break;
	case CGEN_CONST:
		*(int *)color = *(int *)stage->constantColor;
		break;
	case CGEN_WAVEFORM:
		tess.numVertexes = 1;
		RB_CalcWaveColor( &stage->rgbWave, wave );
		tess.numVertexes = savedVertexes;
		*(int *)color = *(int *)wave;
		break;
	case CGEN_ENTITY:
		*(int *)color = *(int *)backEnd.currentEntity->e.shaderRGBA;
		break;
	default:
		color[0] = color[1] = color[2] = color[3] = 255;
		break;
	}
	switch ( stage->alphaGen ) {
	case AGEN_IDENTITY:
		if ( stage->rgbGen != CGEN_IDENTITY ) {
			color[3] = 255;
		}
		break;
	case AGEN_CONST:
		if ( stage->rgbGen != CGEN_CONST ) {
			color[3] = stage->constantColor[3];
		}
		break;
	case AGEN_WAVEFORM:
		tess.numVertexes = 1;
		*(int *)wave = *(int *)color;
		RB_CalcWaveAlpha( &stage->alphaWave, wave );
		tess.numVertexes = savedVertexes;
		color[3] = wave[3];
		break;
	case AGEN_ENTITY:
		color[3] = backEnd.currentEntity->e.shaderRGBA[3];
		break;
	case AGEN_LIGHTING_SPECULAR:
		*specular = qtrue;
		break;
	default:
		break;
	}
}

static void RB_DrawMeshVBO( void ) {
	shader_t		*shader = tess.shader;
	meshModelVBO_t	*model = tess.meshModel;
	trRefEntity_t	*ent = backEnd.currentEntity;
	int				frame = ent->e.frame, oldFrame = ent->e.oldframe, stageNum;
	float			backLerp = frame == oldFrame ? 0 : ent->e.backlerp;
	byte			color[4];

	if ( frame < 0 || frame >= model->numFrames ) {
		frame = 0;
	}
	if ( oldFrame < 0 || oldFrame >= model->numFrames ) {
		oldFrame = 0;
	}

	GL_Cull( shader->cullType );
	if ( shader->polygonOffset ) {
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	// no fixed array may be read with the model's indexes (they point into tess memory)
	GL_SelectTexture( 1 );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 0 );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	qglDisableClientState( GL_COLOR_ARRAY );
	qglDisableClientState( GL_VERTEX_ARRAY );	// the position comes from generic attribute 0

	R_VBOBindMesh( model );

	// WALKER: a part with all its material classes in one pass (tr_glsl.c)
	if ( shader->materialMask && tess.xstages[0] && GLSL_MaterialAvailable() ) {
		float	*wave = shader->materialGlow;
		float	bumpScale = 0;
		float	glow = wave[0] + wave[1] * sin( ( wave[2] + wave[3] * tess.shaderTime ) * 2.0 * M_PI );

		RB_HardwareFogBegin();
		RB_HardwareFogStage( tess.xstages[0]->stateBits );
		GL_State( tess.xstages[0]->stateBits );
		GL_SelectTexture( 1 );
		GL_Bind( shader->materialEnv );
		GL_SelectTexture( 0 );
		qglActiveTextureARB( GL_TEXTURE0_ARB + 2 );		// unit 2: not tracked by GL_Bind
		qglBindTexture( GL_TEXTURE_2D, shader->materialMask->texnum );
		// unit 3: the relief map; its full range as a slope per atlas unit (the map's width)
		if ( shader->materialBump && GLSL_MaterialBumpAvailable() ) {
			bumpScale = shader->materialBumpRange * shader->materialBump->width;
			qglActiveTextureARB( GL_TEXTURE0_ARB + 3 );
			qglBindTexture( GL_TEXTURE_2D, shader->materialBump->texnum );
		}
		qglActiveTextureARB( GL_TEXTURE0_ARB );
		R_BindAnimatedImage( &tess.xstages[0]->bundle[0] );
		GLSL_MaterialBegin( model, frame, oldFrame, backLerp, glow, RB_HardwareFogOn(), bumpScale );
		R_VBODrawRanges( tess.numVboRanges, tess.vboCounts, tess.vboOffsets );
		RB_HardwareFogEnd();
		GLSL_MaterialEnd();
		R_VBOUnbind();
		goto restore;
	}

	GLSL_MeshBegin( model, frame, oldFrame, backLerp );
	RB_HardwareFogBegin();
	for ( stageNum = 0; stageNum < MAX_SHADER_STAGES; stageNum++ ) {
		shaderStage_t	*stage = tess.xstages[stageNum];
		qboolean		diffuse, specular;

		if ( !stage ) {
			break;
		}
		RB_MeshStageColor( stage, color, &diffuse, &specular );
		RB_HardwareFogStage( stage->stateBits );
		GL_State( stage->stateBits );
		R_BindAnimatedImage( &stage->bundle[0] );
		GLSL_MeshStage( diffuse, specular, stage->bundle[0].tcGen == TCGEN_ENVIRONMENT_MAPPED, color, RB_HardwareFogOn() );
		R_VBODrawRanges( tess.numVboRanges, tess.vboCounts, tess.vboOffsets );
	}
	RB_HardwareFogEnd();
	GLSL_MeshEnd();
	R_VBOUnbind();

restore:
	// back to the arrays the old way expects, in its own memory
	qglEnableClientState( GL_VERTEX_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, 16, tess.xyz );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );
	qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors );

	if ( shader->polygonOffset ) {
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
}


void RB_BeginSurface( shader_t *shader, int fogNum ) {

	shader_t *state = (shader->remappedShader) ? shader->remappedShader : shader;

	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.shader = state;
	tess.fogNum = fogNum;
	tess.dlightBits = 0;		// will be OR'd in by surface functions
	tess.xstages = state->stages;
	tess.numPasses = state->numUnfoggedPasses;
	tess.currentStageIteratorFunc = state->optimalStageIteratorFunc;

	tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	if (tess.shader->clampTime && tess.shaderTime >= tess.shader->clampTime) {
		tess.shaderTime = tess.shader->clampTime;
	}

	// WALKER: map surfaces of this batch may come from the map's buffers
	tess.numVboRanges = 0;
	tess.vboIndexes = 0;
	tess.vboAllowed = RB_VBOAllowed( state, fogNum );
	tess.meshModel = NULL;
	tess.meshAllowed = RB_MeshAllowed( state, fogNum );
}

/*
===================
DrawMultitextured

output = t0 * t1 or t0 + t1

t0 = most upstream according to spec
t1 = most downstream according to spec
===================
*/
static void DrawMultitextured( shaderCommands_t *input, int stage ) {
	shaderStage_t	*pStage;

	pStage = tess.xstages[stage];

	GL_State( pStage->stateBits );

	// this is an ugly hack to work around a GeForce driver
	// bug with multitexture and clip planes
	if ( backEnd.viewParms.isPortal ) {
		qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	}

	//
	// base
	//
	GL_SelectTexture( 0 );
	qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoords[0] );
	R_BindAnimatedImage( &pStage->bundle[0] );

	//
	// lightmap/secondary pass
	//
	GL_SelectTexture( 1 );
	qglEnable( GL_TEXTURE_2D );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );

	if ( r_lightmap->integer ) {
		GL_TexEnv( GL_REPLACE );
	} else {
		GL_TexEnv( tess.shader->multitextureEnv );
	}

	qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoords[1] );

	R_BindAnimatedImage( &pStage->bundle[1] );

	R_DrawElements( input->numIndexes, input->indexes );

	//
	// disable texturing on TEXTURE1, then select TEXTURE0
	//
	//qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	qglDisable( GL_TEXTURE_2D );

	GL_SelectTexture( 0 );
}



/*
===================
ProjectDlightTexture

Perform dynamic lighting with another rendering pass
===================
*/
static void ProjectDlightTexture( void ) {
	int		i, l;
#if idppc_altivec
	vec_t	origin0, origin1, origin2;
	float   texCoords0, texCoords1;
	vector float floatColorVec0, floatColorVec1;
	vector float modulateVec, colorVec, zero;
	vector short colorShort;
	vector signed int colorInt;
	vector unsigned char floatColorVecPerm, modulatePerm, colorChar;
	vector unsigned char vSel = (vector unsigned char)(0x00, 0x00, 0x00, 0xff,
							   0x00, 0x00, 0x00, 0xff,
							   0x00, 0x00, 0x00, 0xff,
							   0x00, 0x00, 0x00, 0xff);
#else
	vec3_t	origin;
#endif
	float	*texCoords;
	byte	*colors;
	byte	clipBits[SHADER_MAX_VERTEXES];
	MAC_STATIC float	texCoordsArray[SHADER_MAX_VERTEXES][2];
	byte	colorArray[SHADER_MAX_VERTEXES][4];
	unsigned	hitIndexes[SHADER_MAX_INDEXES];
	int		numIndexes;
	float	scale;
	float	radius;
	vec3_t	floatColor;
	float	modulate;

	if ( !backEnd.refdef.num_dlights ) {
		return;
	}

#if idppc_altivec
	// There has to be a better way to do this so that floatColor 
	// and/or modulate are already 16-byte aligned.
	floatColorVecPerm = vec_lvsl(0,(float *)floatColor);
	modulatePerm = vec_lvsl(0,(float *)&modulate);
	modulatePerm = (vector unsigned char)vec_splat((vector unsigned int)modulatePerm,0);
	zero = (vector float)vec_splat_s8(0);
#endif

	for ( l = 0 ; l < backEnd.refdef.num_dlights ; l++ ) {
		dlight_t	*dl;

		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;	// this surface definately doesn't have any of this light
		}
		texCoords = texCoordsArray[0];
		colors = colorArray[0];

		dl = &backEnd.refdef.dlights[l];
#if idppc_altivec
		origin0 = dl->transformed[0];
		origin1 = dl->transformed[1];
		origin2 = dl->transformed[2];
#else
		VectorCopy( dl->transformed, origin );
#endif
		radius = dl->radius;
		scale = 1.0f / radius;

		floatColor[0] = dl->color[0] * 255.0f;
		floatColor[1] = dl->color[1] * 255.0f;
		floatColor[2] = dl->color[2] * 255.0f;
#if idppc_altivec
		floatColorVec0 = vec_ld(0, floatColor);
		floatColorVec1 = vec_ld(11, floatColor);
		floatColorVec0 = vec_perm(floatColorVec0,floatColorVec0,floatColorVecPerm);
#endif
		for ( i = 0 ; i < tess.numVertexes ; i++, texCoords += 2, colors += 4 ) {
#if idppc_altivec
			vec_t dist0, dist1, dist2;
#else
			vec3_t	dist;
#endif
			int		clip;

			backEnd.pc.c_dlightVertexes++;

#if idppc_altivec
			//VectorSubtract( origin, tess.xyz[i], dist );
			dist0 = origin0 - tess.xyz[i][0];
			dist1 = origin1 - tess.xyz[i][1];
			dist2 = origin2 - tess.xyz[i][2];
			texCoords0 = 0.5f + dist0 * scale;
			texCoords1 = 0.5f + dist1 * scale;

			clip = 0;
			if ( texCoords0 < 0.0f ) {
				clip |= 1;
			} else if ( texCoords0 > 1.0f ) {
				clip |= 2;
			}
			if ( texCoords1 < 0.0f ) {
				clip |= 4;
			} else if ( texCoords1 > 1.0f ) {
				clip |= 8;
			}
			texCoords[0] = texCoords0;
			texCoords[1] = texCoords1;
			
			// modulate the strength based on the height and color
			if ( dist2 > radius ) {
				clip |= 16;
				modulate = 0.0f;
			} else if ( dist2 < -radius ) {
				clip |= 32;
				modulate = 0.0f;
			} else {
				dist2 = Q_fabs(dist2);
				if ( dist2 < radius * 0.5f ) {
					modulate = 1.0f;
				} else {
					modulate = 2.0f * (radius - dist2) * scale;
				}
			}
			clipBits[i] = clip;

			modulateVec = vec_ld(0,(float *)&modulate);
			modulateVec = vec_perm(modulateVec,modulateVec,modulatePerm);
			colorVec = vec_madd(floatColorVec0,modulateVec,zero);
			colorInt = vec_cts(colorVec,0);	// RGBx
			colorShort = vec_pack(colorInt,colorInt);		// RGBxRGBx
			colorChar = vec_packsu(colorShort,colorShort);	// RGBxRGBxRGBxRGBx
			colorChar = vec_sel(colorChar,vSel,vSel);		// RGBARGBARGBARGBA replace alpha with 255
			vec_ste((vector unsigned int)colorChar,0,(unsigned int *)colors);	// store color
#else
			VectorSubtract( origin, tess.xyz[i], dist );
			texCoords[0] = 0.5f + dist[0] * scale;
			texCoords[1] = 0.5f + dist[1] * scale;

			clip = 0;
			if ( texCoords[0] < 0.0f ) {
				clip |= 1;
			} else if ( texCoords[0] > 1.0f ) {
				clip |= 2;
			}
			if ( texCoords[1] < 0.0f ) {
				clip |= 4;
			} else if ( texCoords[1] > 1.0f ) {
				clip |= 8;
			}
			// modulate the strength based on the height and color
			if ( dist[2] > radius ) {
				clip |= 16;
				modulate = 0.0f;
			} else if ( dist[2] < -radius ) {
				clip |= 32;
				modulate = 0.0f;
			} else {
				dist[2] = Q_fabs(dist[2]);
				if ( dist[2] < radius * 0.5f ) {
					modulate = 1.0f;
				} else {
					modulate = 2.0f * (radius - dist[2]) * scale;
				}
			}
			clipBits[i] = clip;

			colors[0] = myftol(floatColor[0] * modulate);
			colors[1] = myftol(floatColor[1] * modulate);
			colors[2] = myftol(floatColor[2] * modulate);
			colors[3] = 255;
#endif
		}

		// build a list of triangles that need light
		numIndexes = 0;
		for ( i = 0 ; i < tess.numIndexes ; i += 3 ) {
			int		a, b, c;

			a = tess.indexes[i];
			b = tess.indexes[i+1];
			c = tess.indexes[i+2];
			if ( clipBits[a] & clipBits[b] & clipBits[c] ) {
				continue;	// not lighted
			}
			hitIndexes[numIndexes] = a;
			hitIndexes[numIndexes+1] = b;
			hitIndexes[numIndexes+2] = c;
			numIndexes += 3;
		}

		if ( !numIndexes ) {
			continue;
		}

		qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
		qglTexCoordPointer( 2, GL_FLOAT, 0, texCoordsArray[0] );

		qglEnableClientState( GL_COLOR_ARRAY );
		qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, colorArray );

		GL_Bind( tr.dlightImage );
		// include GLS_DEPTHFUNC_EQUAL so alpha tested surfaces don't add light
		// where they aren't rendered
		if ( dl->additive ) {
			GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		}
		else {
			GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		}
		R_DrawElements( numIndexes, hitIndexes );
		backEnd.pc.c_totalIndexes += numIndexes;
		backEnd.pc.c_dlightIndexes += numIndexes;
	}
}


/*
===================
WALKER: hardware fog

Q3 draws a fog volume as one more pass over every surface in it, with fog
texture coordinates computed on the CPU for every vertex each frame. Over the
whole map that pass halved the frame rate. A fog volume without a visible
surface (the map-wide haze of wk_islands) is drawn with OpenGL fog instead,
which costs nothing on any video card. The fog curve follows Q3's: about half
density at a quarter of the fog distance (GL_EXP, density 2.4 / distance).
Stages after the first are blended with what is already fogged, so the fog
colour is chosen per blend mode: white for multiply, grey for multiply x2,
black for additive. r_drawfog 2 brings back the original fog pass.
===================
*/
static qboolean	hardwareFog;
static qboolean	hardwareFogUsed;	// this tesselation was fogged by OpenGL
static vec3_t	hardwareFogColor;
static float	hardwareFogDensity;	// for the dynamic lights under the haze

static qboolean RB_HardwareFogBegin( void ) {
	fog_t	*fog;

	hardwareFog = qfalse;
	hardwareFogUsed = qfalse;
	if ( r_drawfog->integer != 1 || !tess.fogNum || !tess.shader->fogPass || !tr.world ) {
		return qfalse;
	}
	fog = tr.world->fogs + tess.fogNum;
	if ( fog->hasSurface ) {
		return qfalse;		// fog with a visible plane: the classic pass draws its edge
	}
	VectorCopy( fog->parms.color, hardwareFogColor );
	qglFogi( GL_FOG_MODE, GL_EXP );
	hardwareFogDensity = 2.4f / ( fog->parms.depthForOpaque > 1 ? fog->parms.depthForOpaque : 1 );
	qglFogf( GL_FOG_DENSITY, hardwareFogDensity );
	qglHint( GL_FOG_HINT, GL_NICEST );
	qglEnable( GL_FOG );
	hardwareFog = qtrue;
	hardwareFogUsed = qtrue;
	return qtrue;
}

static void RB_HardwareFogStage( int stateBits ) {
	int		srcBlend = stateBits & GLS_SRCBLEND_BITS;
	int		dstBlend = stateBits & GLS_DSTBLEND_BITS;
	vec4_t	color;

	if ( !hardwareFog ) {
		return;
	}
	color[3] = 1;
	if ( ( srcBlend == GLS_SRCBLEND_DST_COLOR && dstBlend == GLS_DSTBLEND_ZERO )
		|| ( srcBlend == GLS_SRCBLEND_ZERO && dstBlend == GLS_DSTBLEND_SRC_COLOR ) ) {
		VectorSet( color, 1, 1, 1 );			// multiply: fade to "no change"
	} else if ( srcBlend == GLS_SRCBLEND_DST_COLOR && dstBlend == GLS_DSTBLEND_SRC_COLOR ) {
		VectorSet( color, 0.5f, 0.5f, 0.5f );	// multiply x2
	} else if ( dstBlend == GLS_DSTBLEND_ONE
		|| ( srcBlend == GLS_SRCBLEND_ZERO && dstBlend == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR ) ) {
		VectorSet( color, 0, 0, 0 );			// additive, or darken by the source colour
	} else {
		VectorCopy( hardwareFogColor, color );
	}
	qglFogfv( GL_FOG_COLOR, color );
}

static qboolean RB_HardwareFogOn( void ) {
	return hardwareFog;
}

static void RB_HardwareFogEnd( void ) {
	if ( hardwareFog ) {
		qglDisable( GL_FOG );
		hardwareFog = qfalse;
	}
}

/*
===================
WALKER: dynamic lights in one pass (GLSL)

ProjectDlightTexture draws a pass for every light touching the surface, with
texture coordinates and colours computed on the processor for every vertex.
With OpenGL 2.0 all lights of the surface go in one pass of a program
(tr_glsl.c), which computes the same falloff per pixel; the processor only
picks the triangles inside some light (the same box test as Q3's). r_glsl 0
or additive lights: the classic pass.
===================
*/
static void RB_DlightPassGLSL( void ) {
	float		lights[GLSL_MAX_DLIGHTS][4];
	float		colors[GLSL_MAX_DLIGHTS][4];
	byte		clipBits[SHADER_MAX_VERTEXES];
	byte		lit[SHADER_MAX_INDEXES / 3];
	glIndex_t	hitIndexes[SHADER_MAX_INDEXES];
	int			numTriangles = tess.numIndexes / 3;
	int			light, vertex, triangle, count = 0, numIndexes;

	Com_Memset( lit, 0, numTriangles );
	for ( light = 0; light <= backEnd.refdef.num_dlights; light++ ) {
		dlight_t	*dl;
		float		radius, half;

		// a full set of lights, or the last one: draw what they light
		if ( count == GLSL_MAX_DLIGHTS || ( light == backEnd.refdef.num_dlights && count ) ) {
			numIndexes = 0;
			for ( triangle = 0; triangle < numTriangles; triangle++ ) {
				if ( lit[triangle] ) {
					hitIndexes[numIndexes] = tess.indexes[triangle * 3];
					hitIndexes[numIndexes + 1] = tess.indexes[triangle * 3 + 1];
					hitIndexes[numIndexes + 2] = tess.indexes[triangle * 3 + 2];
					numIndexes += 3;
				}
			}
			if ( numIndexes ) {
				// GLS_DEPTHFUNC_EQUAL: no light where alpha tested surfaces are not drawn
				GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
				GLSL_DlightsBegin( count, lights[0], colors[0], hardwareFogUsed ? hardwareFogDensity : 0 );
				R_DrawElements( numIndexes, hitIndexes );
				GLSL_DlightsEnd();
				backEnd.pc.c_totalIndexes += numIndexes;
				backEnd.pc.c_dlightIndexes += numIndexes;
			}
			count = 0;
			Com_Memset( lit, 0, numTriangles );
		}
		if ( light == backEnd.refdef.num_dlights ) {
			break;
		}
		if ( !( tess.dlightBits & ( 1 << light ) ) ) {
			continue;
		}
		dl = &backEnd.refdef.dlights[light];
		radius = dl->radius;
		half = radius * 0.5f;		// the light texture reaches half the radius across

		for ( vertex = 0; vertex < tess.numVertexes; vertex++ ) {
			float	*xyz = tess.xyz[vertex];
			int		clip = 0;

			if ( dl->transformed[0] - xyz[0] < -half ) {
				clip |= 1;
			} else if ( dl->transformed[0] - xyz[0] > half ) {
				clip |= 2;
			}
			if ( dl->transformed[1] - xyz[1] < -half ) {
				clip |= 4;
			} else if ( dl->transformed[1] - xyz[1] > half ) {
				clip |= 8;
			}
			if ( dl->transformed[2] - xyz[2] > radius ) {
				clip |= 16;
			} else if ( dl->transformed[2] - xyz[2] < -radius ) {
				clip |= 32;
			}
			clipBits[vertex] = clip;
		}
		backEnd.pc.c_dlightVertexes += tess.numVertexes;
		for ( triangle = 0; triangle < numTriangles; triangle++ ) {
			if ( !( clipBits[tess.indexes[triangle * 3]] & clipBits[tess.indexes[triangle * 3 + 1]] & clipBits[tess.indexes[triangle * 3 + 2]] ) ) {
				lit[triangle] = 1;
			}
		}
		VectorCopy( dl->transformed, lights[count] );
		lights[count][3] = 1.0f / radius;
		VectorCopy( dl->color, colors[count] );
		colors[count][3] = 1.0f;
		count++;
	}
}

static void RB_DlightPass( void ) {
	int		light;

	if ( !backEnd.refdef.num_dlights ) {
		return;
	}
	if ( GLSL_DlightsAvailable() ) {
		for ( light = 0; light < backEnd.refdef.num_dlights; light++ ) {
			if ( ( tess.dlightBits & ( 1 << light ) ) && backEnd.refdef.dlights[light].additive ) {
				break;
			}
		}
		if ( light == backEnd.refdef.num_dlights ) {
			RB_DlightPassGLSL();
			return;
		}
	}
	ProjectDlightTexture();
}


/*
===================
RB_FogPass

Blends a fog texture on top of everything else
===================
*/
static void RB_FogPass( void ) {
	fog_t		*fog;
	int			i;

	qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors );

	qglEnableClientState( GL_TEXTURE_COORD_ARRAY);
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );

	fog = tr.world->fogs + tess.fogNum;

	for ( i = 0; i < tess.numVertexes; i++ ) {
		* ( int * )&tess.svars.colors[i] = fog->colorInt;
	}

	RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[0] );

	GL_Bind( tr.fogImage );

	if ( tess.shader->fogPass == FP_EQUAL ) {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
	} else {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	}

	R_DrawElements( tess.numIndexes, tess.indexes );
}

/*
===============
ComputeColors
===============
*/
static void ComputeColors( shaderStage_t *pStage )
{
	int		i;

	//
	// rgbGen
	//
	switch ( pStage->rgbGen )
	{
		case CGEN_IDENTITY:
			Com_Memset( tess.svars.colors, 0xff, tess.numVertexes * 4 );
			break;
		default:
		case CGEN_IDENTITY_LIGHTING:
			Com_Memset( tess.svars.colors, tr.identityLightByte, tess.numVertexes * 4 );
			break;
		case CGEN_LIGHTING_DIFFUSE:
			RB_CalcDiffuseColor( ( unsigned char * ) tess.svars.colors );
			break;
		case CGEN_EXACT_VERTEX:
			Com_Memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			break;
		case CGEN_CONST:
			for ( i = 0; i < tess.numVertexes; i++ ) {
				*(int *)tess.svars.colors[i] = *(int *)pStage->constantColor;
			}
			break;
		case CGEN_VERTEX:
			if ( tr.identityLight == 1 )
			{
				Com_Memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = tess.vertexColors[i][0] * tr.identityLight;
					tess.svars.colors[i][1] = tess.vertexColors[i][1] * tr.identityLight;
					tess.svars.colors[i][2] = tess.vertexColors[i][2] * tr.identityLight;
					tess.svars.colors[i][3] = tess.vertexColors[i][3];
				}
			}
			break;
		case CGEN_ONE_MINUS_VERTEX:
			if ( tr.identityLight == 1 )
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = 255 - tess.vertexColors[i][0];
					tess.svars.colors[i][1] = 255 - tess.vertexColors[i][1];
					tess.svars.colors[i][2] = 255 - tess.vertexColors[i][2];
				}
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = ( 255 - tess.vertexColors[i][0] ) * tr.identityLight;
					tess.svars.colors[i][1] = ( 255 - tess.vertexColors[i][1] ) * tr.identityLight;
					tess.svars.colors[i][2] = ( 255 - tess.vertexColors[i][2] ) * tr.identityLight;
				}
			}
			break;
		case CGEN_FOG:
			{
				fog_t		*fog;

				fog = tr.world->fogs + tess.fogNum;

				for ( i = 0; i < tess.numVertexes; i++ ) {
					* ( int * )&tess.svars.colors[i] = fog->colorInt;
				}
			}
			break;
		case CGEN_WAVEFORM:
			RB_CalcWaveColor( &pStage->rgbWave, ( unsigned char * ) tess.svars.colors );
			break;
		case CGEN_ENTITY:
			RB_CalcColorFromEntity( ( unsigned char * ) tess.svars.colors );
			break;
		case CGEN_ONE_MINUS_ENTITY:
			RB_CalcColorFromOneMinusEntity( ( unsigned char * ) tess.svars.colors );
			break;
		case CGEN_WATER_RIPPLE:
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				vec3_t	toVertex;
				float	strength, value;

				VectorSubtract( tess.xyz[i], backEnd.viewParms.or.origin, toVertex );
				strength = ( VectorLength( toVertex ) - pStage->rgbWave.base ) / ( pStage->rgbWave.amplitude - pStage->rgbWave.base );
				strength = strength <= 0 ? 0 : ( strength >= 1 ? 1 : strength * strength * ( 3 - 2 * strength ) );
				strength *= r_waterRipple->value;
				if ( strength > 1 ) {
					strength = 1;
				}
				if ( pStage->rgbWave.frequency ) {
					// additive glints
					value = strength * pStage->rgbWave.phase * 255;
					value = value > 255 ? 255 : value;
					tess.svars.colors[i][0] = tess.svars.colors[i][1] = tess.svars.colors[i][2] = (byte)value;
					tess.svars.colors[i][3] = 255;
				} else {
					// dst * ( 2k * tex + 1 - k ) = dst * ( 1 + k * ( 2 * tex - 1 ) )
					value = strength * 2 * 255;
					value = value > 255 ? 255 : value;
					tess.svars.colors[i][0] = tess.svars.colors[i][1] = tess.svars.colors[i][2] = (byte)value;
					tess.svars.colors[i][3] = (byte)( strength * 255 );
				}
			}
			break;
		case CGEN_ONE_MINUS_PORTAL:
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				vec3_t	toVertex;
				float	nearness;
				byte	level;

				VectorSubtract( tess.xyz[i], backEnd.viewParms.or.origin, toVertex );
				nearness = 1.0f - VectorLength( toVertex ) / tess.shader->portalRange;
				level = nearness <= 0 ? 0 : ( nearness >= 1 ? 255 : (byte)( nearness * 255 ) );
				tess.svars.colors[i][0] = tess.svars.colors[i][1] = tess.svars.colors[i][2] = level;
				tess.svars.colors[i][3] = 255;
			}
			break;
	}

	//
	// alphaGen
	//
	switch ( pStage->alphaGen )
	{
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if ( pStage->rgbGen != CGEN_IDENTITY ) {
			if ( ( pStage->rgbGen == CGEN_VERTEX && tr.identityLight != 1 ) ||
				 pStage->rgbGen != CGEN_VERTEX ) {
				for ( i = 0; i < tess.numVertexes; i++ ) {
					tess.svars.colors[i][3] = 0xff;
				}
			}
		}
		break;
	case AGEN_CONST:
		if ( pStage->rgbGen != CGEN_CONST ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = pStage->constantColor[3];
			}
		}
		break;
	case AGEN_WAVEFORM:
		RB_CalcWaveAlpha( &pStage->alphaWave, ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_LIGHTING_SPECULAR:
		RB_CalcSpecularAlpha( ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_VIEW_FACING:
		RB_CalcViewFacingAlpha( ( unsigned char * ) tess.svars.colors, pStage->alphaWave.base, pStage->alphaWave.amplitude );
		break;
	case AGEN_ENTITY:
		RB_CalcAlphaFromEntity( ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_ONE_MINUS_ENTITY:
		RB_CalcAlphaFromOneMinusEntity( ( unsigned char * ) tess.svars.colors );
		break;
    case AGEN_VERTEX:
		if ( pStage->rgbGen != CGEN_VERTEX ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = tess.vertexColors[i][3];
			}
		}
        break;
    case AGEN_ONE_MINUS_VERTEX:
        for ( i = 0; i < tess.numVertexes; i++ )
        {
			tess.svars.colors[i][3] = 255 - tess.vertexColors[i][3];
        }
        break;
	case AGEN_PORTAL:
		{
			unsigned char alpha;

			for ( i = 0; i < tess.numVertexes; i++ )
			{
				float len;
				vec3_t v;

				VectorSubtract( tess.xyz[i], backEnd.viewParms.or.origin, v );
				len = VectorLength( v );

				len /= tess.shader->portalRange;

				if ( len < 0 )
				{
					alpha = 0;
				}
				else if ( len > 1 )
				{
					alpha = 0xff;
				}
				else
				{
					alpha = len * 0xff;
				}

				tess.svars.colors[i][3] = alpha;
			}
		}
		break;
	}

	//
	// fog adjustment for colors to fade out as fog increases
	//
	if ( tess.fogNum && r_drawfog->integer && !hardwareFogUsed && !backEnd.viewParms.isPortal )
	{
		switch ( pStage->adjustColorsForFog )
		{
		case ACFF_MODULATE_RGB:
			RB_CalcModulateColorsByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_MODULATE_ALPHA:
			RB_CalcModulateAlphasByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_MODULATE_RGBA:
			RB_CalcModulateRGBAsByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_NONE:
			break;
		}
	}
}

/*
===============
ComputeTexCoords
===============
*/
static void ComputeTexCoords( shaderStage_t *pStage ) {
	int		i;
	int		b;

	for ( b = 0; b < NUM_TEXTURE_BUNDLES; b++ ) {
		int tm;

		//
		// generate the texture coordinates
		//
		switch ( pStage->bundle[b].tcGen )
		{
		case TCGEN_IDENTITY:
			Com_Memset( tess.svars.texcoords[b], 0, sizeof( float ) * 2 * tess.numVertexes );
			break;
		case TCGEN_TEXTURE:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = tess.texCoords[i][0][0];
				tess.svars.texcoords[b][i][1] = tess.texCoords[i][0][1];
			}
			break;
		case TCGEN_LIGHTMAP:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = tess.texCoords[i][1][0];
				tess.svars.texcoords[b][i][1] = tess.texCoords[i][1][1];
			}
			break;
		case TCGEN_VECTOR:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[0] );
				tess.svars.texcoords[b][i][1] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[1] );
			}
			break;
		case TCGEN_FOG:
			RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[b] );
			break;
		case TCGEN_ENVIRONMENT_MAPPED:
			RB_CalcEnvironmentTexCoords( ( float * ) tess.svars.texcoords[b] );
			break;
		case TCGEN_BAD:
			return;
		}

		//
		// alter texture coordinates
		//
		for ( tm = 0; tm < pStage->bundle[b].numTexMods ; tm++ ) {
			switch ( pStage->bundle[b].texMods[tm].type )
			{
			case TMOD_NONE:
				tm = TR_MAX_TEXMODS;		// break out of for loop
				break;

			case TMOD_TURBULENT:
				RB_CalcTurbulentTexCoords( &pStage->bundle[b].texMods[tm].wave, 
						                 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ENTITY_TRANSLATE:
				RB_CalcScrollTexCoords( backEnd.currentEntity->e.shaderTexCoord,
									 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCROLL:
				RB_CalcScrollTexCoords( pStage->bundle[b].texMods[tm].scroll,
										 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCALE:
				RB_CalcScaleTexCoords( pStage->bundle[b].texMods[tm].scale,
									 ( float * ) tess.svars.texcoords[b] );
				break;
			
			case TMOD_STRETCH:
				RB_CalcStretchTexCoords( &pStage->bundle[b].texMods[tm].wave, 
						               ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_TRANSFORM:
				RB_CalcTransformTexCoords( &pStage->bundle[b].texMods[tm],
						                 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ROTATE:
				RB_CalcRotateTexCoords( pStage->bundle[b].texMods[tm].rotateSpeed,
										( float * ) tess.svars.texcoords[b] );
				break;

			default:
				ri.Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'\n", pStage->bundle[b].texMods[tm].type, tess.shader->name );
				break;
			}
		}
	}
}

/*
** RB_IterateStagesGeneric
*/
static void RB_IterateStagesGeneric( shaderCommands_t *input )
{
	int stage;

	for ( stage = 0; stage < MAX_SHADER_STAGES; stage++ )
	{
		shaderStage_t *pStage = tess.xstages[stage];

		if ( !pStage )
		{
			break;
		}

		ComputeColors( pStage );
		ComputeTexCoords( pStage );
		RB_HardwareFogStage( pStage->stateBits );

		if ( !setArraysOnce )
		{
			qglEnableClientState( GL_COLOR_ARRAY );
			qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, input->svars.colors );
		}

		//
		// do multitexture
		//
		if ( pStage->bundle[1].image[0] != 0 )
		{
			DrawMultitextured( input, stage );
		}
		else
		{
			if ( !setArraysOnce )
			{
				qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoords[0] );
			}

			//
			// set state
			//
			if ( pStage->bundle[0].vertexLightmap && ( (r_vertexLight->integer && !r_uiFullScreen->integer) || glConfig.hardwareType == GLHW_PERMEDIA2 ) && r_lightmap->integer )
			{
				GL_Bind( tr.whiteImage );
			}
			else 
				R_BindAnimatedImage( &pStage->bundle[0] );

			GL_State( pStage->stateBits );

			//
			// draw
			//
			R_DrawElements( input->numIndexes, input->indexes );
		}
		// allow skipping out to show just lightmaps during development
		if ( r_lightmap->integer && ( pStage->bundle[0].isLightmap || pStage->bundle[1].isLightmap || pStage->bundle[0].vertexLightmap ) )
		{
			break;
		}
	}
}


/*
** RB_StageIteratorGeneric
*/
void RB_StageIteratorGeneric( void )
{
	shaderCommands_t *input;

	input = &tess;

	RB_DeformTessGeometry();

	//
	// log this call
	//
	if ( r_logFile->integer ) 
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va("--- RB_StageIteratorGeneric( %s ) ---\n", tess.shader->name) );
	}

	//
	// set face culling appropriately
	//
	GL_Cull( input->shader->cullType );

	// set polygon offset if necessary
	if ( input->shader->polygonOffset )
	{
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	//
	// if there is only a single pass then we can enable color
	// and texture arrays before we compile, otherwise we need
	// to avoid compiling those arrays since they will change
	// during multipass rendering
	//
	if ( tess.numPasses > 1 || input->shader->multitextureEnv )
	{
		setArraysOnce = qfalse;
		qglDisableClientState (GL_COLOR_ARRAY);
		qglDisableClientState (GL_TEXTURE_COORD_ARRAY);
	}
	else
	{
		setArraysOnce = qtrue;

		qglEnableClientState( GL_COLOR_ARRAY);
		qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors );

		qglEnableClientState( GL_TEXTURE_COORD_ARRAY);
		qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );
	}

	//
	// lock XYZ
	//
	qglVertexPointer (3, GL_FLOAT, 16, input->xyz);	// padded for SIMD
	if (qglLockArraysEXT)
	{
		qglLockArraysEXT(0, input->numVertexes);
		GLimp_LogComment( "glLockArraysEXT\n" );
	}

	//
	// enable color and texcoord arrays after the lock if necessary
	//
	if ( !setArraysOnce )
	{
		qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
		qglEnableClientState( GL_COLOR_ARRAY );
	}

	//
	// call shader function
	//
	RB_HardwareFogBegin();
	RB_IterateStagesGeneric( input );
	RB_HardwareFogEnd();

	// 
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE
		&& !(tess.shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) ) ) {
		RB_DlightPass();		// WALKER: one GLSL pass when it can
	}

	//
	// now do fog
	//
	// WALKER: r_drawfog 0 turns the haze off, 1 draws it with OpenGL fog where
	// possible (RB_HardwareFogBegin), 2 always with the fog pass
	if ( tess.fogNum && tess.shader->fogPass && r_drawfog->integer && !hardwareFogUsed && !backEnd.viewParms.isPortal ) {
		RB_FogPass();
	}

	// 
	// unlock arrays
	//
	if (qglUnlockArraysEXT) 
	{
		qglUnlockArraysEXT();
		GLimp_LogComment( "glUnlockArraysEXT\n" );
	}

	//
	// reset polygon offset
	//
	if ( input->shader->polygonOffset )
	{
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
}


/*
** RB_StageIteratorVertexLitTexture
*/
void RB_StageIteratorVertexLitTexture( void )
{
	shaderCommands_t *input;
	shader_t		*shader;

	input = &tess;

	shader = input->shader;

	//
	// compute colors
	//
	RB_CalcDiffuseColor( ( unsigned char * ) tess.svars.colors );

	//
	// log this call
	//
	if ( r_logFile->integer ) 
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va("--- RB_StageIteratorVertexLitTexturedUnfogged( %s ) ---\n", tess.shader->name) );
	}

	//
	// set face culling appropriately
	//
	GL_Cull( input->shader->cullType );

	//
	// set arrays and lock
	//
	qglEnableClientState( GL_COLOR_ARRAY);
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY);

	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors );
	qglTexCoordPointer( 2, GL_FLOAT, 16, tess.texCoords[0][0] );
	qglVertexPointer (3, GL_FLOAT, 16, input->xyz);

	if ( qglLockArraysEXT )
	{
		qglLockArraysEXT(0, input->numVertexes);
		GLimp_LogComment( "glLockArraysEXT\n" );
	}

	//
	// call special shade routine
	//
	RB_HardwareFogBegin();
	RB_HardwareFogStage( tess.xstages[0]->stateBits );
	R_BindAnimatedImage( &tess.xstages[0]->bundle[0] );
	GL_State( tess.xstages[0]->stateBits );
	R_DrawElements( input->numIndexes, input->indexes );
	RB_HardwareFogEnd();

	// 
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE ) {
		RB_DlightPass();		// WALKER: one GLSL pass when it can
	}

	//
	// now do fog
	//
	// WALKER: r_drawfog 0 turns the haze off, 1 draws it with OpenGL fog where
	// possible (RB_HardwareFogBegin), 2 always with the fog pass
	if ( tess.fogNum && tess.shader->fogPass && r_drawfog->integer && !hardwareFogUsed && !backEnd.viewParms.isPortal ) {
		RB_FogPass();
	}

	// 
	// unlock arrays
	//
	if (qglUnlockArraysEXT) 
	{
		qglUnlockArraysEXT();
		GLimp_LogComment( "glUnlockArraysEXT\n" );
	}
}

//define	REPLACE_MODE

void RB_StageIteratorLightmappedMultitexture( void ) {
	shaderCommands_t *input;

	input = &tess;

	//
	// log this call
	//
	if ( r_logFile->integer ) {
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va("--- RB_StageIteratorLightmappedMultitexture( %s ) ---\n", tess.shader->name) );
	}

	//
	// set face culling appropriately
	//
	GL_Cull( input->shader->cullType );

	//
	// set color, pointers, and lock
	//
	GL_State( GLS_DEFAULT );
	qglVertexPointer( 3, GL_FLOAT, 16, input->xyz );
	RB_HardwareFogBegin();
	RB_HardwareFogStage( GLS_DEFAULT );

#ifdef REPLACE_MODE
	qglDisableClientState( GL_COLOR_ARRAY );
	qglColor3f( 1, 1, 1 );
	qglShadeModel( GL_FLAT );
#else
	qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.constantColor255 );
#endif

	//
	// select base stage
	//
	GL_SelectTexture( 0 );

	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	R_BindAnimatedImage( &tess.xstages[0]->bundle[0] );
	qglTexCoordPointer( 2, GL_FLOAT, 16, tess.texCoords[0][0] );

	//
	// configure second stage
	//
	GL_SelectTexture( 1 );
	qglEnable( GL_TEXTURE_2D );
	if ( r_lightmap->integer ) {
		GL_TexEnv( GL_REPLACE );
	} else {
		GL_TexEnv( GL_MODULATE );
	}
	R_BindAnimatedImage( &tess.xstages[0]->bundle[1] );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 16, tess.texCoords[0][1] );

	//
	// lock arrays
	//
	if ( qglLockArraysEXT ) {
		qglLockArraysEXT(0, input->numVertexes);
		GLimp_LogComment( "glLockArraysEXT\n" );
	}

	R_DrawElements( input->numIndexes, input->indexes );

	//
	// disable texturing on TEXTURE1, then select TEXTURE0
	//
	qglDisable( GL_TEXTURE_2D );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );

	GL_SelectTexture( 0 );
#ifdef REPLACE_MODE
	GL_TexEnv( GL_MODULATE );
	qglShadeModel( GL_SMOOTH );
#endif
	RB_HardwareFogEnd();

	// 
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE ) {
		RB_DlightPass();		// WALKER: one GLSL pass when it can
	}

	//
	// now do fog
	//
	// WALKER: r_drawfog 0 turns the haze off, 1 draws it with OpenGL fog where
	// possible (RB_HardwareFogBegin), 2 always with the fog pass
	if ( tess.fogNum && tess.shader->fogPass && r_drawfog->integer && !hardwareFogUsed && !backEnd.viewParms.isPortal ) {
		RB_FogPass();
	}

	//
	// unlock arrays
	//
	if ( qglUnlockArraysEXT ) {
		qglUnlockArraysEXT();
		GLimp_LogComment( "glUnlockArraysEXT\n" );
	}
}

/*
** RB_EndSurface
*/
void RB_EndSurface( void ) {
	shaderCommands_t *input;

	input = &tess;

	// WALKER: the shadow quads collected without the tess arrays (RB_MeshShadow)
	if ( tess.shader == tr.shadowShader ) {
		RB_ShadowTessEnd();
		return;
	}

	if ( input->numIndexes == 0 && input->numVboRanges == 0 ) {
		return;
	}

	if (input->indexes[SHADER_MAX_INDEXES-1] != 0) {
		ri.Error (ERR_DROP, "RB_EndSurface() - SHADER_MAX_INDEXES hit");
	}	
	if (input->xyz[SHADER_MAX_VERTEXES-1][0] != 0) {
		ri.Error (ERR_DROP, "RB_EndSurface() - SHADER_MAX_VERTEXES hit");
	}

	// for debugging of sort order issues, stop rendering after a given sort value
	if ( r_debugSort->integer && r_debugSort->integer < tess.shader->sort ) {
		tess.numVboRanges = 0;
		tess.meshModel = NULL;
		return;
	}

	// WALKER: the part of the batch in the map's buffers
	if ( input->numVboRanges ) {
		backEnd.pc.c_shaders++;
		backEnd.pc.c_indexes += tess.vboIndexes;
		backEnd.pc.c_totalIndexes += tess.vboIndexes * tess.numPasses;
		if ( tess.meshModel ) {
			RB_DrawMeshVBO();
		} else {
			RB_DrawWorldVBO();
		}
		tess.numVboRanges = 0;
		tess.vboIndexes = 0;
		tess.meshModel = NULL;
	}

	if ( input->numIndexes ) {
		//
		// update performance counters
		//
		backEnd.pc.c_shaders++;
		backEnd.pc.c_vertexes += tess.numVertexes;
		backEnd.pc.c_indexes += tess.numIndexes;
		backEnd.pc.c_totalIndexes += tess.numIndexes * tess.numPasses;

		//
		// call off to shader specific tess end function
		//
		tess.currentStageIteratorFunc();

		//
		// draw debugging stuff
		//
		if ( r_showtris->integer ) {
			DrawTris (input);
		}
		if ( r_shownormals->integer ) {
			DrawNormals (input);
		}
	}
	// clear shader so we can tell we don't have any unclosed surfaces
	tess.numIndexes = 0;

	GLimp_LogComment( "----------\n" );
}

