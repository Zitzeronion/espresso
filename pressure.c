// This file is part of the ESPResSo distribution (http://www.espresso.mpg.de).
// It is therefore subject to the ESPResSo license agreement which you accepted upon receiving the distribution
// and by which you are legally bound while utilizing this file in any form or way.
// There is NO WARRANTY, not even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// You should have received a copy of that license along with this program;
// if not, refer to http://www.espresso.mpg.de/license.html where its current version can be found, or
// write to Max-Planck-Institute for Polymer Research, Theory Group, PO Box 3148, 55021 Mainz, Germany.
// Copyright (c) 2002-2003; all rights reserved unless otherwise stated.
#include "pressure.h"
#include "parser.h"
#include "cells.h"
#include "integrate.h"
#include "domain_decomposition.h"
#include "nsquare.h"
#include "layered.h"

Observable_stat virials  = {0, {NULL,0,0}, 0,0,0,0};
Observable_stat total_pressure = {0, {NULL,0,0}, 0,0,0,0};
Observable_stat p_tensor = {0, {NULL,0,0},0,0,0,0};

nptiso_struct   nptiso   = { 0.0,0.0,0.0, 0.0,0.0,0.0,0.0,0.0 };

/************************************************************/
/* callbacks for setmd                                      */
/************************************************************/

int piston_callback(Tcl_Interp *interp, void *_data) {
  double data = *(double *)_data;
  if (data <= 0.0) { Tcl_AppendResult(interp, "the piston's mass must be positive.", (char *) NULL); return (TCL_ERROR); }
  nptiso.piston = data;
  mpi_bcast_parameter(FIELD_NPTISO_PISTON);
  return (TCL_OK);
}

int p_ext_callback(Tcl_Interp *interp, void *_data) {
  double data = *(double *)_data;
  nptiso.p_ext = data;
  mpi_bcast_parameter(FIELD_NPTISO_PEXT);
  return (TCL_OK);
}


/************************************************************/
/* local prototypes                                         */
/************************************************************/

/** Calculate long range virials (P3M, MMM2d...). */
void calc_long_range_virials();

/** Initializes a virials Observable stat. */
void init_virials(Observable_stat *stat);

/** on the master node: calc energies only if necessary */
void master_pressure_calc();

/** Does the binning for calc_p_tensor
    @param _new_bin   NOT_DOCUMENTED
    @param _elements  NOT_DOCUMENTED
    @param _volumes   NOT_DOCUMENTED
    @param r_min      minimum distance for binning
    @param r_max      maximum distance for binning
    @param r_bins     number of bins
    @param center     3 dim pointer to sphere origin 
*/
void calc_bins_sphere(int *_new_bin,int *_elements,double *_volumes,double r_min,double r_max,int r_bins, double *center);

/** Initializes extern Energy_stat \ref #p_tensor to be used by \ref calc_p_tensor. */
void init_p_tensor();

/** Calculates the pressure in the system from a virial expansion as a tensor.<BR>
    Output is stored in the \ref #p_tensor array, in which the <tt>p_tensor.sum</tt>-components contain the sum of the component-tensors
    stored in <tt>p_tensor.node</tt>. The function is executed on the master node only and uses sort of a N^2-loop to calculate the virials,
    so it is rather slow.
    @param volume the volume of the bin to be considered
    @param p_list contains the list of particles to look at
    @param flag   decides whether to derive the interactions of the particles in p_list to <i>all</i> other particles (=1) or not (=0).
*/
void calc_p_tensor(double volume, IntList *p_list, int flag);


/*******************/
/* Scalar Pressure */
/*******************/

void pressure_calc(double *result)
{
  int n;

  double volume = box_l[0]*box_l[1]*box_l[2];

  init_virials(&virials);

  if(resort_particles) {
    cells_resort_particles(CELL_GLOBAL_EXCHANGE);
    resort_particles = 0;
  }

  switch (cell_structure.type) {
  case CELL_STRUCTURE_LAYERED:
    layered_calculate_virials();
    break;
  case CELL_STRUCTURE_DOMDEC:
    if (rebuild_verletlist) build_verlet_lists();
    calculate_verlet_virials();
    break;
  case CELL_STRUCTURE_NSQUARE:
    nsq_calculate_virials();
  }
  /* rescale kinetic energy (=ideal contribution) */
#ifdef ROTATION
  virials.data.e[0] /= (6.0*volume*time_step*time_step);
#else
  virials.data.e[0] /= (3.0*volume*time_step*time_step);
#endif

  calc_long_range_virials();

  for (n = 1; n < virials.data.n; n++)
    virials.data.e[n] /= 3*volume;

  /* gather data */
  MPI_Reduce(virials.data.e, result, virials.data.n, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
}

/************************************************************/

void calc_long_range_virials()
{
#ifdef ELECTROSTATICS  
  /* calculate k-space part of electrostatic interaction. */
  switch (coulomb.method) {
  case COULOMB_P3M:
    virials.coulomb[1] = P3M_calc_kspace_forces(0,1);
    break;
  }
#endif
}

/************************************************************/

void init_virials(Observable_stat *stat)
{
  int n_pre, n_non_bonded, n_coulomb;

  n_pre        = 1;
  n_non_bonded = (n_particle_types*(n_particle_types+1))/2;

  n_coulomb    = 0;
#ifdef ELECTROSTATICS
  if(coulomb.bjerrum != 0.0) {
    n_coulomb  = 1;
    if(coulomb.method==COULOMB_P3M)
      n_coulomb += 1;
  }
#endif

  obsstat_realloc_and_clear(stat, n_pre, n_bonded_ia, n_non_bonded, n_coulomb, 1);
  stat->init_status = 0;
}

/************************************************************/

void master_pressure_calc() {
  mpi_gather_stats(2, total_pressure.data.e);

  total_pressure.init_status=1;
}


/****************************************************************************************
 *                                 parser
 ****************************************************************************************/

static void print_detailed_pressure(Tcl_Interp *interp)
{
  char buffer[TCL_DOUBLE_SPACE + TCL_INTEGER_SPACE + 2];
  double value;
  int i, j;

  value = total_pressure.data.e[0];
  for (i = 1; i < total_pressure.data.n; i++)
    value += total_pressure.data.e[i];

  Tcl_PrintDouble(interp, value, buffer);
  Tcl_AppendResult(interp, "{ pressure ", buffer, " } ", (char *)NULL);

  Tcl_PrintDouble(interp, total_pressure.data.e[0], buffer);
  Tcl_AppendResult(interp, "{ ideal ", buffer, " } ", (char *)NULL);

  for(i=0;i<n_bonded_ia;i++) {
    sprintf(buffer, "%d ", i);
    Tcl_AppendResult(interp, "{ ", buffer, (char *)NULL);
    Tcl_PrintDouble(interp, *obsstat_bonded(&total_pressure, i), buffer);
    Tcl_AppendResult(interp,
		     get_name_of_bonded_ia(bonded_ia_params[i].type),
		     " ", buffer, " } ", (char *) NULL);
  }

  for (i = 0; i < n_particle_types; i++)
    for (j = i; j < n_particle_types; j++) {
      if (checkIfParticlesInteract(i, j)) {
	sprintf(buffer, "%d ", i);
	Tcl_AppendResult(interp, "{ ", buffer, (char *)NULL);
	sprintf(buffer, "%d ", j);
	Tcl_AppendResult(interp, " ", buffer, (char *)NULL);
	Tcl_PrintDouble(interp, *obsstat_nonbonded(&total_pressure, i, j), buffer);
	Tcl_AppendResult(interp, "nonbonded ", buffer, " } ", (char *)NULL);	    
      }
    }
  
#ifdef ELECTROSTATICS
  if(coulomb.bjerrum != 0.0) {
    /* total Coulomb pressure */
    value = total_pressure.coulomb[0];
    for (i = 1; i < total_pressure.n_coulomb; i++)
      value += total_pressure.coulomb[i];
    Tcl_PrintDouble(interp, value, buffer);
    Tcl_AppendResult(interp, "{ coulomb ", buffer, (char *)NULL);

    /* if it is split up, then print the split up parts */
    if (total_pressure.n_coulomb > 1) {
      for (i = 0; i < total_pressure.n_coulomb; i++) {
	Tcl_PrintDouble(interp, total_pressure.coulomb[i], buffer);
	Tcl_AppendResult(interp, " ", buffer, (char *)NULL);
      }
    }
    Tcl_AppendResult(interp, " }", (char *)NULL);
  }
#endif
}

/************************************************************/

int parse_and_print_pressure(Tcl_Interp *interp, int argc, char **argv)
{
  /* 'analyze pressure [{ fene <type_num> | harmonic <type_num> | lj <type1> <type2> | ljcos <type1> <type2> | gb <type1> <type2> | coulomb | ideal | total }]' */
  char buffer[TCL_DOUBLE_SPACE + TCL_INTEGER_SPACE + 2];
  int i, j;

  if (n_total_particles == 0) {
    Tcl_AppendResult(interp, "(no particles)",
		     (char *)NULL);
    return (TCL_OK);
  }

  if (total_pressure.init_status == 0) {
    init_virials(&total_pressure);
    master_pressure_calc();
  }

  if (argc == 0)
    print_detailed_pressure(interp);
  else {
    double value;
    if      (ARG0_IS_S("ideal"))
      value = total_pressure.data.e[0];
    else if (ARG0_IS_S("bonded") ||
	     ARG0_IS_S("fene") ||
	     ARG0_IS_S("subt_lj_harm") ||
	     ARG0_IS_S("subt_lj_fene") ||
	     ARG0_IS_S("subt_lj") ||
	     ARG0_IS_S("harmonic")) {
      if(argc<2 || ! ARG1_IS_I(i)) {
	Tcl_ResetResult(interp);
	Tcl_AppendResult(interp, "wrong # or type of arguments for: analyze pressure bonded <type_num>",
			 (char *)NULL);
	return (TCL_ERROR);
      }
      if(i < 0 || i >= n_bonded_ia) {
	Tcl_AppendResult(interp, "bond type does not exist", (char *)NULL);
	return (TCL_ERROR);
      }
      value = *obsstat_bonded(&total_pressure, i);
    }
    else if (ARG0_IS_S("nonbonded") ||
	     ARG0_IS_S("lj") ||
	     ARG0_IS_S("lj-cos") ||
	     ARG0_IS_S("tabulated") ||
	     ARG0_IS_S("gb")) {
      if(argc<3 || ! ARG_IS_I(1, i) || ! ARG_IS_I(2, j)) {
	Tcl_ResetResult(interp);
	Tcl_AppendResult(interp, "wrong # or type of arguments for: analyze pressure nonbonded <type1> <type2>",
			 (char *)NULL);
	return (TCL_ERROR);
      }
      if(i < 0 || i >= n_particle_types || j < 0 || j >= n_particle_types) {
	Tcl_AppendResult(interp, "particle type does not exist", (char *)NULL);
	return (TCL_ERROR);
      }
      value = *obsstat_nonbonded(&total_pressure, i, j);
    }
    else if( ARG0_IS_S("coulomb")) {
#ifdef ELECTROSTATICS
      value = total_pressure.coulomb[0];
      for (i = 1; i < total_pressure.n_coulomb; i++)
	value += total_pressure.coulomb[i];
#else
      Tcl_AppendResult(interp, "ELECTROSTATICS not compiled (see config.h)\n", (char *)NULL);
#endif
    }
    else if (ARG0_IS_S("total")) {
      value = total_pressure.data.e[0];
      for (i = 1; i < total_pressure.data.n; i++)
	value += total_pressure.data.e[i];
    }
    else {
      Tcl_AppendResult(interp, "unknown feature of: analyze pressure",
		       (char *)NULL);
      return (TCL_ERROR);
    }
    Tcl_PrintDouble(interp, value, buffer);
    Tcl_AppendResult(interp, buffer, (char *)NULL);
  }

  return (TCL_OK);
}

/**********************/
/* Tensorial Pressure */
/**********************/


void calc_bins_sphere(int *new_bin,int *elements,double *volumes,double r_min,double r_max,int r_bins, double *center)
{
  int i,j,counter=0,*bins;
  double d[3],dist;
  double d_bin;
  d_bin = (r_max-r_min)/r_bins;
  bins = malloc(n_total_particles*sizeof(int));
  for(i=0; i<n_total_particles; i++) {
    get_mi_vector(d, center, partCfg[i].r.p);
    dist = sqrt(sqrlen(d))-r_min;
    bins[i] = floor(dist/d_bin);
    if(dist > r_max-r_min) bins[i] = -1;
  }
  for(i=0;i<r_bins;i++){
    new_bin[i] = 0;
    volumes[i] = 4./3.*3.1415926536*(pow(r_min+(i+1.)*d_bin,3.)-pow(r_min+i*d_bin,3.));
    for(j=0;j<n_total_particles;j++){
      if(bins[j]==i){
	elements[counter++] = partCfg[j].p.identity;
	new_bin[i] += 1;
      }
    }
    printf("vol %d %le\n",i,volumes[i]);
  }
}

/************************************************************/

int parse_bins(Tcl_Interp *interp, int argc, char **argv)
{
  /* bin particles for pressure calculation */
  /******************************************/
  /** Computes bins for pressure calculations, gives back lists
      with particles and bin volumes for each bin in spherical geometry**/
  char buffer[1000*TCL_INTEGER_SPACE];
  double r_min=0, r_max=-1.0, center[3];
  int r_bins=-1, i,j,k;
  int *new_bin;
  int *elements;
  double *volumes;

  if (ARG0_IS_S("sphere")) { 
    argc--; argv++;
    if(argc < 6) { Tcl_AppendResult(interp,"Too few arguments! Usage: 'analyze bins sphere <r_min> <r_max> <r_bins> <center1> <center2> <center3> '",(char *)NULL); return (TCL_ERROR); }
    if( argc>0 ) { if (!ARG0_IS_D(r_min)) return (TCL_ERROR); argc--; argv++; }
    if( argc>0 ) { if (!ARG0_IS_D(r_max)) return (TCL_ERROR); argc--; argv++; }
    if( argc>0 ) { if (!ARG0_IS_I(r_bins)) return (TCL_ERROR); argc--; argv++; }
    if( argc>0 ) { if (!ARG0_IS_D(center[0])) return (TCL_ERROR); argc--; argv++; }
    if( argc>0 ) { if (!ARG0_IS_D(center[1])) return (TCL_ERROR); argc--; argv++; }
    if( argc>0 ) { if (!ARG0_IS_D(center[2])) return (TCL_ERROR); argc--; argv++; }
    
    
    /* if not given use default */
    if(r_max == -1.0) r_max = min_box_l/2.0;
    if(r_bins == -1) r_bins = n_total_particles / 20;
    
    /* give back what you do */
    
    elements = malloc(n_total_particles*sizeof(int));
    new_bin = malloc(r_bins*sizeof(int));
    volumes = malloc(r_bins*sizeof(double));
    updatePartCfg(WITHOUT_BONDS);
    calc_bins_sphere(new_bin,elements,volumes, r_min, r_max, r_bins, center);
    /* append result */
    {
      Tcl_AppendResult(interp, " { ", (char *)NULL);
      sprintf(buffer,"%le",volumes[0]);
      Tcl_AppendResult(interp, buffer, (char *)NULL);
      Tcl_AppendResult(interp, " { ", (char *)NULL);
      /* i->particles, j->bin, k->particle/bin */ 
      for(i=0,j=0,k=0;i<n_total_particles;i++,k++){
	if(k==new_bin[j] || new_bin[j] == 0){
	  /* if all bins are full, rest of particles are outside r_min/r_max */
	  k=0,j++;
	  if(j==r_bins) break;
	  Tcl_AppendResult(interp, "} } { ", (char *)NULL);
	  sprintf(buffer,"%le",volumes[j]);
	  Tcl_AppendResult(interp, buffer, (char *)NULL);
	  Tcl_AppendResult(interp, " { ", (char *)NULL);
	}
	sprintf(buffer,"%d ",elements[i]);
	Tcl_AppendResult(interp, buffer, (char *)NULL);
      }
      Tcl_AppendResult(interp, "}\n", (char *)NULL);
    }
    free(new_bin);
    free(elements);
    return (TCL_OK);
  }
  else {
    sprintf(buffer, "The feature 'analyze bins %s' you requested is not yet implemented.",argv[0]);
    Tcl_AppendResult(interp,buffer,(char *)NULL);
    return (TCL_ERROR);
  }
  return (TCL_ERROR);
}

int parse_and_print_p_IK1(Tcl_Interp *interp, int argc, char **argv)
{
  /* 'analyze p_IK1 <bin_volume> { <ind_list> } <all>' */
  /*****************************************************/
  char buffer[9*TCL_DOUBLE_SPACE + 256];
  int i,j,k, flag=0;
  double volume, value;
  IntList p1;

  if (n_total_particles == 0) { Tcl_AppendResult(interp, "(no particles)",(char *)NULL); return (TCL_OK); }

  init_intlist(&p1);

  if(argc < 3) { Tcl_AppendResult(interp,"Too few arguments! Usage: 'analyze p_IK1 <bin_volume> { <ind_list> } <all>'",(char *)NULL); return (TCL_ERROR); }
  if ((!ARG0_IS_D(volume)) || (!ARG1_IS_INTLIST(p1)) || (!ARG_IS_I(2, flag))) { 
    Tcl_ResetResult(interp); Tcl_AppendResult(interp,"usage: 'analyze p_IK1 <bin_volume> { <ind_list> } <all>'",(char *)NULL); return (TCL_ERROR); 
  }

  init_p_tensor();
  calc_p_tensor(volume,&p1,flag);

  Tcl_AppendResult(interp, "{ pressure ", (char *)NULL);
  for(j=0; j<9; j++) {
    value = p_tensor.data.e[j];
    for (i = 1; i < p_tensor.data.n/9; i++) value += p_tensor.data.e[9*i + j];
    Tcl_PrintDouble(interp, value, buffer);
    Tcl_AppendResult(interp, buffer, " ", (char *)NULL);
  }
  Tcl_AppendResult(interp, "} ", (char *)NULL); 

  Tcl_AppendResult(interp, "{ ideal ", (char *)NULL);
  for(j=0; j<9; j++) {
    Tcl_PrintDouble(interp, p_tensor.data.e[j], buffer);
    Tcl_AppendResult(interp, buffer, " ", (char *)NULL);
  }
  Tcl_AppendResult(interp, "} ", (char *)NULL); 

  for(i=0;i<n_bonded_ia;i++) {
    sprintf(buffer, "%d ", i);
    Tcl_AppendResult(interp, "{ ", buffer, get_name_of_bonded_ia(bonded_ia_params[i].type)," ", (char *)NULL);
    for(j=0; j<9; j++) {
      Tcl_PrintDouble(interp, obsstat_bonded(&p_tensor, i)[j], buffer);
      Tcl_AppendResult(interp, buffer, " ", (char *)NULL);
    }
    Tcl_AppendResult(interp, "} ", (char *)NULL);
  } 

  for (i = 0; i < n_particle_types; i++)
    for (j = i; j < n_particle_types; j++) {
      if (checkIfParticlesInteract(i, j)) {
	sprintf(buffer, "%d ", i);
	Tcl_AppendResult(interp, "{ ", buffer, (char *)NULL);
	sprintf(buffer, "%d ", j);
	Tcl_AppendResult(interp, " ", buffer, "nonbonded ", (char *)NULL);
	for(k=0; k<9; k++) {
	  Tcl_PrintDouble(interp, obsstat_nonbonded(&p_tensor, i, j)[k], buffer);
	  Tcl_AppendResult(interp, buffer, " ", (char *)NULL);
	}
	Tcl_AppendResult(interp, "} ", (char *)NULL);
      }
    }

#ifdef ELECTROSTATICS
  if(coulomb.bjerrum > 0.0) {
    Tcl_AppendResult(interp, "{ coulomb ", (char *)NULL); 
    for(j=0; j<9; j++) {
      Tcl_PrintDouble(interp, p_tensor.coulomb[j], buffer);
      Tcl_AppendResult(interp, buffer, (char *)NULL);
    }
    Tcl_AppendResult(interp, "} ", (char *)NULL); 
  }
#endif

  return (TCL_OK);
}


/* Initialize the p_tensor */
/***************************/
void init_p_tensor() {
  int n_pre, n_non_bonded, n_coulomb;

  n_pre        = 1;
  n_non_bonded = (n_particle_types*(n_particle_types+1))/2;

  n_coulomb    = 0;
#ifdef ELECTROSTATICS
  if(coulomb.bjerrum != 0.0) {
    switch (coulomb.method) {
    case COULOMB_DH:   n_coulomb = 1; break;
    default:
      fprintf(stderr, "%d: init_p_tensor: cannot calculate p_tensor for coulomb method %d\n",
	      this_node, coulomb.method);
      errexit();
    }
  }
#endif

  obsstat_realloc_and_clear(&p_tensor, n_pre, n_bonded_ia, n_non_bonded, n_coulomb, 9);
  p_tensor.init_status = 0;
}

/* Derive the p_tensor */
/***********************/
void calc_p_tensor(double volume, IntList *p_list, int flag) {
  Particle p1, p2, p3;
  int *p1_list, n_p1;
  int i,j, k,l, indi,indj,startj,endj, type_num;
  double d[3],dist,dist2;
  IA_parameters *ia_params;

  p1_list = p_list->e; n_p1 = p_list->n;

  for(indi=0; indi<n_p1; indi++) {
    if (get_particle_data(p1_list[indi], &p1) != TCL_OK) { fprintf(stderr,"The particle %d you requested does not exist! ",p1_list[indi]); errexit(); }

    /* ideal gas contribution (the rescaling of the velocities by '/=time_step' each will be done later) */
    for(k=0;k<3;k++)
      for(l=0;l<3;l++)
	p_tensor.data.e[k*3 + l] += (p1.m.v[k])*(p1.m.v[l]);

    /* bonded interactions */
    i=0;
    while(i < p1.bl.n) {
      if((flag==1) || intlist_contains(p_list,p1.bl.e[i+1])) {
	get_particle_data(p1.bl.e[i+1], &p2);
	for (j = 0; j < 3; j++) {
	  p1.f.f[j] = 0;
	  p2.f.f[j] = 0;
	}

	get_mi_vector(d, p1.r.p, p2.r.p);

	type_num = p1.bl.e[i];
	switch(bonded_ia_params[type_num].type) {
	case BONDED_IA_FENE:
	  add_fene_pair_force(&p1,&p2,type_num);
	  for(k=0;k<3;k++)
	    for(l=0;l<3;l++)
	      obsstat_bonded(&p_tensor, type_num)[k*3 + l] += p1.f.f[k]*d[l];
	  i+=2; break;
	case BONDED_IA_HARMONIC:
	  add_harmonic_pair_force(&p1,&p2,type_num);
	  for(k=0;k<3;k++)
	    for(l=0;l<3;l++)
	      obsstat_bonded(&p_tensor, type_num)[k*3 + l] += p1.f.f[k]*d[l];
	  i+=2; break;
	case BONDED_IA_SUBT_LJ_HARM:
	  add_subt_lj_harm_pair_force(&p1,&p2,type_num);
	  for(k=0;k<3;k++)
	    for(l=0;l<3;l++)
	      obsstat_bonded(&p_tensor, type_num)[k*3 + l] += p1.f.f[k]*d[l];
	  i+=2; break;
	case BONDED_IA_SUBT_LJ_FENE:
	  add_subt_lj_fene_pair_force(&p1,&p2,type_num);
	  for(k=0;k<3;k++)
	    for(l=0;l<3;l++)
	      obsstat_bonded(&p_tensor, type_num)[k*3 + l] += p1.f.f[k]*d[l];
	  i+=2; break;
	case BONDED_IA_SUBT_LJ:
	  add_subt_lj_pair_force(&p1,&p2,type_num);
	  for(k=0;k<3;k++)
	    for(l=0;l<3;l++)
	      obsstat_bonded(&p_tensor, type_num)[k*3 + l] += p1.f.f[k]*d[l];
	  i+=2; break;
	case BONDED_IA_ANGLE:
	  get_particle_data(p1.bl.e[i+2], &p3);
	  for (j = 0; j < 3; j++)
	    p3.f.f[j] = 0;
	  add_angle_force(&p1,&p2,&p3,type_num);
	  for(k=0;k<3;k++)
	    for(l=0;l<3;l++)
	      obsstat_bonded(&p_tensor, type_num)[k*3 + l] += -p2.f.f[k]*d[l];
	  get_mi_vector(d, p1.r.p, p3.r.p);
	  for(k=0;k<3;k++)
	    for(l=0;l<3;l++)
	      obsstat_bonded(&p_tensor, type_num)[k*3 + l] += -p3.f.f[k]*d[l];
	  free_particle(&p3);
	  i+=3; break;
	default :
	  fprintf(stderr,"WARNING: Bond type %d of atom %d unknown\n",type_num, p1.p.identity);
	  i = p1.bl.n; break;
	}
	free_particle(&p2);
      }
    }

    /* non-bonded interactions and electrostatics */
    if(flag==1) { startj=0; endj=n_total_particles; } else { startj=indi+1; endj=n_p1; }
    for(indj=startj; indj<endj; indj++) {
      if(flag==1) {
	if((indj == p1.p.identity) || intlist_contains(p_list,indj)) continue;
	get_particle_data(indj, &p2);
      }
      else
	get_particle_data(p1_list[indj], &p2);

      for (j = 0; j < 3; j++) {
	p1.f.f[j] = 0;
	p2.f.f[j] = 0;
      }

      /* distance calculation */
      get_mi_vector(d, p1.r.p, p2.r.p);
      dist2 = SQR(d[0]) + SQR(d[1]) + SQR(d[2]);
      dist  = sqrt(dist2);

      /* non-bonded interactions */
      if (checkIfParticlesInteract(p1.p.type, p2.p.type)) { 
	ia_params = get_ia_param(p1.p.type,p2.p.type);

	/* lennnard jones */
	add_lj_pair_force(&p1,&p2,ia_params,d,dist);
	/* lennard jones cosine */
#ifdef LJCOS
	add_ljcos_pair_force(&p1,&p2,ia_params,d,dist);
#endif
	/* tabulated */
	add_tabulated_pair_force(&p1,&p2,ia_params,d,dist);

#ifdef ROTATION  
	add_gb_pair_force(&p1,&p2,ia_params,d,dist);
#endif

	for(k=0;k<3;k++)
	  for(l=0;l<3;l++)
	    obsstat_nonbonded(&p_tensor, p1.p.type, p2.p.type)[k*3 + l] += p1.f.f[k]*d[l];
      }

#ifdef ELECTROSTATICS
      if (coulomb.bjerrum != 0.0 && coulomb.method == COULOMB_DH) {
	for (j = 0; j < 3; j++) {
	  p1.f.f[j] = 0;
	  p2.f.f[j] = 0;
	}

	add_dh_coulomb_pair_force(&p1,&p2,d,dist);
	for(k=0;k<3;k++)
	  for(l=0;l<3;l++)
	    p_tensor.coulomb[k*3 + l] += p1.f.f[k]*d[l];
      }
#endif

      free_particle(&p2);
    }

    free_particle(&p1);
  }

  /* Rescale entries, kinetic (ideal) contribution first */
  for(i=0; i<9; i++) {
#ifdef ROTATION
    p_tensor.data.e[i] /= (6.0*volume*time_step*time_step);
#else
    p_tensor.data.e[i] /= (3.0*volume*time_step*time_step);
#endif
  }

  for(i=9; i<p_tensor.data.n; i++)
    p_tensor.data.e[i]  /= 3.0*volume;

  p_tensor.init_status=1;
}

