
#include "mrc_io_private.h"
#include <mrc_params.h>
#include <mrc_dict.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

// TODO:
// 2D serial output (?)
// scaling for serial output
// vector/scalar

enum {
  ID_DIAGS = 14000,
  ID_DIAGS_CMD,
  ID_DIAGS_CREATE_OUTDIR,
  ID_DIAGS_CREATE_BASENAME,
  ID_DIAGS_CMD_CREATE,
  ID_DIAGS_CMD_OPEN,
  ID_DIAGS_CMD_WRITE,
  ID_DIAGS_CMD_WRITE_ATTR,
  ID_DIAGS_CMD_CRDX,
  ID_DIAGS_CMD_CRDY,
  ID_DIAGS_CMD_CRDZ,
  ID_DIAGS_BASENAME,
  ID_DIAGS_TIME,
  ID_DIAGS_FLDNAME,
  ID_DIAGS_SUBDOMAIN,
  ID_DIAGS_DATA,
  ID_DIAGS_2DDATA,
};

enum {
  DIAG_CMD_CREATE,
  DIAG_CMD_OPENFILE,
  DIAG_CMD_SHUTDOWN,
};

// ======================================================================
// diag client interface

struct diagc_combined_params {
  int rank_diagsrv;
};

#define VAR(x) (void *)offsetof(struct diagc_combined_params, x)

static struct param diagc_combined_params_descr[] = {
  { "rank_diagsrv"        , VAR(rank_diagsrv)      , PARAM_INT(0)       },
  {},
};

#undef VAR

// ----------------------------------------------------------------------
// diagc_combined_setup
//
// called once before first open, will do handshake with server

static void
diagc_combined_send_domain_info(struct mrc_io *io, struct mrc_domain *domain)
{
  if (io->diagc_domain_info_sent)
    return;

  struct diagc_combined_params *par = io->obj.subctx;
    
  int iw[9], *off = iw, *ldims = iw + 3, *gdims = iw + 6;
  mrc_domain_get_local_offset_dims(domain, off, ldims);
  mrc_domain_get_global_dims(domain, gdims);
  MPI_Send(iw, 9, MPI_INT, par->rank_diagsrv, ID_DIAGS_CMD_CREATE, MPI_COMM_WORLD);

  struct mrc_crds *crds = mrc_domain_get_crds(domain);
  for (int d = 0; d < 3; d++) {
    MPI_Send(&MRC_CRD(crds, d, 2), ldims[d], MPI_FLOAT, par->rank_diagsrv,
	     ID_DIAGS_CMD_CRDX + d, MPI_COMM_WORLD); // FIXME, hardcoded SW
  }

  io->diagc_domain_info_sent = true;
}

static void
diagc_combined_setup(struct mrc_obj *obj)
{
  struct mrc_io *io = to_mrc_io(obj);
  struct mrc_io_params *par_io = &io->par;
  struct diagc_combined_params *par = io->obj.subctx;

  if (io->rank == 0) {
    int icmd[1] = { DIAG_CMD_CREATE };
    MPI_Send(icmd, 1, MPI_INT, par->rank_diagsrv, ID_DIAGS_CMD, MPI_COMM_WORLD);
    MPI_Send(par_io->outdir, strlen(par_io->outdir) + 1, MPI_CHAR, par->rank_diagsrv,
	     ID_DIAGS_CREATE_OUTDIR, MPI_COMM_WORLD);
    MPI_Send(par_io->basename, strlen(par_io->basename) + 1, MPI_CHAR, par->rank_diagsrv,
	     ID_DIAGS_CREATE_BASENAME, MPI_COMM_WORLD);
  }
}

// ----------------------------------------------------------------------
// diagc_combined_open
//
// opens a new output file (index itime)
// afterwards, diag_write_field() may be called repeatedly and
// must be concluded with a call to diag_close()

static void
diagc_combined_open(struct mrc_io *io, const char *mode)
{
  struct diagc_combined_params *par = io->obj.subctx;
  assert(strcmp(mode, "w") == 0); // only writing supported for now

  if (io->rank == 0) {
    int icmd[2] = { DIAG_CMD_OPENFILE, io->step };

    MPI_Send(icmd, 2, MPI_INT, par->rank_diagsrv, ID_DIAGS_CMD_OPEN, MPI_COMM_WORLD);
    MPI_Send(io->par.basename, strlen(io->par.basename) + 1, MPI_CHAR,
	     par->rank_diagsrv, ID_DIAGS_BASENAME, MPI_COMM_WORLD);
    MPI_Send(&io->time, 1, MPI_FLOAT, par->rank_diagsrv, ID_DIAGS_TIME, MPI_COMM_WORLD);
  }
}

// ----------------------------------------------------------------------
// diagc_combined_close

static void
diagc_combined_close(struct mrc_io *io)
{
  struct diagc_combined_params *par = io->obj.subctx;

  if (io->rank == 0) {
    char str[] = "";
    MPI_Send(str, 1, MPI_CHAR, par->rank_diagsrv, ID_DIAGS_FLDNAME, MPI_COMM_WORLD);
  }
}

// ----------------------------------------------------------------------
// diagc_combined_destroy
//
// shuts down the diag server process

static void
diagc_combined_destroy(struct mrc_obj *obj)
{
  struct mrc_io *io = (struct mrc_io *) obj;
  struct diagc_combined_params *par = io->obj.subctx;

  if (io->rank == 0) {
    int icmd[1] = { DIAG_CMD_SHUTDOWN };
    MPI_Send(icmd, 1, MPI_INT, par->rank_diagsrv, ID_DIAGS_CMD, MPI_COMM_WORLD);
  }
}

// ----------------------------------------------------------------------
// diagc_combined_write_field

static void
copy_and_scale(float *buf, struct mrc_f3 *fld, int m, float scale)
{
  int i = 0;
  for (int iz = 2; iz < fld->im[2] - 2; iz++) {
    for (int iy = 2; iy < fld->im[1] - 2; iy++) {
      for (int ix = 2; ix < fld->im[0] - 2; ix++) {
	buf[i++] = scale * MRC_F3(fld, m, ix,iy,iz);
      }
    }
  }
}

static void
diagc_combined_write_field(struct mrc_io *io, const char *path,
			   float scale, struct mrc_f3 *fld, int m)
{
  struct diagc_combined_params *par = io->obj.subctx;

  diagc_combined_send_domain_info(io, fld->domain);

  int ldims[3];
  mrc_domain_get_local_offset_dims(fld->domain, NULL, ldims);
  assert(ldims[0] == fld->im[0]-4 && ldims[1] == fld->im[1]-4 && ldims[2] == fld->im[2]-4);
  int nout = ldims[0] * ldims[1] * ldims[2];
  float *buf = calloc(sizeof(float), nout);

  if (io->rank == 0) {
    MPI_Send((char *)mrc_f3_name(fld), strlen(mrc_f3_name(fld)) + 1, MPI_CHAR, par->rank_diagsrv,
	     ID_DIAGS_FLDNAME, MPI_COMM_WORLD);
    MPI_Send(fld->name[m], strlen(fld->name[m]) + 1, MPI_CHAR, par->rank_diagsrv,
	     ID_DIAGS_FLDNAME, MPI_COMM_WORLD);
    int outtype = DIAG_TYPE_3D;
    MPI_Send(&outtype, 1, MPI_INT, par->rank_diagsrv,
	     ID_DIAGS_CMD_WRITE, MPI_COMM_WORLD);
  }

  copy_and_scale(buf, fld, m, scale);

  int iw[6], *off = iw, *dims = iw + 3; // off, then dims
  mrc_domain_get_local_offset_dims(fld->domain, off, dims);

  MPI_Send(iw, 6, MPI_INT, par->rank_diagsrv, ID_DIAGS_SUBDOMAIN, MPI_COMM_WORLD);
  MPI_Send(buf, nout, MPI_FLOAT, par->rank_diagsrv, ID_DIAGS_DATA, MPI_COMM_WORLD);

  free(buf);
}

static void
diagc_combined_write_field2d(struct mrc_io *io, float scale, struct mrc_f2 *fld,
			     int outtype, float sheet)
{
  struct diagc_combined_params *par = io->obj.subctx;

  diagc_combined_send_domain_info(io, fld->domain);

  assert(outtype >= DIAG_TYPE_2D_X && outtype <= DIAG_TYPE_2D_Z);
  int dim = outtype - DIAG_TYPE_2D_X;

  if (io->rank == 0) {
    MPI_Send("mrc_f2", strlen("mrc_f2") + 1, MPI_CHAR, par->rank_diagsrv,
	     ID_DIAGS_FLDNAME, MPI_COMM_WORLD);
    MPI_Send(fld->name[0], strlen(fld->name[0]) + 1, MPI_CHAR, par->rank_diagsrv,
	     ID_DIAGS_FLDNAME, MPI_COMM_WORLD);
    MPI_Send(&outtype, 1, MPI_INT, par->rank_diagsrv,
	     ID_DIAGS_CMD_WRITE, MPI_COMM_WORLD);
    MPI_Send(&sheet, 1, MPI_FLOAT, par->rank_diagsrv,
	     ID_DIAGS_CMD_WRITE, MPI_COMM_WORLD);
  }

  int iw[6] = { -1, };
  if (fld->arr) { // part of the slice?
    int *off = iw, *dims = iw + 3; // off, then dims
    mrc_domain_get_local_offset_dims(fld->domain, off, dims);
    off[dim] = 0;
    dims[dim] = 1;

    MPI_Send(iw, 6, MPI_INT, par->rank_diagsrv, ID_DIAGS_SUBDOMAIN, MPI_COMM_WORLD);
    MPI_Send(fld->arr, fld->len, MPI_FLOAT, par->rank_diagsrv, ID_DIAGS_2DDATA, MPI_COMM_WORLD);
  } else {
    MPI_Send(iw, 6, MPI_INT, par->rank_diagsrv, ID_DIAGS_SUBDOMAIN, MPI_COMM_WORLD);
  }
}

static void
diagc_combined_write_attr(struct mrc_io *io, const char *path, int type,
			  const char *name, union param_u *pv)
{
  struct diagc_combined_params *par = io->obj.subctx;

  if (io->rank == 0) {
    MPI_Send((char *)path, strlen(path) + 1, MPI_CHAR, par->rank_diagsrv,
	     ID_DIAGS_CMD_WRITE_ATTR, MPI_COMM_WORLD);
    MPI_Send(&type, 1, MPI_INT, par->rank_diagsrv,
	     ID_DIAGS_CMD_WRITE_ATTR, MPI_COMM_WORLD);
    MPI_Send((char *)name, strlen(name) + 1, MPI_CHAR, par->rank_diagsrv,
	     ID_DIAGS_CMD_WRITE_ATTR, MPI_COMM_WORLD);
    switch (type) {
    case PT_INT:
    case PT_SELECT:
      MPI_Send(&pv->u_int, 1, MPI_INT, par->rank_diagsrv,
	       ID_DIAGS_CMD_WRITE_ATTR, MPI_COMM_WORLD);
      break;
    case PT_FLOAT:
      MPI_Send(&pv->u_float, 1, MPI_FLOAT, par->rank_diagsrv,
	       ID_DIAGS_CMD_WRITE_ATTR, MPI_COMM_WORLD);
      break;
    case PT_STRING:
      MPI_Send((char *)pv->u_string, strlen(pv->u_string) + 1, MPI_CHAR, par->rank_diagsrv,
	       ID_DIAGS_CMD_WRITE_ATTR, MPI_COMM_WORLD);
      break;
    default:
      assert(0);
    }
  }
}

static struct mrc_io_ops mrc_io_ops_combined = {
  .name          = "combined",
  .size          = sizeof(struct diagc_combined_params),
  .param_descr   = diagc_combined_params_descr,
  .setup         = diagc_combined_setup,
  .destroy       = diagc_combined_destroy,
  .open          = diagc_combined_open,
  .write_field   = diagc_combined_write_field,
  .write_field2d = diagc_combined_write_field2d,
  .write_attr    = diagc_combined_write_attr,
  .close         = diagc_combined_close,
};

// ======================================================================
// diag server one

// generic diagnostics server running on one node.
// a particular implementation needs to take care of opening files/
// writing/closing them,
// the generic part here (in conjunction with the client side above)
// takes care of communicating / assembling the fields

// ----------------------------------------------------------------------

struct diagsrv_one {
  list_t mrc_io_list;

  // only valid from open() -> close()
  struct mrc_io *io;

  struct diagsrv_srv_ops *srv_ops;
  void *srv;
};

struct diagsrv_srv_ops {
  const char *name;
  void  (*create)(struct diagsrv_one *ds, struct mrc_domain *domain);
  void  (*destroy)(struct diagsrv_one *ds);
  void  (*open)(struct diagsrv_one *ds, int step, float time);
  void  (*get_gfld_2d)(struct diagsrv_one *ds, struct mrc_f2 *f2, int dims[2]);
  struct mrc_f3 *(*get_gfld_3d)(struct diagsrv_one *ds, int dims[3]);
  void  (*put_gfld_2d)(struct diagsrv_one *ds, struct mrc_f2 *f2, char *fld_name, 
		       int outtype, float sheet);
  void  (*put_gfld_3d)(struct diagsrv_one *ds, struct mrc_f3 *f3);
  void  (*write_attr)(struct diagsrv_one *ds, const char *path, int type,
		      const char *name, union param_u *pv);
  void  (*close)(struct diagsrv_one *ds);
};

// ----------------------------------------------------------------------

struct mrc_io_entry {
  struct mrc_io *io;
  struct mrc_domain *domain;
  int ldims[3];

  char *basename;
  list_t entry;
};

static struct mrc_io_entry *
find_diagsrv_io(struct diagsrv_one *ds, const char *basename)
{
  struct mrc_io_entry *p;
  list_for_each_entry(p, &ds->mrc_io_list, entry) {
    if (strcmp(p->basename, basename) == 0) {
      return p;
    }
  }
  return NULL;
}

static void
create_diagsrv_io(struct diagsrv_one *ds,
		  const char *format, const char *outdir, const char *basename)
{
  struct mrc_io_entry *p = calloc(1, sizeof(*p));
  p->io = mrc_io_create(MPI_COMM_SELF);
  mrc_io_set_type(p->io, format);
  mrc_io_set_param_string(p->io, "outdir", outdir);
  mrc_io_set_param_string(p->io, "basename", basename);
  mrc_io_setup(p->io);
  mrc_io_view(p->io);

  p->basename = strdup(basename);
  list_add_tail(&p->entry, &ds->mrc_io_list);
}

// ----------------------------------------------------------------------
// diagsrv_one

struct diagsrv_srv {
  float *gfld;
  struct mrc_domain *domain;
};

static void
ds_srv_create(struct diagsrv_one *ds, struct mrc_domain *domain)
{
  struct diagsrv_srv *srv = malloc(sizeof(*srv));
  srv->domain = domain;
  int gdims[3];
  mrc_domain_get_global_dims(domain, gdims);
  int nglobal = gdims[0] * gdims[1] * gdims[2];
  srv->gfld = malloc(nglobal * sizeof(float));
  ds->srv = srv;
}

static void
ds_srv_destroy(struct diagsrv_one *ds)
{
  struct diagsrv_srv *srv = (struct diagsrv_srv *) ds->srv;
  free(srv->gfld);
  free(srv);
}

static void
ds_srv_open(struct diagsrv_one *ds, int step, float time)
{
  mrc_io_open(ds->io, "w", step, time);
}

static struct mrc_f3 *
ds_srv_get_gfld_3d(struct diagsrv_one *ds, int gdims[3])
{
  struct diagsrv_srv *srv = (struct diagsrv_srv *) ds->srv;
  struct mrc_f3 *f3 = mrc_domain_f3_create(srv->domain, SW_0);
  mrc_f3_set_array(f3, srv->gfld);
  mrc_f3_setup(f3);
  return f3;
}

static void
ds_srv_get_gfld_2d(struct diagsrv_one *ds, struct mrc_f2 *f2, int gdims[2])
{
  struct diagsrv_srv *srv = (struct diagsrv_srv *) ds->srv;
  mrc_f2_alloc_with_array(f2, NULL, gdims, 1, srv->gfld);
  f2->domain = srv->domain; // FIXME, quite a hack
}

static void
ds_srv_put_gfld_2d(struct diagsrv_one *ds, struct mrc_f2 *gfld, char *fld_name,
		   int outtype, float sheet)
{
  mrc_io_write_field2d(ds->io, 1., gfld, outtype, sheet);
}

static void
ds_srv_put_gfld_3d(struct diagsrv_one *ds, struct mrc_f3 *gfld)
{
  mrc_f3_write(gfld, ds->io);
  mrc_f3_destroy(gfld);
}

static void
ds_srv_close(struct diagsrv_one *ds)
{
  mrc_io_close(ds->io);
}

static void
ds_srv_write_attr(struct diagsrv_one *ds, const char *path, int type,
		  const char *name, union param_u *pv)
{
  mrc_io_write_attr(ds->io, path, type, name, pv);
}

struct diagsrv_srv_ops ds_srv_ops = {
  .name        = "nocache",
  .create      = ds_srv_create,
  .destroy     = ds_srv_destroy,
  .open        = ds_srv_open,
  .get_gfld_2d = ds_srv_get_gfld_2d,
  .get_gfld_3d = ds_srv_get_gfld_3d,
  .put_gfld_2d = ds_srv_put_gfld_2d,
  .put_gfld_3d = ds_srv_put_gfld_3d,
  .write_attr  = ds_srv_write_attr,
  .close       = ds_srv_close,
};

// ----------------------------------------------------------------------
// diagsrv_srv_cache

#define MAX_FIELDS (21)

struct mrc_attrs_entry {
  struct mrc_dict *attrs;
  char *path;
  list_t entry;
};

struct diagsrv_srv_cache_ctx {
  char *obj_names[MAX_FIELDS];
  char *fld_names[MAX_FIELDS];
  int outtypes[MAX_FIELDS];
  float sheets[MAX_FIELDS];
  float *gflds[MAX_FIELDS];
  struct mrc_domain *domain;
  int nr_flds;
  int step;
  float time;
  list_t attrs_list;
};

static void
ds_srv_cache_create(struct diagsrv_one *ds, struct mrc_domain *domain)
{
  struct diagsrv_srv_cache_ctx *srv = malloc(sizeof(*srv));
  srv->domain = domain;
  int gdims[3];
  mrc_domain_get_global_dims(domain, gdims);
  int nglobal = gdims[0] * gdims[1] * gdims[2];
  for (int i = 0; i < MAX_FIELDS; i++) {
    srv->obj_names[i] = NULL;
    srv->fld_names[i] = NULL;
    srv->gflds[i] = malloc(nglobal * sizeof(float));
  }
  ds->srv = srv;
}

static void
ds_srv_cache_open(struct diagsrv_one *ds, int step, float time)
{
  struct diagsrv_srv_cache_ctx *srv = (struct diagsrv_srv_cache_ctx *) ds->srv;
  srv->nr_flds = 0;
  srv->step = step;
  srv->time = time;
  INIT_LIST_HEAD(&srv->attrs_list);
}

static void
ds_srv_cache_destroy(struct diagsrv_one *ds)
{
  struct diagsrv_srv_cache_ctx *srv = (struct diagsrv_srv_cache_ctx *) ds->srv;
  for (int i = 0; i < MAX_FIELDS; i++) {
    free(srv->obj_names[i]);
    free(srv->fld_names[i]);
    free(srv->gflds[i]);
  }
  free(srv);
}

static void
ds_srv_cache_get_gfld_2d(struct diagsrv_one *ds, struct mrc_f2 *f2, int gdims[2])
{
  struct diagsrv_srv_cache_ctx *srv = (struct diagsrv_srv_cache_ctx *) ds->srv;
  assert(srv->nr_flds < MAX_FIELDS);
  free(srv->fld_names[srv->nr_flds]);
  mrc_f2_alloc_with_array(f2, NULL, gdims, 1, srv->gflds[srv->nr_flds]);
  f2->domain = srv->domain;
}

static struct mrc_f3 *
ds_srv_cache_get_gfld_3d(struct diagsrv_one *ds, int gdims[3])
{
  struct diagsrv_srv_cache_ctx *srv = (struct diagsrv_srv_cache_ctx *) ds->srv;
  assert(srv->nr_flds < MAX_FIELDS);
  free(srv->obj_names[srv->nr_flds]);
  free(srv->fld_names[srv->nr_flds]);
  struct mrc_f3 *f3 = mrc_domain_f3_create(srv->domain, SW_0);
  mrc_f3_set_array(f3, srv->gflds[srv->nr_flds]);
  mrc_f3_setup(f3);
  return f3;
}

static void
ds_srv_cache_put_gfld_2d(struct diagsrv_one *ds, struct mrc_f2 *gfld, char *fld_name,
			 int outtype, float sheet)
{
  struct diagsrv_srv_cache_ctx *srv = (struct diagsrv_srv_cache_ctx *) ds->srv;
  assert(srv->nr_flds < MAX_FIELDS);
  srv->fld_names[srv->nr_flds] = strdup(fld_name);
  srv->outtypes[srv->nr_flds] = outtype;
  srv->sheets[srv->nr_flds] = sheet;
  srv->nr_flds++;
}

static void
ds_srv_cache_put_gfld_3d(struct diagsrv_one *ds, struct mrc_f3 *f3)
{
  struct diagsrv_srv_cache_ctx *srv = (struct diagsrv_srv_cache_ctx *) ds->srv;
  assert(srv->nr_flds < MAX_FIELDS);
  srv->obj_names[srv->nr_flds] = strdup(mrc_f3_name(f3));
  srv->fld_names[srv->nr_flds] = strdup(f3->name[0]);
  srv->outtypes[srv->nr_flds] = DIAG_TYPE_3D;
  srv->nr_flds++;
  mrc_f3_destroy(f3);
}

static void
ds_srv_cache_write_attr(struct diagsrv_one *ds, const char *path, int type,
			const char *name, union param_u *pv)
{
  struct diagsrv_srv_cache_ctx *srv = (struct diagsrv_srv_cache_ctx *) ds->srv;

  struct mrc_attrs_entry *p;
  list_for_each_entry(p, &srv->attrs_list, entry) {
    if (strcmp(p->path, path) == 0)
      goto found;
  }
  
  p = calloc(1, sizeof(*p));
  p->attrs = mrc_dict_create(mrc_domain_comm(srv->domain));
  mrc_dict_set_name(p->attrs, path);
  p->path = strdup(path);
  list_add_tail(&p->entry, &srv->attrs_list);

 found:
  mrc_dict_add(p->attrs, type, name, pv);
}

static void
ds_srv_cache_close(struct diagsrv_one *ds)
{
  struct diagsrv_srv_cache_ctx *srv = (struct diagsrv_srv_cache_ctx *) ds->srv;
  int gdims[3];
  mrc_domain_get_global_dims(srv->domain, gdims);

  mrc_io_open(ds->io, "w", srv->step, srv->time);

  for (int i = 0; i < srv->nr_flds; i++) {
    switch (srv->outtypes[i]) {
    case DIAG_TYPE_3D: {
      struct mrc_f3 *gfld = mrc_domain_f3_create(srv->domain, SW_0);
      mrc_f3_set_array(gfld, srv->gflds[i]);
      mrc_f3_set_name(gfld, srv->obj_names[i]);
      gfld->name[0] = strdup(srv->fld_names[i]);
      mrc_f3_setup(gfld);
      mrc_f3_write(gfld, ds->io);
      mrc_f3_destroy(gfld);
      break;
    }
    case DIAG_TYPE_2D_X: {
      struct mrc_f2 gfld2;
      mrc_f2_alloc_with_array(&gfld2, NULL, (int [2]) { gdims[1], gdims[2] }, 1, srv->gflds[i]);
      gfld2.domain = srv->domain;
      gfld2.name[0] = strdup(srv->fld_names[i]);
      mrc_io_write_field2d(ds->io, 1., &gfld2, DIAG_TYPE_2D_X, srv->sheets[i]);
      mrc_f2_free(&gfld2);
      break;
    }
    case DIAG_TYPE_2D_Y: {
      struct mrc_f2 gfld2;
      mrc_f2_alloc_with_array(&gfld2, NULL, (int [2]) { gdims[0], gdims[2] }, 1, srv->gflds[i]);
      gfld2.domain = srv->domain;
      gfld2.name[0] = strdup(srv->fld_names[i]);
      mrc_io_write_field2d(ds->io, 1., &gfld2, DIAG_TYPE_2D_Y, srv->sheets[i]);
      mrc_f2_free(&gfld2);
      break;
    }
    case DIAG_TYPE_2D_Z: {
      struct mrc_f2 gfld2;
      mrc_f2_alloc_with_array(&gfld2, NULL, (int [2]) { gdims[0], gdims[1] }, 1, srv->gflds[i]);
      gfld2.domain = srv->domain;
      gfld2.name[0] = strdup(srv->fld_names[i]);
      mrc_io_write_field2d(ds->io, 1., &gfld2, DIAG_TYPE_2D_Z, srv->sheets[i]);
      mrc_f2_free(&gfld2);
      break;
    }
    default:
      assert(0);
    }
  }

  while (!list_empty(&srv->attrs_list)) {
    struct mrc_attrs_entry *p =
      list_entry(srv->attrs_list.next, struct mrc_attrs_entry, entry);
    mrc_dict_write(p->attrs, ds->io);
    mrc_dict_destroy(p->attrs);
    free(p->path);
    list_del(&p->entry);
    free(p);
  }

  mrc_io_close(ds->io);
}

struct diagsrv_srv_ops ds_srv_cache_ops = {
  .name        = "cache",
  .create      = ds_srv_cache_create,
  .destroy     = ds_srv_cache_destroy,
  .open        = ds_srv_cache_open,
  .get_gfld_2d = ds_srv_cache_get_gfld_2d,
  .get_gfld_3d = ds_srv_cache_get_gfld_3d,
  .put_gfld_2d = ds_srv_cache_put_gfld_2d,
  .put_gfld_3d = ds_srv_cache_put_gfld_3d,
  .write_attr  = ds_srv_cache_write_attr,
  .close       = ds_srv_cache_close,
};

// ----------------------------------------------------------------------
// diagsrv_one helpers

static void
add_to_field_2d(struct mrc_f2 *g, struct mrc_f2 *l, int ib[2])
{
  for (int iy = 0; iy < l->im[1]; iy++) {
    for (int ix = 0; ix < l->im[0]; ix++) {
      MRC_F2(g,0, ix+ib[0],iy+ib[1]) = MRC_F2(l,0, ix,iy);
    }
  }
}

static void
add_to_field_3d(struct mrc_f3 *g, struct mrc_f3 *l, int ib[2])
{
  for (int iz = 0; iz < l->im[2]; iz++) {
    for (int iy = 0; iy < l->im[1]; iy++) {
      for (int ix = 0; ix < l->im[0]; ix++) {
	MRC_F3(g,0, ix+ib[0],iy+ib[1],iz+ib[2]) = MRC_F3(l,0, ix,iy,iz);
      }
    }
  }
}

// ----------------------------------------------------------------------
// diagsrv_one

static struct diagsrv_srv_ops *ds_srvs[] = {
  &ds_srv_ops,
  &ds_srv_cache_ops,
  NULL,
};

static struct diagsrv_srv_ops *
find_ds_srv(const char *ds_srv)
{
#if 0  
  fprintf(stderr,"Available ds_srvs:\n");
  for (int i = 0; ds_srvs[i]; i++){
    fprintf(stderr,"ds_srvs[i]:%s\n",ds_srvs[i]->name);
  }
#endif

  for (int i = 0; ds_srvs[i]; i++) {
    if (strcmp(ds_srv, ds_srvs[i]->name) == 0) {
      return ds_srvs[i];
    }
  }
  fprintf(stderr, "ERROR: unknown diagsrv_srv (ds_srv) '%s'\n", ds_srv);
  abort();
}


static struct mrc_domain *
diagsrv_recv_domain_info(int nr_procs, int ldims[3])
{
  for (int d = 0; d < 3; d++) {
    ldims[d] = 0;
  }

  struct mrc_domain *domain = NULL;
  struct mrc_crds *crds = NULL;
  int iw[9], *off = iw, *_ldims = iw + 3, *gdims = iw + 6;
  for (int rank = 0; rank < nr_procs; rank++) {
    MPI_Recv(iw, 9, MPI_INT, rank, ID_DIAGS_CMD_CREATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    if (rank == 0) {
      struct mrc_domain_simple_params par = {
	.ldims    = { gdims[0], gdims[1], gdims[2] },
	.nr_procs = { 1, 1, 1 },
      };
      domain = mrc_domain_create(MPI_COMM_SELF);
      mrc_domain_set_type(domain, "simple");
      mrc_domain_simple_set_params(domain, &par);
      crds = mrc_domain_get_crds(domain);
      mrc_crds_set_type(crds, "rectilinear");
      mrc_domain_setup(domain);
    }
    for (int d = 0; d < 3; d++) {
      // find max local domain
      if (ldims[d] < _ldims[d]) {
	ldims[d] = _ldims[d];
      }
      // FIXME, this is a bit stupid, but by far the easiest way without assuming
      // internal mpi_domain knowledge -- we repeatedly receive the same pieces
      // of the global coord array, but it's only done once, anyway
      float *buf = calloc(_ldims[d], sizeof(*buf));
      MPI_Recv(buf, _ldims[d], MPI_FLOAT, rank, ID_DIAGS_CMD_CRDX + d, MPI_COMM_WORLD,
	       MPI_STATUS_IGNORE);
      for (int i = 0; i < _ldims[d]; i++) {
	MRC_CRD(crds, d, i + off[d]) = buf[i];
      }
      free(buf);
    }
  }
  return domain;
}

void
mrc_io_server(const char *ds_format, const char *ds_srv, int nr_procs)
{
  struct diagsrv_params {
    char *format;
    char *server;
  };

#define VAR(x) (void *)offsetof(struct diagsrv_params, x)
static struct param diagsrv_params_descr[] = {
  { "diagsrv_format"     , VAR(format)          , PARAM_STRING(NULL)      },
  { "diagsrv_server"     , VAR(server)          , PARAM_STRING(NULL)      },
  {},
};
#undef VAR

  struct diagsrv_params par = {
    .format = (char *) ds_format,
    .server = (char *) ds_srv,
  };
  mrc_params_parse_nodefault(&par, diagsrv_params_descr, "diagsrv",
			     MPI_COMM_SELF);
  mrc_params_print(&par, diagsrv_params_descr, "diagsrv", MPI_COMM_SELF);

  struct diagsrv_srv_ops *srv_ops = find_ds_srv(par.server);
  struct diagsrv_one ds = {
    .srv_ops    = srv_ops,
  };
  INIT_LIST_HEAD(&ds.mrc_io_list);

  int gdims[3];

  // loop waiting for data to write
  for (;;) {
    int icmd[2];
    MPI_Status status;
    MPI_Status stat;
    MPI_Recv(icmd, 2, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &stat);
    if (icmd[0] == DIAG_CMD_SHUTDOWN) {
      break;
    }

    if (icmd[0] == DIAG_CMD_CREATE) {
      char s[256] = {};
      MPI_Recv(s, 255, MPI_CHAR, 0, ID_DIAGS_CREATE_OUTDIR, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      char *outdir = strdup(s);
      MPI_Recv(s, 255, MPI_CHAR, 0, ID_DIAGS_CREATE_BASENAME, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      char *basename = strdup(s);

      assert(!find_diagsrv_io(&ds, basename));
      create_diagsrv_io(&ds, par.format, outdir, basename);
      continue;
    }

    assert(icmd[0] == DIAG_CMD_OPENFILE);
    int step = icmd[1];

    int outtag = stat.MPI_TAG;

    char s[256] = {};
    MPI_Recv(s, 255, MPI_CHAR, 0, ID_DIAGS_BASENAME, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    struct mrc_io_entry *io_entry = find_diagsrv_io(&ds, s);
    assert(io_entry);
    ds.io = io_entry->io;

    float time;
    MPI_Recv(&time, 1, MPI_FLOAT, 0, ID_DIAGS_TIME, MPI_COMM_WORLD,
	     MPI_STATUS_IGNORE);

    if (!io_entry->domain) {
      io_entry->domain = diagsrv_recv_domain_info(nr_procs, io_entry->ldims);
      srv_ops->create(&ds, io_entry->domain);
    }
    int *ldims = io_entry->ldims;

    mrc_domain_get_global_dims(io_entry->domain, gdims);
    float *w2 = malloc(ldims[0] * ldims[1] * ldims[2] * sizeof(float));

    switch (outtag) {
    case ID_DIAGS_CMD_OPEN:
      srv_ops->open(&ds, step, time);
      break;
    default:
      assert(0);
    }

    for (;;) { //waiting for field to write.
  
      char fld_name80[80];
      MPI_Recv(fld_name80, 80, MPI_CHAR, 0, MPI_ANY_TAG, MPI_COMM_WORLD,
	       &status);
      if (status.MPI_TAG == ID_DIAGS_CMD_WRITE_ATTR) {
	char *path = fld_name80;
	int type;
	MPI_Recv(&type, 1, MPI_INT, 0, ID_DIAGS_CMD_WRITE_ATTR, MPI_COMM_WORLD,
		 MPI_STATUS_IGNORE);
	char name[80];
	MPI_Recv(name, 80, MPI_CHAR, 0, ID_DIAGS_CMD_WRITE_ATTR, MPI_COMM_WORLD,
		 MPI_STATUS_IGNORE);
	union param_u val;
	switch (type) {
	case PT_INT:
	case PT_SELECT:
	  MPI_Recv(&val.u_int, 1, MPI_INT, 0, ID_DIAGS_CMD_WRITE_ATTR, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  break;
	case PT_FLOAT:
	  MPI_Recv(&val.u_float, 1, MPI_FLOAT, 0, ID_DIAGS_CMD_WRITE_ATTR, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  break;
	case PT_STRING: ;
	  char str[100];
	  MPI_Recv(str, 100, MPI_CHAR, 0, ID_DIAGS_CMD_WRITE_ATTR, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  val.u_string = strdup(str);
	  break;
	default:
	  assert(0);
	}
	srv_ops->write_attr(&ds, path, type, name, &val);
	continue;
      };
      assert(status.MPI_TAG == ID_DIAGS_FLDNAME);

      if (!fld_name80[0])
	break;

      char obj_name80[80];
      strcpy(obj_name80, fld_name80);
      
      MPI_Recv(fld_name80, 80, MPI_CHAR, 0, ID_DIAGS_FLDNAME, MPI_COMM_WORLD,
	       MPI_STATUS_IGNORE);

      int outtype;
      MPI_Recv(&outtype, 1, MPI_INT, 0, ID_DIAGS_CMD_WRITE, MPI_COMM_WORLD,
	       MPI_STATUS_IGNORE);

      if (outtype != DIAG_TYPE_3D) {
	float sheet;
	MPI_Recv(&sheet, 1, MPI_FLOAT, 0, ID_DIAGS_CMD_WRITE, MPI_COMM_WORLD,
		 MPI_STATUS_IGNORE);

	int i0 = -1, i1 = -1;
	switch (outtype) {
	case DIAG_TYPE_2D_X: i0 = 1; i1 = 2; break;
	case DIAG_TYPE_2D_Y: i0 = 0; i1 = 2; break;
	case DIAG_TYPE_2D_Z: i0 = 0; i1 = 1; break;
	default:
	  assert(0);
	}

	struct mrc_f2 gfld2, lfld2;
	srv_ops->get_gfld_2d(&ds, &gfld2, (int [2]) { gdims[i0], gdims[i1] });

	for (int k = 0; k < nr_procs; k++) { 
	  int iw[6], *off = iw, *dims = iw + 3; // off, then dims
	  MPI_Recv(iw, 6, MPI_INT, k, ID_DIAGS_SUBDOMAIN, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  
	  // receive data and add to field
	  if (iw[0] > -1) {
	    mrc_f2_alloc_with_array(&lfld2, NULL, (int [2]) { dims[i0], dims[i1] }, 1, w2);
	    MPI_Recv(lfld2.arr, lfld2.len, MPI_FLOAT, k, ID_DIAGS_2DDATA, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);
	    add_to_field_2d(&gfld2, &lfld2, (int [2]) { off[i0], off[i1] });
	    mrc_f2_free(&lfld2);
	  }
	}
	
	srv_ops->put_gfld_2d(&ds, &gfld2, fld_name80, outtype, sheet);
      } else {
	struct mrc_f3 *gfld3 = srv_ops->get_gfld_3d(&ds, gdims);

	for (int k = 0; k < nr_procs; k++) { 
	  int iw[6], *off = iw, *dims = iw + 3; // off, then dims
	  MPI_Recv(iw, 6, MPI_INT, k, ID_DIAGS_SUBDOMAIN, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  
	  // receive data and add to field
	  if (iw[0] > -1) {
	    struct mrc_f3 *lfld3 = mrc_f3_alloc(MPI_COMM_SELF, NULL, dims);
	    mrc_f3_set_array(lfld3, w2);
	    mrc_f3_setup(lfld3);
	    MPI_Recv(lfld3->arr, lfld3->len, MPI_FLOAT, k, ID_DIAGS_DATA, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);
	    add_to_field_3d(gfld3, lfld3, off);
	    mrc_f3_destroy(lfld3);
	  }
	}

	mrc_f3_set_name(gfld3, obj_name80);
	gfld3->name[0] = strdup(fld_name80);
	srv_ops->put_gfld_3d(&ds, gfld3);
      }
    }  
    srv_ops->close(&ds);
    free(w2);
    ds.io = NULL;
  }  //for (;;) //loop waiting for data to write...

  mprintf("diagsrv shutting down\n");

  while (!list_empty(&ds.mrc_io_list)) {
    struct mrc_io_entry *p = list_entry(ds.mrc_io_list.next, struct mrc_io_entry, entry);
    mrc_io_destroy(p->io);
    mrc_domain_destroy(p->domain);
    list_del(&p->entry);
    free(p);
  }

  srv_ops->destroy(&ds);
}

void
libmrc_io_register_combined()
{
  libmrc_io_register(&mrc_io_ops_combined);
}