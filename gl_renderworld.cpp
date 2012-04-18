/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
//code written by Dr Labman (drlabman@flyingsaucepan.com)

//world brush related drawing code
#include "quakedef.h"

typedef struct glRect_s {
	unsigned char l,t,w,h;
} glRect_t;

//world texture chains
extern	msurface_t  *skychain;
extern	msurface_t  *waterchain;
extern	msurface_t	*extrachain;
extern	msurface_t	*outlinechain;

#define	BLOCK_WIDTH		128
#define	BLOCK_HEIGHT	128

#define	MAX_LIGHTMAPS	1024

extern	qboolean	lightmap_modified[MAX_LIGHTMAPS];
extern	glRect_t	lightmap_rectchange[MAX_LIGHTMAPS];
extern	byte		lightmaps[4*MAX_LIGHTMAPS*BLOCK_WIDTH*BLOCK_HEIGHT];

extern	int			lightmap_textures;
extern	int			lightmap_bytes;

void R_RenderDynamicLightmaps (msurface_t *fa);
void R_DrawExtraChains(msurface_t *extrachain);
void Surf_DrawExtraChainsFour(msurface_t *extrachain);

/*
Thoughts about code

  Textures to draw on a surface:
Normal, Lightmap, Detail (semi optional)
  Optional textures:
Caustic, Shiny, Fullbright

  Other surfaces:
Water, Sky

4 TMU's
2 pass system

	first pass:
		Normal, Lightmap, Fullbright, Detail
	second pass:
		caustics 1, caustics 2, Shiny Glass, Shiny Metal

2 TMU's
4 pass system

	first pass:
		Normal, Lightmap
	second pass:
		Fullbright, Detail
	thrid pass
		caustics 1, caustics 2
	forth pass
		shiny glass, shiny metal

Needs to be replaced with a system to compress passes together
IE: shiny glass and shiny metal currently wont be used together
	so if there is no fullbright or detail is turned off they could be added into that stage

To make changing order easier each texture type should have a enable and disable function
also because caustics need a diffrent texture coord system (ie scrolling) there should be some option for that

  */

//setup for a normal texture
__inline void Surf_EnableNormal(){
	//just uses normal modulate
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

//setup for Lightmap
__inline void Surf_EnableLightmap(){
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
	glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
	glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 4.0);
}

//setup for Fullbright
__inline void Surf_EnableFullbright(){
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
}

//setup for Detail
__inline void Surf_EnableDetail(){
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
	glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
	glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0);
}

//setup for a shiny texture
__inline void Surf_EnableShiny(){
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

__inline void Surf_EnableExtra(){
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
}

__inline void Surf_EnableCaustic(){
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

//reset the settings to the defualt
__inline void Surf_Reset(){
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

void Surf_Outline(msurface_t *surfchain){
	msurface_t *s, *removelink;
	float		*v;
	int			k;

	glColor4f(0,0,0,1);

	//GL_DisableTMU(GL_TEXTURE0_ARB);
	Surf_EnableNormal();
	glBindTexture(GL_TEXTURE_2D, 0);
	glCullFace (GL_BACK);
	glPolygonMode (GL_FRONT, GL_LINE);

	glLineWidth (5.5f);
	glEnable (GL_LINE_SMOOTH);

	for (s = surfchain; s; ){
		// Outline the polys
		glBegin(GL_POLYGON);
		v = s->polys->verts[0];
		for (k=0 ; k<s->polys->numverts ; k++, v+= VERTEXSIZE)
		{
			glVertex3fv (v);
		}
		glEnd ();

		removelink = s;
		s = s->outline;
		removelink->outline = NULL;
	}

	glColor4f(1,1,1,1);
	glCullFace (GL_FRONT);
	glPolygonMode (GL_FRONT, GL_FILL);

	//GL_EnableTMU(GL_TEXTURE0_ARB);
	Surf_Reset();
}

/*
================
R_DrawTextureChains
================
*/

// TODO: speedup rendering
void Surf_DrawTextureChainsFour(model_t *model)
{
	msurface_t	*s, *removelink;
	int			i, k, detail;

	extern	int	detailtexture;
	float		*v;
	texture_t	*t;

	glRect_t	*theRect;

	//new caustic shader effect
	extern unsigned int dst_caustic;


	//Draw the sky chains first so they can have depth on...
	//draw the sky
	R_DrawSkyChain (skychain);
	skychain = NULL;

	//always a normal texture, so enable tmu
	GL_EnableTMU(GL_TEXTURE0_ARB);
	Surf_EnableNormal();

	//always a lightmap, so enable tmu
	GL_EnableTMU(GL_TEXTURE1_ARB);
	Surf_EnableLightmap();

	//could be a fullbright, just setup for them

	GL_SelectTexture(GL_TEXTURE2_ARB);
	Surf_EnableFullbright();

	if (gl_detail.value)
	{
		GL_EnableTMU(GL_TEXTURE3_ARB);
		glBindTexture(GL_TEXTURE_2D,detailtexture);
		Surf_EnableDetail();
		detail = true;
	}else{
		detail = false;
	}

	//ok now we are ready to draw the texture chains
	for (i=0 ; i<model->numtextures ; i++)
	{
		//if theres no chain go to next.
		//saves binding the textures if they arnt used.
		if (!model->textures[i] || !model->textures[i]->texturechain)
			continue;

		//work out what texture we need if its animating texture
		t = R_TextureAnimation (model->textures[i]->texturechain->texinfo->texture);
	// Binds world to texture unit 0
		GL_SelectTexture(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_2D,t->gl_texturenum);

		if (t->gl_fullbright != 0){
			//if there is a fullbright texture then bind it to TMU2
			GL_EnableTMU(GL_TEXTURE2_ARB);
			glBindTexture(GL_TEXTURE_2D,t->gl_fullbright);
		}


		GL_SelectTexture(GL_TEXTURE1_ARB);

		for (s = model->textures[i]->texturechain; s; )
		{
	// Select the right lightmap
			glBindTexture(GL_TEXTURE_2D,lightmap_textures + s->lightmaptexturenum);

	//update lightmap now
			R_RenderDynamicLightmaps (s);

			k = s->lightmaptexturenum;
			if (lightmap_modified[k])
			{
				lightmap_modified[k] = false;
				theRect = &lightmap_rectchange[k];
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, theRect->t, BLOCK_WIDTH, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE, lightmaps+(k* BLOCK_HEIGHT + theRect->t) *BLOCK_WIDTH*lightmap_bytes);
				theRect->l = BLOCK_WIDTH;
				theRect->t = BLOCK_HEIGHT;
				theRect->h = 0;
				theRect->w = 0;
			}

	// Draw the polys
			glBegin(GL_POLYGON);
			v = s->polys->verts[0];
			for (k=0 ; k<s->polys->numverts ; k++, v+= VERTEXSIZE)
			{
				qglMultiTexCoord2fARB (GL_TEXTURE0_ARB, v[3], v[4]);
				qglMultiTexCoord2fARB (GL_TEXTURE1_ARB, v[5], v[6]);
				if (t->gl_fullbright)
					qglMultiTexCoord2fARB (GL_TEXTURE2_ARB, v[3], v[4]);
				if (detail)
					qglMultiTexCoord2fARB (GL_TEXTURE3_ARB, v[7]*18, v[8]*18);

				glVertex3fv (v);
			}
			glEnd ();

			removelink = s;
			s = s->texturechain;
			removelink->texturechain = NULL;
		}

		if (t->gl_fullbright != 0){
			//if there was a fullbright disable the TMU now
			GL_DisableTMU(GL_TEXTURE2_ARB);
		}

		model->textures[i]->texturechain = NULL;
	}

	// Disable detail texture
	if (detail)
	{
		GL_DisableTMU(GL_TEXTURE3_ARB);
		Surf_Reset();
	}

	// Disable fullbright texture
	GL_DisableTMU(GL_TEXTURE2_ARB);
	Surf_Reset();

	// Disable lightmaps
	GL_DisableTMU(GL_TEXTURE1_ARB);
	Surf_Reset();

	GL_SelectTexture(GL_TEXTURE0_ARB);
	Surf_Reset();

	if (r_outline.value){
		Surf_Outline(outlinechain);
		outlinechain = NULL;
	}

	//draw the extra polys (caustics, metal, glass)
	Surf_DrawExtraChainsFour(extrachain);
	extrachain = NULL;

	//draw the water
	R_DrawWaterChain (waterchain);
	waterchain = NULL;
}

void Surf_DrawExtraChainsFour(msurface_t *extrachain){
	int				k, caustic = false, shiny_metal = false, shiny_glass = false;
	msurface_t		*surf, *removelink;
	float		*v;

	//text coords for water
	float		s, ss, t, tt, os, ot;

	//the first water caustic
	GL_DisableTMU(GL_TEXTURE0_ARB);
	Surf_EnableCaustic();
	glBindTexture(GL_TEXTURE_2D,underwatertexture);

	//the second water caustic
	GL_SelectTexture(GL_TEXTURE1_ARB);
	Surf_EnableCaustic();
	glBindTexture(GL_TEXTURE_2D,underwatertexture);

	//the glass shiny texture
	GL_SelectTexture(GL_TEXTURE2_ARB);
	Surf_EnableShiny();
	glBindTexture(GL_TEXTURE_2D, shinetex_glass);

	//the metal shiny texture
	GL_SelectTexture(GL_TEXTURE3_ARB);
	Surf_EnableShiny();
	glBindTexture(GL_TEXTURE_2D, shinetex_chrome);


	glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
	glEnable (GL_BLEND);
	glColor4f(1,1,1,1);

	for (surf = extrachain; surf; )
	{
		if (!caustic && surf->flags & SURF_UNDERWATER && gl_caustics.value)
		{
			GL_EnableTMU(GL_TEXTURE0_ARB);
			GL_EnableTMU(GL_TEXTURE1_ARB);

			caustic = true;
		}

		if (!shiny_glass && surf->flags & SURF_SHINY_GLASS && gl_shiny.value)
		{
			GL_EnableTMU(GL_TEXTURE2_ARB);

			glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
			glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
			glEnable(GL_TEXTURE_GEN_S);
			glEnable(GL_TEXTURE_GEN_T);

			shiny_glass = true;
		}

		if (!shiny_metal && surf->flags & SURF_SHINY_METAL && gl_shiny.value)
		{
			GL_EnableTMU(GL_TEXTURE3_ARB);

			glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
			glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
			glEnable(GL_TEXTURE_GEN_S);
			glEnable(GL_TEXTURE_GEN_T);

			shiny_metal = true;
		}


		glBegin(GL_POLYGON);
		glNormal3fv(surf->plane->normal);
		v = surf->polys->verts[0];
		for (k=0 ; k<surf->polys->numverts ; k++, v+= VERTEXSIZE)
		{
			if (caustic)
			{
				//work out tex coords
				os = v[7];
				ot = v[8];

				s = os/4 + (realtime*0.05);
				t = ot/4 + (realtime*0.05);
				ss= os/5 - (realtime*0.05);
				tt= ot/5 + (realtime*0.05);

				qglMultiTexCoord2fARB (GL_TEXTURE0_ARB,    s,    t);
				qglMultiTexCoord2fARB (GL_TEXTURE1_ARB,   ss,   tt);
			}

			if (shiny_glass)
				qglMultiTexCoord2fARB (GL_TEXTURE2_ARB, v[7], v[8]);
			if (shiny_metal)
				qglMultiTexCoord2fARB (GL_TEXTURE3_ARB, v[7], v[8]);

			glVertex3fv (v);
		}
		glEnd ();

		if (surf->extra){
			if (!surf->extra->flags & SURF_UNDERWATER && caustic && gl_caustics.value)
			{
				GL_DisableTMU(GL_TEXTURE0_ARB);
				GL_DisableTMU(GL_TEXTURE1_ARB);

				caustic = false;
			}

			if (!(surf->extra->flags & SURF_SHINY_GLASS) && shiny_glass && gl_shiny.value)
			{
				GL_DisableTMU(GL_TEXTURE2_ARB);

				glDisable(GL_TEXTURE_GEN_S);
				glDisable(GL_TEXTURE_GEN_T);

				shiny_glass = false;
			}

			if (!surf->extra->flags & SURF_SHINY_METAL && shiny_metal && gl_shiny.value)
			{
				GL_DisableTMU(GL_TEXTURE3_ARB);

				glDisable(GL_TEXTURE_GEN_S);
				glDisable(GL_TEXTURE_GEN_T);

				shiny_metal = false;
			}
		}

		removelink = surf;
		surf = surf->extra;
		removelink->extra=NULL;
	}

	// Disable shiny metal
	GL_DisableTMU(GL_TEXTURE3_ARB);
	glDisable(GL_TEXTURE_GEN_S);
	glDisable(GL_TEXTURE_GEN_T);
	Surf_Reset();

	// Disable shiny glass
	GL_DisableTMU(GL_TEXTURE2_ARB);
	glDisable(GL_TEXTURE_GEN_S);
	glDisable(GL_TEXTURE_GEN_T);
	Surf_Reset();

	// Disable caustic
	GL_DisableTMU(GL_TEXTURE1_ARB);
	Surf_Reset();

	// Disable caustic
	GL_SelectTexture(GL_TEXTURE0_ARB);
	Surf_Reset();

	GL_EnableTMU(GL_TEXTURE0_ARB);
	glDisable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(1,1,1,1);

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

/*
================
R_DrawTextureChains
================
*/
void Surf_DrawTextureChainsTwo(model_t *model)
{
	msurface_t	*s, *removelink, *prev;
	int			i, k;

	extern	int	detailtexture;
	float		*v;
	texture_t	*t;

	glRect_t	*theRect;

	//new caustic shader effect
	extern unsigned int dst_caustic;

	//Draw the sky chains first so they can have depth on...
	//draw the sky
	R_DrawSkyChain (skychain);
	skychain = NULL;

	//always a normal texture, so enable tmu
	GL_EnableTMU(GL_TEXTURE0_ARB);
	Surf_EnableNormal();

	//always a lightmap, so enable tmu
	GL_EnableTMU(GL_TEXTURE1_ARB);
	Surf_EnableLightmap();

	//ok now we are ready to draw the texture chains
	for (i=0 ; i<model->numtextures ; i++)
	{
		//if theres no chain go to next.
		//saves binding the textures if they arnt used.
		if (!model->textures[i] || !model->textures[i]->texturechain)
			continue;

		//work out what texture we need if its animating texture
		t = R_TextureAnimation (model->textures[i]->texturechain->texinfo->texture);
	// Binds world to texture unit 0
		GL_SelectTexture(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_2D,t->gl_texturenum);

		GL_SelectTexture(GL_TEXTURE1_ARB);

		prev = NULL;

		for (s = model->textures[i]->texturechain; s; )
		{
	// Select the right lightmap
			glBindTexture(GL_TEXTURE_2D,lightmap_textures + s->lightmaptexturenum);

	//update lightmap now
			R_RenderDynamicLightmaps (s);

			k = s->lightmaptexturenum;
			if (lightmap_modified[k])
			{
				lightmap_modified[k] = false;
				theRect = &lightmap_rectchange[k];
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, theRect->t, BLOCK_WIDTH, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE, lightmaps+(k* BLOCK_HEIGHT + theRect->t) *BLOCK_WIDTH*lightmap_bytes);
				theRect->l = BLOCK_WIDTH;
				theRect->t = BLOCK_HEIGHT;
				theRect->h = 0;
				theRect->w = 0;
			}

	// Draw the polys
			glBegin(GL_POLYGON);
			v = s->polys->verts[0];
			for (k=0 ; k<s->polys->numverts ; k++, v+= VERTEXSIZE)
			{
				qglMultiTexCoord2fARB (GL_TEXTURE0_ARB, v[3], v[4]);
				qglMultiTexCoord2fARB (GL_TEXTURE1_ARB, v[5], v[6]);
				if (t->gl_fullbright)
					qglMultiTexCoord2fARB (GL_TEXTURE2_ARB, v[3], v[4]);
				if (gl_detail.value)
					qglMultiTexCoord2fARB (GL_TEXTURE3_ARB, v[7]*18, v[8]*18);

				glVertex3fv (v);
			}
			glEnd ();

			if (!t->gl_fullbright && !gl_detail.value){
				if (prev){								//we arnt the first texture of a chain
					removelink = s;						//we want to remove this surface from the list.
					s = s->texturechain;				//continue to next surface
					removelink->texturechain = NULL;	//remove the surface from the chain
					prev->texturechain = s;				//set the previous surface to have this (the next) one as the next
				} else {
					removelink = s;						//we want to remove this surface from the list.
					s = s->texturechain;				//continue to next surface
					removelink->texturechain = NULL;	//remove the surface from the chain
					model->textures[i]->texturechain = s;//remove this first texture of the chain
				}
			}else{
				prev = s;
				s = s->texturechain;
			}
		}
	}

	// Disable lightmaps
	GL_DisableTMU(GL_TEXTURE1_ARB);
	Surf_Reset();

	GL_SelectTexture(GL_TEXTURE0_ARB);
	Surf_Reset();

	//second pass
	glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
	glEnable (GL_BLEND);
	glColor4f(1,1,1,1);

	//could be a fullbright, just setup for them
	GL_SelectTexture(GL_TEXTURE0_ARB);
	Surf_EnableFullbright();

	if (gl_detail.value)
	{
		GL_EnableTMU(GL_TEXTURE1_ARB);
		glBindTexture(GL_TEXTURE_2D,detailtexture);
		Surf_EnableDetail();
	}

	//ok now we are ready to draw the texture chains
	for (i=0 ; i<model->numtextures ; i++)
	{
		//if theres no chain go to next.
		//saves binding the textures if they arnt used.
		if (!model->textures[i] || !model->textures[i]->texturechain)
			continue;

		//work out what texture we need if its animating texture
		t = R_TextureAnimation (model->textures[i]->texturechain->texinfo->texture);

		if (t->gl_fullbright){
			//if there is a fullbright texture then bind it to TMU0
			GL_EnableTMU(GL_TEXTURE0_ARB);
			glBindTexture(GL_TEXTURE_2D,t->gl_fullbright);
		}

		for (s = model->textures[i]->texturechain; s; )
		{
			if (!gl_detail.value && !t->gl_fullbright){
		// Draw the polys
				glBegin(GL_POLYGON);
				v = s->polys->verts[0];
				for (k=0 ; k<s->polys->numverts ; k++, v+= VERTEXSIZE)
				{
					if (t->gl_fullbright)
						qglMultiTexCoord2fARB (GL_TEXTURE0_ARB, v[3], v[4]);
					if (gl_detail.value)
						qglMultiTexCoord2fARB (GL_TEXTURE1_ARB, v[7]*18, v[8]*18);

					glVertex3fv (v);
				}
				glEnd ();
			}
			removelink = s;
			s = s->texturechain;
			removelink->texturechain = NULL;
		}

		if (t->gl_fullbright != 0){
			//if there was a fullbright disable the TMU now
			GL_DisableTMU(GL_TEXTURE0_ARB);
		}

		model->textures[i]->texturechain = NULL;
	}

	// Disable detail texture
	if (gl_detail.value)
	{
		GL_DisableTMU(GL_TEXTURE1_ARB);
		Surf_Reset();
	}

	// Disable fullbright texture
	GL_SelectTexture(GL_TEXTURE0_ARB);
	Surf_Reset();

	glDisable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(1,1,1,1);

	if (r_outline.value){
		Surf_Outline(outlinechain);
		outlinechain = NULL;
	}

	//draw the extra polys (caustics, metal, glass)
	Surf_DrawExtraChainsFour(extrachain);
	extrachain = NULL;

	//draw the water
	R_DrawWaterChain (waterchain);
	waterchain = NULL;
}

void Surf_DrawExtraChainsTwo(msurface_t *extrachain){
	int				k, caustic = false, shiny_metal = false, shiny_glass = false;
	msurface_t		*surf, *removelink, *prev;
	float		*v;

	//text coords for water
	float		s, ss, t, tt, os, ot;

	//the first water caustic
	GL_DisableTMU(GL_TEXTURE0_ARB);
	Surf_EnableCaustic();
	glBindTexture(GL_TEXTURE_2D,underwatertexture);

	//the second water caustic
	GL_SelectTexture(GL_TEXTURE1_ARB);
	Surf_EnableCaustic();
	glBindTexture(GL_TEXTURE_2D,underwatertexture);

	glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
	glEnable (GL_BLEND);
	glColor4f(1,1,1,1);

	for (surf = extrachain; surf; )
	{
		if (!caustic && surf->flags & SURF_UNDERWATER && gl_caustics.value)
		{
			GL_EnableTMU(GL_TEXTURE0_ARB);
			GL_EnableTMU(GL_TEXTURE1_ARB);

			caustic = true;
		}

		if (caustic){
			glBegin(GL_POLYGON);
			glNormal3fv(surf->plane->normal);
			v = surf->polys->verts[0];
			for (k=0 ; k<surf->polys->numverts ; k++, v+= VERTEXSIZE)
			{
				//work out tex coords
				os = v[7];
				ot = v[8];

				s = os/4 + (realtime*0.05);
				t = ot/4 + (realtime*0.05);
				ss= os/5 - (realtime*0.05);
				tt= ot/5 + (realtime*0.05);

				qglMultiTexCoord2fARB (GL_TEXTURE0_ARB,    s,    t);
				qglMultiTexCoord2fARB (GL_TEXTURE1_ARB,   ss,   tt);

				glVertex3fv (v);
			}
			glEnd ();
		}

		//check for another in the chain
		if (surf->extra){
			//if the next in the chain isnt a caustic surface and the current one was turn off caustics
			if (caustic && !surf->extra->flags & SURF_UNDERWATER && gl_caustics.value)
			{
				GL_DisableTMU(GL_TEXTURE0_ARB);
				GL_DisableTMU(GL_TEXTURE1_ARB);

				caustic = false;
			}
		}
		//if the surface isnt a shiny surface drop it
		if ((surf->flags & SURF_SHINY_GLASS && surf->flags & SURF_SHINY_METAL) || !gl_shiny.value){
			removelink = surf;			//we want to remove this surface from the list.
			surf = surf->extra;			//continue to next surface
			removelink->extra = NULL;	//remove the surface from the chain
			prev->extra = surf;			//set the previous surface to have this (the next) one as the next
		}else{
			prev = surf;				//set the this surface to be the previous one
			surf = surf->extra;			//move to next surface
		}
	}

	// Disable caustic
	GL_DisableTMU(GL_TEXTURE1_ARB);
	Surf_Reset();

	// Disable caustic
	GL_SelectTexture(GL_TEXTURE0_ARB);
	Surf_Reset();

//second pass
	//the glass shiny texture
	GL_SelectTexture(GL_TEXTURE0_ARB);
	Surf_EnableShiny();
	glBindTexture(GL_TEXTURE_2D, shinetex_glass);

	//the metal shiny texture
	GL_SelectTexture(GL_TEXTURE1_ARB);
	Surf_EnableShiny();
	glBindTexture(GL_TEXTURE_2D, shinetex_chrome);

	for (surf = extrachain; surf; )
	{
		if (!shiny_glass && surf->flags & SURF_SHINY_GLASS && gl_shiny.value)
		{
			GL_EnableTMU(GL_TEXTURE0_ARB);

			glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
			glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
			glEnable(GL_TEXTURE_GEN_S);
			glEnable(GL_TEXTURE_GEN_T);

			shiny_glass = true;
		}

		if (!shiny_metal && surf->flags & SURF_SHINY_METAL && gl_shiny.value)
		{
			GL_EnableTMU(GL_TEXTURE1_ARB);

			glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
			glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
			glEnable(GL_TEXTURE_GEN_S);
			glEnable(GL_TEXTURE_GEN_T);

			shiny_metal = true;
		}

		//if one of these textures needs drawing
		if (shiny_glass || shiny_metal){
			glBegin(GL_POLYGON);
			glNormal3fv(surf->plane->normal);
			v = surf->polys->verts[0];
			for (k=0 ; k<surf->polys->numverts ; k++, v+= VERTEXSIZE)
			{
				if (shiny_glass)
					qglMultiTexCoord2fARB (GL_TEXTURE0_ARB, v[7], v[8]);
				if (shiny_metal)
					qglMultiTexCoord2fARB (GL_TEXTURE1_ARB, v[7], v[8]);

				glVertex3fv (v);
			}
			glEnd ();
		}

		if (surf->extra){
			if (!surf->extra->flags & SURF_SHINY_GLASS && surf->flags & SURF_SHINY_GLASS && gl_shiny.value)
			{
				GL_DisableTMU(GL_TEXTURE2_ARB);

				glDisable(GL_TEXTURE_GEN_S);
				glDisable(GL_TEXTURE_GEN_T);

				shiny_glass = false;
			}

			if (!surf->extra->flags & SURF_SHINY_METAL && surf->flags & SURF_SHINY_METAL && gl_shiny.value)
			{
				GL_DisableTMU(GL_TEXTURE3_ARB);

				glDisable(GL_TEXTURE_GEN_S);
				glDisable(GL_TEXTURE_GEN_T);

				shiny_metal = false;
			}
		}

		removelink = surf;			//remove this surface
		surf = surf->extra;			//continue to next surface
		removelink->extra = NULL;	//remove link to next on last surface
	}

	// Disable shiny metal
	GL_DisableTMU(GL_TEXTURE1_ARB);
	glDisable(GL_TEXTURE_GEN_S);
	glDisable(GL_TEXTURE_GEN_T);
	Surf_Reset();

	// Disable shiny glass
	GL_SelectTexture(GL_TEXTURE0_ARB);
	glDisable(GL_TEXTURE_GEN_S);
	glDisable(GL_TEXTURE_GEN_T);
	Surf_Reset();


	GL_EnableTMU(GL_TEXTURE0_ARB);
	glDisable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(1,1,1,1);

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}