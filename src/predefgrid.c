/*
 *  predefgrid.c
 *  This file is part of LIME, the versatile line modeling engine
 *
 *  Copyright (C) 2006-2014 Christian Brinch
 *  Copyright (C) 2015-2017 The LIME development team
 *
 */

#include "lime.h"

void
predefinedGrid(configInfo *par, struct grid *gp){
  FILE *fp;
  int i,j;
  double x,y,z,scale;
  struct cell *dc=NULL; /* Not used at present. */
  unsigned long numCells,nExtraSinks;

  gsl_rng *ran = gsl_rng_alloc(gsl_rng_ranlxs2);
#ifdef TEST
  gsl_rng_set(ran,6611304);
#else
  gsl_rng_set(ran,time(0));
#endif

  fp=fopen(par->pregrid,"r");
  par->ncell=par->pIntensity+par->sinkPoints;

  for(i=0;i<par->pIntensity;i++){
    //    fscanf(fp,"%d %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf\n", &gp[i].id, &gp[i].x[0], &gp[i].x[1], &gp[i].x[2],  &gp[i].dens[0], &gp[i].t[0], &abun, &gp[i].dopb_turb, &gp[i].vel[0], &gp[i].vel[1], &gp[i].vel[2]);
    //    fscanf(fp,"%d %lf %lf %lf %lf %lf %lf %lf\n", &gp[i].id, &gp[i].x[0], &gp[i].x[1], &gp[i].x[2],  &gp[i].dens[0], &gp[i].t[0], &abun, &gp[i].dopb_turb);
    int nRead = fscanf(fp,"%d %lf %lf %lf %lf %lf %lf %lf %lf\n", &gp[i].id, &gp[i].x[0], &gp[i].x[1], &gp[i].x[2],  &gp[i].dens[0], &gp[i].t[0], &gp[i].vel[0], &gp[i].vel[1], &gp[i].vel[2]);
    if( nRead != 9 || gp[i].id < 0 || gp[i].id > par->ncell)
      {
        if(!silent) bail_out("Reading Grid File error");
        exit(0);
      }

    gp[i].dopb_turb=200;
    gp[i].mol[0].abun=1e-9;

    gp[i].sink=0;
    gp[i].t[1]=gp[i].t[0];
    gp[i].mol[0].nmol=gp[i].mol[0].abun*gp[i].dens[0];
    gp[i].B[0]=0.0;
    gp[i].B[1]=0.0;
    gp[i].B[2]=0.0;

    /* This next step needs to be done, even though it looks stupid */
    gp[i].dir=malloc(sizeof(point)*1);
    gp[i].ds =malloc(sizeof(double)*1);
    gp[i].neigh =malloc(sizeof(struct grid *)*1);
    if(!silent) progressbar((double) i/((double)par->pIntensity-1), 4);	
  }

  checkGridDensities(par, gp);

  for(i=par->pIntensity;i<par->ncell;i++){
    x=2*gsl_rng_uniform(ran)-1.;
    y=2*gsl_rng_uniform(ran)-1.;
    z=2*gsl_rng_uniform(ran)-1.;
    if(x*x+y*y+z*z<1){
      scale=par->radius*sqrt(1/(x*x+y*y+z*z));
      gp[i].id=i;
      gp[i].x[0]=scale*x;
      gp[i].x[1]=scale*y;
      gp[i].x[2]=scale*z;
      gp[i].sink=1;
      gp[i].mol[0].abun=0.0; /* Just to give it a value. */
      gp[i].dens[0]=1e-30;
      gp[i].mol[0].nmol=0.0; /* Just to give it a value. */
      gp[i].t[0]=par->tcmb;
      gp[i].t[1]=par->tcmb;
      gp[i].B[0]=0.0;
      gp[i].B[1]=0.0;
      gp[i].B[2]=0.0;
      gp[i].dopb_turb=0.;
      for(j=0;j<DIM;j++) gp[i].vel[j]=0.;
    } else i--;
  }
  fclose(fp);

  delaunay(DIM, gp, (unsigned long)par->ncell, 0, 1, &dc, &numCells);

  /* We just asked delaunay() to flag any grid points with IDs lower than par->pIntensity (which means their distances from model centre are less than the model radius) but which are nevertheless found to be sink points by virtue of the geometry of the mesh of Delaunay cells. Now we need to reshuffle the list of grid points, then reset par->pIntensity, such that all the non-sink points still have IDs lower than par->pIntensity.
  */ 
  nExtraSinks = reorderGrid((unsigned long)par->ncell, gp);
  par->pIntensity -= nExtraSinks;
  par->sinkPoints += nExtraSinks;

  distCalc(par,gp);
  //  getArea(par,gp, ran);
  //  getMass(par,gp, ran);
  getVelocities_pregrid(par,gp);

  par->dataFlags |= (1 << DS_bit_x);
  par->dataFlags |= (1 << DS_bit_neighbours);
  par->dataFlags |= (1 << DS_bit_velocity);
  par->dataFlags |= (1 << DS_bit_density);
  par->dataFlags |= (1 << DS_bit_abundance);
  par->dataFlags |= (1 << DS_bit_turb_doppler);
  par->dataFlags |= (1 << DS_bit_temperatures);
  par->dataFlags |= (1 << DS_bit_magfield);
  par->dataFlags |= (1 << DS_bit_ACOEFF);

//**** should fill in any missing info via the appropriate function calls.

  if(par->gridfile) write_VTK_unstructured_Points(par, gp);
  gsl_rng_free(ran);
  free(dc);

  par->numDensities = 1;
}

