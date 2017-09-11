/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Authors: Jean Le Feuvre
 *			Copyright (c) Telecom ParisTech 2017
 *					All rights reserved
 *
 *  This file is part of GPAC / generic FILE input filter
 *
 *  GPAC is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  GPAC is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */


#include <gpac/filters.h>
/*for GF_STREAM_PRIVATE_SCENE definition*/
#include <gpac/constants.h>
#include <gpac/download.h>
#include <gpac/xml.h>

typedef struct
{
	//options
	const char *src;
	u32 block_size;

	//only one output pid declared
	GF_FilterPid *pid;

	FILE *file;
	u64 file_size;

	char *block;
	u32 start;
} GF_FileInCtx;


GF_FilterPid * filein_declare_pid(GF_Filter *filter, const char *url, const char *local_file, const char *mime_type, char *probe_data, u32 probe_size)
{
	u32 oti=0;
	char *ext = NULL;
	GF_FilterPid *pid;
	//declare a single PID carrying FILE data pid
	pid = gf_filter_pid_new(filter);

	if (local_file)
		gf_filter_pid_set_property(pid, GF_PROP_PID_FILEPATH, &PROP_STRING((char *)local_file));

	gf_filter_pid_set_property(pid, GF_PROP_PID_URL, &PROP_STRING((char *)url));

	ext = strrchr(url, '.');
	if (ext && !stricmp(ext, ".gz")) {
		char *anext;
		ext[0] = 0;
		anext = strrchr(url, '.');
		ext[0] = '.';
		ext = anext;
	}
	if (ext) ext++;

	//TODO - make this generic
	if (!mime_type && probe_data) {
		if (strstr(probe_data, "<XMT-A") || strstr(probe_data, ":mpeg4:xmta:")) {
			mime_type = "application/x-xmt";
		} else if (strstr(probe_data, "InitialObjectDescriptor")
			|| (strstr(probe_data, "EXTERNPROTO") && strstr(probe_data, "gpac:"))
		) {
			mime_type = "application/x-bt";
		} else if (strstr(probe_data, "#VRML V2.0 utf8")) {
			mime_type = "model/vrml";
		} else if ( strstr(probe_data, "#X3D V3.0")) {
			mime_type = "model/x3d+vrml";
		} else if (strstr(probe_data, "<X3D") || strstr(probe_data, "/x3d-3.0.dtd")) {
			mime_type = "model/x3d+xml";
		} else if (strstr(probe_data, "<saf") || strstr(probe_data, "mpeg4:SAF:2005")
			|| strstr(probe_data, "mpeg4:LASeR:2005")
		) {
			mime_type = "application/x-LASeR+xml";
		} else if (strstr(probe_data, "<svg") || strstr(probe_data, "w3.org/2000/svg") ) {
			mime_type = "application/widget";
		}
	}

	if (ext)
		gf_filter_pid_set_property(pid, GF_PROP_PID_FILE_EXT, &PROP_STRING(ext));
	if (mime_type)
		gf_filter_pid_set_property(pid, GF_PROP_PID_MIME, &PROP_STRING(mime_type));

	return pid;
}


GF_Err filein_initialize(GF_Filter *filter)
{
	GF_FileInCtx *ctx = (GF_FileInCtx *) gf_filter_get_udta(filter);
	char *frag_par = NULL;
	char *cgi_par = NULL;
	char *src;

	if (!ctx || !ctx->src) return GF_BAD_PARAM;

	if (strnicmp(ctx->src, "file:/", 6) && strstr(ctx->src, "://"))  {
		gf_filter_setup_failure(filter, GF_NOT_SUPPORTED);
		return GF_NOT_SUPPORTED;
	}

	//local file

	//strip any fragment identifer
	frag_par = strchr(ctx->src, '#');
	if (frag_par) frag_par[0] = 0;
	cgi_par = strchr(ctx->src, '?');
	if (cgi_par) cgi_par[0] = 0;

	src = ctx->src;
	if (!strnicmp(ctx->src, "file://", 7)) src += 7;
	else if (!strnicmp(ctx->src, "file:", 5)) src += 5;

	ctx->file = gf_fopen(src, "rt");
	if (!ctx->file) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("[FileIn] Failed to open %s\n", src));

		if (frag_par) frag_par[0] = '#';
		if (cgi_par) cgi_par[0] = '?';

		gf_filter_setup_failure(filter, GF_URL_ERROR);
		return GF_URL_ERROR;
	}
	gf_fseek(ctx->file, 0, SEEK_END);
	ctx->file_size = gf_ftell(ctx->file);
	gf_fseek(ctx->file, 0, SEEK_SET);

	if (frag_par) frag_par[0] = '#';
	if (cgi_par) cgi_par[0] = '?';

	ctx->block = gf_malloc(ctx->block_size +1);

	return GF_OK;
}

void filein_finalize(GF_Filter *filter)
{
	GF_FileInCtx *ctx = (GF_FileInCtx *) gf_filter_get_udta(filter);

	if (ctx->file) gf_fclose(ctx->file);
	if (ctx->block) gf_free(ctx->block);

}

GF_FilterProbeScore filein_probe_url(const char *url, const char *mime_type)
{
	char *frag_par = NULL;
	char *cgi_par = NULL;
	char *src = url;
	Bool res;
	if (!strnicmp(url, "file://", 7)) src += 7;
	else if (!strnicmp(url, "file:", 5)) src += 5;

	//strip any fragment identifer
	frag_par = strchr(url, '#');
	if (frag_par) frag_par[0] = 0;
	cgi_par = strchr(url, '?');
	if (cgi_par) cgi_par[0] = 0;

	res = gf_file_exists(src);

	if (frag_par) frag_par[0] = '#';
	if (cgi_par) cgi_par[0] = '?';

	return res ? GF_FPROBE_SUPPORTED : GF_FPROBE_NOT_SUPPORTED;
}

static Bool filein_process_event(GF_Filter *filter, GF_FilterEvent *com)
{
	GF_FileInCtx *ctx = (GF_FileInCtx *) gf_filter_get_udta(filter);

	if (!com->base.on_pid) return GF_FALSE;
	if (com->base.on_pid != ctx->pid) return GF_FALSE;

	switch (com->base.type) {
	case GF_FEVT_PLAY:
		ctx->start = (u32) (1000 * com->play.start_range);
		return GF_TRUE;
	case GF_FEVT_STOP:
		return GF_TRUE;
	default:
		break;
	}
	return GF_FALSE;
}

static GF_Err filein_process(GF_Filter *filter)
{
	u32 nb_read;
	GF_FilterPacket *pck;
	char *pck_data;
	GF_FileInCtx *ctx = (GF_FileInCtx *) gf_filter_get_udta(filter);

	if (ctx->pid) return GF_EOS;

	nb_read = fread(ctx->block, 1, ctx->block_size, ctx->file);

	ctx->block[nb_read] = 0;
	ctx->pid = filein_declare_pid(filter, ctx->src, ctx->src, NULL, ctx->block, nb_read);
	if (!ctx->pid) return GF_SERVICE_ERROR;

	pck = gf_filter_pck_new_shared(ctx->pid, ctx->block, 0, NULL);
	if (!pck) return GF_OK;

	gf_filter_pck_set_cts(pck, ctx->start);

	gf_filter_pck_set_framing(pck, GF_TRUE, GF_TRUE);
	gf_filter_pck_set_sap(pck, 1);
	gf_filter_pck_set_property(pck, GF_PROP_PCK_BYTE_OFFSET, &PROP_LONGUINT( 0 ));

	gf_filter_pck_send(pck);

	gf_filter_pid_set_eos(ctx->pid);
	return GF_EOS;
}



#define OFFS(_n)	#_n, offsetof(GF_FileInCtx, _n)

static const GF_FilterArgs FileInArgs[] =
{
	{ OFFS(src), "location of source content", GF_PROP_NAME, NULL, NULL, GF_FALSE},
	{ OFFS(block_size), "block size used to read file", GF_PROP_UINT, "2048", NULL, GF_FALSE},
	{}
};

GF_FilterRegister FileInRegister = {
	.name = "filein",
	.description = "Generic File Input",
	.private_size = sizeof(GF_FileInCtx),
	.args = FileInArgs,
	.initialize = filein_initialize,
	.finalize = filein_finalize,
	.process = filein_process,
	.configure_pid = NULL,
	.update_arg = NULL,
	.process_event = filein_process_event,
	.probe_url = filein_probe_url
};


const GF_FilterRegister *filein_register(GF_FilterSession *session)
{
	return &FileInRegister;
}

