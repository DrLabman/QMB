/* gl_md3.c
 * Based on code from the Aftershock 3D rendering engine
 * Copyright (C) 1999 Stephen C. Taylor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "quakedef.h"
#ifdef Q3MODELS
#include "gl_md3.h"

#define TEMPHACK 1
#ifdef TEMPHACK
extern cvar_t temp1;
#endif

void R_MD3TagRotate (entity_t *e, model_t *tagmodel, char *tagname)
{
	int i;
	md3tag_t *tag = NULL;
	md3header_t *model = Mod_Extradata(tagmodel);
	float m[16];

	for (i=0; i<model->num_tags; i++)
	{
		md3tag_t *tags = (md3tag_t *)((byte *)model + model->tag_offs);
		if(Q_strcmp(tags[i].name, tagname)==0)
#if TEMPHACK
			if(temp1.value > model->num_frames)
				tag = &tags[(model->num_frames-1) * model->num_tags + i];
			else
				tag = &tags[(int)temp1.value * model->num_tags + i];
#else
			if(e->frame > model->num_frames)
				tag = &tags[(model->num_frames-1) * model->num_tags + i];
			else
				tag = &tags[e->frame * model->num_tags + i];
#endif
	}

	if(!tag)
	{
		Con_Printf("Tag not found in %s : %s\n", tagmodel->name, tagname);
		return;
	}

	m[0] = tag->rot[0][0];	m[4] = tag->rot[1][0];	m[8] = tag->rot[2][0];	m[12] = tag->pos[0];
	m[1] = tag->rot[0][1];	m[5] = tag->rot[1][1];	m[9] = tag->rot[2][1];	m[13] = tag->pos[1];
	m[2] = tag->rot[0][2];	m[6] = tag->rot[1][2];	m[10]= tag->rot[2][2];	m[14] = tag->pos[2];
	m[3] = 0;				m[7] = 0;				m[11]= 0;				m[15] = 1;

	glMultMatrixf(m);
}

void R_DrawQ3Model(entity_t *e, int shell, int outline)
{
	md3header_mem_t *model;
	int i, j, k;
	int frame;
	int lastframe;
	int vertframeoffset;
	int lastvertframeoffset;
	//int *tris;
	md3surface_mem_t *surf;
	md3shader_mem_t *shader;
	md3st_mem_t *tc;
	md3tri_mem_t *tris;
	md3vert_mem_t *verts, *vertslast;

	int	usevertexarray = true;

	extern vec3_t lightcolor;
	extern vec3_t	shadevector;
	extern	unsigned int	celtexture;
	extern	unsigned int	vertextexture;

	//md3 interpolation
	float blend, iblend;

	model = Mod_Extradata (e->model);

	if (*(long *)model->id != MD3IDHEADER){
		Con_Printf("MD3 bad model for: %s\n", model->filename);
		return;
	}

	if ((r_celshading.value || r_vertexshading.value) && !outline)
	{
	//setup for shading
		GL_SelectTexture(GL_TEXTURE1_ARB);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_TEXTURE_1D);
		if (r_celshading.value)
			glBindTexture (GL_TEXTURE_1D, celtexture);
		else
			glBindTexture (GL_TEXTURE_1D, vertextexture);
		GL_SelectTexture(GL_TEXTURE0_ARB);

		usevertexarray = false;
	}

	if (gl_ammoflash.value && (model->flags & EF_MODELFLASH)){
		lightcolor[0] += sin(2 * cl.time * M_PI)/4;
		lightcolor[1] += sin(2 * cl.time * M_PI)/4;
		lightcolor[2] += sin(2 * cl.time * M_PI)/4;
		//Con_Printf("Model flags: %x\n", model->flags);
	}

	//md3 interpolation
	e->frame_interval = 0.1f;
	if (e->pose2 != e->frame)
	{
		e->frame_start_time = realtime;
		e->pose1 = e->pose2;
		e->pose2 = e->frame;
		blend = 0;
		iblend = 1.0 - blend;
	} else {
		blend = (realtime - e->frame_start_time) / e->frame_interval;
		if (blend > 1)
			blend = 1;
		iblend = 1.0 - blend;
	}
	//md3 end

	//blend = 1;

//#ifdef EXTENDQC
//	glColor4f (l, l, l, currententity->alpha);
//#else
	if (!shell){
		if (!outline) {
			glColor3fv(lightcolor);
		}
	} else {
		usevertexarray = false;
	}

	glPushMatrix();

	//interpolate unless its the viewmodel
	if (e != &cl.viewent)
		R_BlendedRotateForEntity (e);
	else
		R_RotateForEntity (e);
	
	surf = (md3surface_mem_t *)((byte *)model + model->offs_surfaces);
	for (i = 0; i < model->num_surfaces; i++)
	{
		if (*(long *)surf->id != MD3IDHEADER){
			Con_Printf("MD3 bad surface for: %s\n", model->filename);
			surf = (md3surface_mem_t *)((byte *)surf + surf->offs_end);
			continue;
		}

		frame = e->frame;
		if (surf->num_surf_frames == 0){
			surf = (md3surface_mem_t *)((byte *)surf + surf->offs_end);
			continue;	//shouldn't ever do this, each surface should have at least one frame
		}

		frame = frame % surf->num_surf_frames;	//cap the frame inside the list of frames in the model
		vertframeoffset = frame * surf->num_surf_verts * sizeof(md3vert_mem_t);

		lastframe = e->pose1 % surf->num_surf_frames;
		lastvertframeoffset = lastframe * surf->num_surf_verts * sizeof(md3vert_mem_t);

		//get pointer to shaders
		shader = (md3shader_mem_t *)((byte *)surf + surf->offs_shaders);
		tc = (md3st_mem_t *)((byte *)surf + surf->offs_tc);
		tris = (md3tri_mem_t *)((byte *)surf + surf->offs_tris);
		verts = (md3vert_mem_t *)((byte *)surf + surf->offs_verts + vertframeoffset);
		vertslast = (md3vert_mem_t *)((byte *)surf + surf->offs_verts + lastvertframeoffset);

		if (!shell){
			if (surf->num_surf_shaders!=0)
				glBindTexture(GL_TEXTURE_2D, shader[(e->skinnum%surf->num_surf_shaders)].texnum);
			else
				glBindTexture(GL_TEXTURE_2D, 0);
		}

		if (blend >=1 && usevertexarray){
			glNormalPointer(GL_FLOAT, 6 * sizeof(float), (float *)verts->normal);
			glEnableClientState(GL_NORMAL_ARRAY);

			glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (float *)verts->vec);
			glEnableClientState(GL_VERTEX_ARRAY);

			glTexCoordPointer(2, GL_FLOAT, 0, (float *)tc);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);

			glDrawElements(GL_TRIANGLES,surf->num_surf_tris*3,GL_UNSIGNED_INT,(int *)tris);

			glDisableClientState(GL_NORMAL_ARRAY);
			glDisableClientState(GL_VERTEX_ARRAY);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}else{
			glBegin(GL_TRIANGLES);
				//for each triangle
				for (j = 0; j < surf->num_surf_tris; j++)
				{
					//draw the poly
					for (k=0; k < 3; k++){
						//interpolated
						vec3_t vec, normal;
					
						vec[0] = verts[tris[j].tri[k]].vec[0] * blend + vertslast[tris[j].tri[k]].vec[0] * iblend;
						vec[1] = verts[tris[j].tri[k]].vec[1] * blend + vertslast[tris[j].tri[k]].vec[1] * iblend;
						vec[2] = verts[tris[j].tri[k]].vec[2] * blend + vertslast[tris[j].tri[k]].vec[2] * iblend;

						normal[0] = verts[tris[j].tri[k]].normal[0] * blend + vertslast[tris[j].tri[k]].normal[0] * iblend;
						normal[1] = verts[tris[j].tri[k]].normal[1] * blend + vertslast[tris[j].tri[k]].normal[1] * iblend;
						normal[2] = verts[tris[j].tri[k]].normal[2] * blend + vertslast[tris[j].tri[k]].normal[2] * iblend;

						if (shell){
							VectorAdd(vec,normal,vec);
						}

						if (!shell){
							qglMTexCoord1fARB (GL_TEXTURE1_ARB, bound(0,DotProduct(shadevector,normal),1));
							qglMTexCoord2fARB (GL_TEXTURE0_ARB, tc[tris[j].tri[k]].s, tc[tris[j].tri[k]].t);
						}else{
							glTexCoord2f(tc[tris[j].tri[k]].s + realtime*2, tc[tris[j].tri[k]].t + realtime*2);
						}
						glVertex3fv(vec);
						glNormal3fv(normal);
					}
				}
			glEnd();
		}

		surf = (md3surface_mem_t *)((byte *)surf + surf->offs_end);
	}
	glPopMatrix();

	if ((r_celshading.value || r_vertexshading.value) && !outline)
	{
	//setup for shading
		GL_SelectTexture(GL_TEXTURE1_ARB);
		glDisable(GL_TEXTURE_1D);
		GL_SelectTexture(GL_TEXTURE0_ARB);
	}
}

extern char loadname[32];

void Mod_LoadQ3Model(model_t *mod, void *buffer)
{
	md3header_t *header;
	md3header_mem_t *mem_head;
	md3surface_t *surf;
	md3surface_mem_t *currentsurf;
	int i, j;//, size, skinnamelen;
	int posn;
	int surfstartposn;
	char name[128];

	//we want to work out the size of the model in memory
	
	//size of surfaces = surface size + num_shaders * shader_mem size + 
	//		num_triangles * tri size + num_verts * textcoord size +
	//		num_verts * vert_mem size
	int surf_size = 0;
	int mem_size = 0;

	//pointer to header
	header = (md3header_t *)buffer;
	//pointer to the surface list
	surf = (md3surface_t*)((byte *)buffer + header->surface_offs);

	surf_size = 0;
	for (i = 0; i < header->num_surfaces; i++)
	{
		surf_size += sizeof(md3surface_mem_t);
		surf_size += surf->num_surf_shaders * sizeof(md3shader_mem_t);
		surf_size += surf->num_surf_tris * sizeof(md3tri_mem_t);
		surf_size += surf->num_surf_verts * sizeof(md3st_mem_t);
		surf_size += surf->num_surf_verts * surf->num_surf_frames * sizeof(md3vert_mem_t);

		//goto next surf
		surf = (md3surface_t*)((byte *)surf + surf->end_offs);
	}

	//total size =	header size + num_frames * frame size + num_tags * tag size +
	//		size of surfaces
	mem_size = sizeof(md3header_mem_t);
	mem_size += header->num_frames * sizeof(md3frame_mem_t);
	mem_size += header->num_tags * sizeof(md3tag_mem_t);
	mem_size += surf_size;

	Con_DPrintf("Loading md3 model...%s (%s)\n", header->filename, mod->name);

	mem_head = (md3header_mem_t *)Cache_Alloc (&mod->cache, mem_size, mod->name);
	if (!mod->cache.data){
		return;	//cache alloc failed
	}

	//setup more mem stuff
	mod->type = mod_alias;
	mod->aliastype = MD3IDHEADER;
	mod->numframes = header->num_frames;

	//copy stuff across from disk buffer to memory
	posn = 0; //posn in new buffer

	//copy header
	Q_memcpy(mem_head, header, sizeof(md3header_t));
	posn += sizeof(md3header_mem_t);

	//posn of frames
	mem_head->offs_frames = posn;

	//copy frames
	Q_memcpy((byte *)mem_head + mem_head->offs_frames, (byte *)header + header->frame_offs, sizeof(md3frame_t)*header->num_frames);
	posn += sizeof(md3frame_mem_t)*header->num_frames;

	//posn of tags
	mem_head->offs_tags = posn;

	//copy tags
	Q_memcpy((byte *)mem_head + mem_head->offs_tags, (byte *)header + header->tag_offs, sizeof(md3tag_t)*header->num_tags);
	posn += sizeof(md3tag_mem_t)*header->num_tags;

	//posn of surfaces
	mem_head->offs_surfaces = posn;

	//copy surfaces, one at a time
	//get pointer to surface in file
	surf = (md3surface_t *)((byte *)header + header->surface_offs);
	//get pointer to surface in memory
	currentsurf = (md3surface_mem_t *)((byte *)mem_head + posn);
	surfstartposn = posn;

	for (i=0; i < header->num_surfaces; i++)
	{
		//copy size of surface
		Q_memcpy((byte *)mem_head + posn, (byte *)header + header->surface_offs, sizeof(md3surface_t));
		posn += sizeof(md3surface_mem_t);

		//posn of shaders for this surface
		currentsurf->offs_shaders = posn - surfstartposn;
		
		for (j=0; j < surf->num_surf_shaders; j++){
			//copy jth shader accross
			Q_memcpy((byte *)mem_head + posn, (byte *)surf + surf->shader_offs + j * sizeof(md3shader_t), sizeof(md3shader_t));
			posn += sizeof(md3shader_mem_t); //copyed non-mem into mem one
		}
		//posn of tris for this surface
		currentsurf->offs_tris = posn - surfstartposn;

		//copy tri
		Q_memcpy((byte *)mem_head + posn, (byte *)surf + surf->tris_offs, sizeof(md3tri_t) * surf->num_surf_tris);
		posn += sizeof(md3tri_mem_t) * surf->num_surf_tris;

		//posn of tex coords in this surface
		currentsurf->offs_tc = posn - surfstartposn;

		//copy st
		Q_memcpy((byte *)mem_head + posn, (byte *)surf + surf->tc_offs, sizeof(md3st_t) * surf->num_surf_verts);
		posn += sizeof(md3st_t) * surf->num_surf_verts;

		//posn points to surface->verts
		currentsurf->offs_verts = posn - surfstartposn;

		//next to have to be redone
		for (j=0; j < surf->num_surf_verts * surf->num_surf_frames; j++){
			float lat;
			float lng;

			//convert verts from shorts to floats
			md3vert_mem_t *mem_vert = (md3vert_mem_t *)((byte *)mem_head + posn);
			md3vert_t *disk_vert = (md3vert_t *)((byte *)surf + surf->vert_offs + j * sizeof(md3vert_t));
			mem_vert->vec[0] = (float)disk_vert->vec[0] / 64.0f;
			mem_vert->vec[1] = (float)disk_vert->vec[1] / 64.0f;
			mem_vert->vec[2] = (float)disk_vert->vec[2] / 64.0f;

			
			//work out normals
			lat = (disk_vert->normal + 255) * (2 * 3.141592654f) / 256.0f;
			lng = ((disk_vert->normal >> 8) & 255) * (2 * 3.141592654f) / 256.0f;
			mem_vert->normal[0] = -(float)(sin (lat) * cos (lng));
			mem_vert->normal[1] =  (float)(sin (lat) * sin (lng));
			mem_vert->normal[2] =  (float)(cos (lat) * 1);

			posn += sizeof(md3vert_mem_t); //copyed non-mem into mem one
		}

		//point to next surf (or eof)
		surf = (md3surface_t*)((byte *)surf + surf->end_offs);

		//posn points to the end of this surface
		currentsurf->offs_end = posn;
		//next start of surf (if there is one)
		surfstartposn = posn;
	}
	//posn should now equal mem_size
	if (posn != mem_size){
		Con_Printf("Copied diffrent ammount to what was worked out, copied: %i worked out: %i\n",posn, mem_size);
	}

	VectorCopy(((md3frame_mem_t *)((byte *)mem_head + mem_head->offs_frames))->mins, mod->mins);
	VectorCopy(((md3frame_mem_t *)((byte *)mem_head + mem_head->offs_frames))->maxs, mod->maxs);
	mod->flags = mem_head->flags;

	//get pointer to first surface
	currentsurf = (md3surface_mem_t *)((byte *)mem_head + mem_head->offs_surfaces);
	for (i=0; i<mem_head->num_surfaces; i++)
	{
		if (*(long *)currentsurf->id != MD3IDHEADER){
			Con_Printf("MD3 bad surface for: %s\n", mem_head->filename);

		}else {
			md3shader_mem_t *shader = (md3shader_mem_t *)((byte *)currentsurf + currentsurf->offs_shaders);
			
			for (j=0; j<currentsurf->num_surf_shaders; j++){
				//try loading texture here
				sprintf(&name[0],"progs/%s",shader[j].name);
				
				shader[j].texnum = GL_LoadTexImage(&name[0], false, true, gl_sincity.value);
				if (shader[j].texnum == 0){
					Con_Printf("Model: %s  Texture missing: %s\n", mod->name, shader[j].name);
				}
			}
		}
		currentsurf = (md3surface_mem_t *)((byte *)currentsurf + currentsurf->offs_end);
	}
}

#if 0
int debug = 0;
multimodel_t *Mod_AddMultiModel (entity_t *entity, model_t *mod);
void Mod_LoadQ3MultiModel (model_t *mod)
{
	multimodel_t *head, *upper, *lower;
	int handle;
	char path[MAX_QPATH];
	mod->type = mod_null;

	sprintf(path, "%sanimation.cfg", mod->name);
	COM_OpenFile(path, &handle);

	if(handle) //Q3Player
	{
		COM_CloseFile(handle);
		//Load player
		lower = Mod_AddMultiModel(NULL, mod);//Allocate a mmodel
		lower->model = Z_Malloc(sizeof(model_t));
		Q_strcpy(lower->model->name, va("%slower.md3", mod->name));
		lower->model->needload = TRUE;
		Mod_LoadModel(lower->model, 1);
		Q_strcpy(lower->identifier, "lower");

		lower->linktype = MULTIMODEL_LINK_STANDARD;


		upper = Mod_AddMultiModel(NULL, mod);//Allocate a mmodel
		upper->model = Z_Malloc(sizeof(model_t));
		Q_strcpy(upper->model->name, va("%supper.md3", mod->name));
		upper->model->needload = TRUE;
		Mod_LoadModel(upper->model, 1);
		Q_strcpy(upper->identifier, "upper");

		upper->linktype = MULTIMODEL_LINK_TAG;
		upper->linkedmodel = lower;
		Q_strcpy(upper->tagname, "tag_torso");


		head = Mod_AddMultiModel(NULL, mod);//Allocate a mmodel
		head->model = Z_Malloc(sizeof(model_t));
		Q_strcpy(head->model->name, va("%shead.md3", mod->name));
		head->model->needload = TRUE;
		Mod_LoadModel(head->model, 1);
		Q_strcpy(head->identifier, "head");

		head->linktype = MULTIMODEL_LINK_TAG;
		head->linkedmodel = upper;
		Q_strcpy(head->tagname, "tag_head");
		
		return;
	}

	sprintf(path, "%s", mod->name);
	Q_strcat(path, Q_strrchr(path, '/'));
	path[Q_strlen(path)-1] = '\0';
	Q_strcat(path, "_hand.md3");
	Sys_Error("Trying to find weaponmodel %s", path);
	COM_OpenFile(path, &handle);
	if(handle) //W_Weapon
	{
		COM_CloseFile(handle);
	}
}
#endif

#endif