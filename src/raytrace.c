/*
 *  raytrace.c
 *  This file is part of LIME, the versatile line modeling engine
 *
 *  Copyright (C) 2006-2014 Christian Brinch
 *  Copyright (C) 2015 The LIME development team
 *
 */

#include "lime.h"


/*....................................................................*/
void calcLineAmpSample(const double x[3], const double dx[3], const double ds\
  , const double binv, double *projVels, const int nSteps\
  , const double oneOnNSteps, const double deltav, double *vfac){
  /*
The bulk velocity of the model material can vary significantly with position, thus so can the value of the line-shape function at a given frequency and direction. The present function calculates 'vfac', an approximate average of the line-shape function along a path of length ds in the direction of the line of sight.
  */
  int i;
  double v,d,val,vel[3];

  *vfac=0.;
  for(i=0;i<nSteps;i++){
    d=i*ds*oneOnNSteps;
    v = deltav - projVels[i]; /* projVels contains the component of the local bulk velocity in the direction of dx, whereas deltav is the recession velocity of the channel we are interested in (corrected for bulk source velocity and line displacement from the nominal frequency). Remember also that, since dx points away from the observer, positive values of the projected velocity also represent recessions. Line centre occurs when v==0, i.e. when deltav==veloproject(dx,vel). That is the reason for the subtraction here. */
    val=fabs(v)*binv;
    if(val <=  2500.){
#ifdef FASTEXP
      *vfac+= FastExp(val*val);
#else
      *vfac+=   exp(-(val*val));
#endif
    }
  }
  *vfac *= oneOnNSteps;
  return;
}

/*....................................................................*/
void calcLineAmpInterp(const double projVelRay, const double binv\
  , const double deltav, double *vfac){
  /*
The bulk velocity of the model material can vary significantly with position, thus so can the value of the line-shape function at a given frequency and direction. The present function calculates 'vfac', an approximate average of the line-shape function along a path of length ds in the direction of the line of sight.
  */
  double v,val;

  v = deltav - projVelRay; /* projVelRay is the component of the local bulk velocity in the direction of the ray, whereas deltav is the recession velocity of the channel we are interested in (corrected for bulk source velocity and line displacement from the nominal frequency). Remember also that, since the ray points away from the observer, positive values of the projected velocity also represent recessions. Line centre occurs when v==0, i.e. when deltav==projVelRay. That is the reason for the subtraction here. */
  val = fabs(v)*binv;
  if(val <=  2500.){
#ifdef FASTEXP
    *vfac = FastExp(val*val);
#else
    *vfac =   exp(-(val*val));
#endif
  }else
    *vfac = 0.0;
}

/*....................................................................*/
void
line_plane_intersect(struct grid *g, double *ds, int posn, int *nposn, double *dx, double *x, double cutoff){
  /*
This function returns ds as the (always positive-valued) distance between the present value of x and the next Voronoi face in the direction of vector dx, and nposn as the id of the grid cell that abuts that face. 
  */
  double newdist, numerator, denominator ;
  int i;

  for(i=0;i<g[posn].numNeigh;i++) {
    /* Find the shortest distance between (x,y,z) and any of the posn Voronoi faces */
    /* ds=(p0-l0) dot n / l dot n */

    numerator=((g[posn].x[0]+g[posn].dir[i].x[0]/2. - x[0]) * g[posn].dir[i].x[0]+
               (g[posn].x[1]+g[posn].dir[i].x[1]/2. - x[1]) * g[posn].dir[i].x[1]+
               (g[posn].x[2]+g[posn].dir[i].x[2]/2. - x[2]) * g[posn].dir[i].x[2]);

    denominator=(dx[0]*g[posn].dir[i].x[0]+dx[1]*g[posn].dir[i].x[1]+dx[2]*g[posn].dir[i].x[2]);
    
    if(fabs(denominator) > 0){
      newdist=numerator/denominator;
      if(newdist<*ds && newdist > cutoff){
        *ds=newdist;
        *nposn=g[posn].neigh[i]->id;
      }
    }
  }
  if(*nposn==-1) *nposn=posn;
}

/*....................................................................*/
void
traceray(rayData ray, inputPars *par, const int tmptrans, image *img\
  , const int im, struct grid *gp, struct gAuxType *gAux, molData *md\
  , const int nlinetot, int *allLineMolIs, int *allLineLineIs, const double cutoff\
  , const int nSteps, const double oneOnNSteps){
  /*
For a given image pixel position, this function evaluates the intensity of the total light emitted/absorbed along that line of sight through the (possibly rotated) model. The calculation is performed for several frequencies, one per channel of the output image.

Note that the algorithm employed here is similar to that employed in the function photon() which calculates the average radiant flux impinging on a grid cell: namely the notional photon is started at the side of the model near the observer and 'propagated' in the receding direction until it 'reaches' the far side. This is rather non-physical in conception but it makes the calculation easier.
  */

  int ichan,stokesId,di,i,posn,nposn,polMolI,polLineI,contMolI,contLineI,iline,molI,lineI;
  double xp,yp,zp,x[DIM],dx[DIM],dist2,ndist2,col,ds,snu_pol[3],dtau;
  double contJnu,contAlpha,jnu,alpha,lineRedShift,vThisChan,deltav,vfac=0.;
  double remnantSnu,expDTau,brightnessIncrement;
  double projVels[nSteps],d,vel[DIM];

  for(ichan=0;ichan<img[im].nchan;ichan++){
    ray.tau[ichan]=0.0;
    ray.intensity[ichan]=0.0;
  }

  xp=ray.x;
  yp=ray.y;

  /* The model is circular in projection. We only follow the ray if it will intersect the model.
  */
  if((xp*xp+yp*yp)>par->radiusSqu)
    return;

  zp=-sqrt(par->radiusSqu-(xp*xp+yp*yp)); /* There are two points of intersection between the line of sight and the spherical model surface; this is the Z coordinate (in the unrotated frame) of the one nearer to the observer. */

  /* Rotate the line of sight as desired. */
  for(di=0;di<DIM;di++){
    x[di]=xp*img[im].rotMat[di][0] + yp*img[im].rotMat[di][1] + zp*img[im].rotMat[di][2];
    dx[di]= img[im].rotMat[di][2]; /* This points away from the observer. */
  }

  contMolI = 0; /****** Always?? */

  if(img[im].doline && img[im].trans > -1)
    contLineI = img[im].trans;
  else if(img[im].doline && img[im].trans == -1)
    contLineI = tmptrans;
  else
    contLineI = 0;

  /* Find the grid point nearest to the starting x. */
  i=0;
  dist2=(x[0]-gp[i].x[0])*(x[0]-gp[i].x[0]) + (x[1]-gp[i].x[1])*(x[1]-gp[i].x[1]) + (x[2]-gp[i].x[2])*(x[2]-gp[i].x[2]);
  posn=i;
  for(i=1;i<par->ncell;i++){
    ndist2=(x[0]-gp[i].x[0])*(x[0]-gp[i].x[0]) + (x[1]-gp[i].x[1])*(x[1]-gp[i].x[1]) + (x[2]-gp[i].x[2])*(x[2]-gp[i].x[2]);
    if(ndist2<dist2){
      posn=i;
      dist2=ndist2;
    }
  }

  col=0;
  do{
    ds=-2.*zp-col; /* This default value is chosen to be as large as possible given the spherical model boundary. */
    nposn=-1;
    line_plane_intersect(gp,&ds,posn,&nposn,dx,x,cutoff); /* Returns a new ds equal to the distance to the next Voronoi face, and nposn, the ID of the grid cell that abuts that face. */

    if(par->polarization){
      polMolI = 0; /****** Always?? */
      polLineI = 0; /****** Always?? */
      for(stokesId=0;stokesId<img[im].nchan;stokesId++){
        sourceFunc_pol(ds, gp[posn].B, md[polMolI], gAux[posn].mol[polMolI], polLineI, img[im].theta, snu_pol, &dtau);
#ifdef FASTEXP
        brightnessIncrement = FastExp(ray.tau[stokesId])*(1.-FastExp(dtau))*snu_pol[stokesId];
#else
        brightnessIncrement =    exp(-ray.tau[stokesId])*(1.-   exp(-dtau))*snu_pol[stokesId];
#endif
        ray.intensity[stokesId] += brightnessIncrement;
        ray.tau[stokesId]+=dtau;
      }
    } else {
      if(!par->pregrid){
        for(i=0;i<nSteps;i++){
          d = i*ds*oneOnNSteps;
          velocity(x[0]+(dx[0]*d),x[1]+(dx[1]*d),x[2]+(dx[2]*d),vel);
          projVels[i] = veloproject(dx,vel);
        }
      }

      /* Calculate first the continuum stuff because it is the same for all channels:
      */
      contJnu = 0.0;
      contAlpha = 0.0;
      sourceFunc_cont_raytrace(gAux[posn].mol[contMolI], contLineI, &contJnu, &contAlpha);

      for(ichan=0;ichan<img[im].nchan;ichan++){
        jnu = contJnu;
        alpha = contAlpha;
        vThisChan = (ichan-(img[im].nchan-1)*0.5)*img[im].velres; /* Consistent with the WCS definition in writefits(). */

        for(iline=0;iline<nlinetot;iline++){
          molI = allLineMolIs[iline];
          lineI = allLineLineIs[iline];
          if(img[im].doline\
          && md[molI].freq[lineI] > img[im].freq-img[im].bandwidth*0.5\
          && md[molI].freq[lineI] < img[im].freq+img[im].bandwidth*0.5){
            /* Calculate the red shift of the transition wrt to the frequency specified for the image.
            */
            if(img[im].trans > -1){
              lineRedShift=(md[molI].freq[img[im].trans]-md[molI].freq[lineI])/md[molI].freq[img[im].trans]*CLIGHT;
            } else {
              lineRedShift=(img[im].freq-md[molI].freq[lineI])/img[im].freq*CLIGHT;
            }

            deltav = vThisChan - img[im].source_vel - lineRedShift;
            /* Line centre occurs when deltav = the recession velocity of the radiating material. Explanation of the signs of the 2nd and 3rd terms on the RHS: (i) A bulk source velocity (which is defined as >0 for the receding direction) should be added to the material velocity field; this is equivalent to subtracting it from deltav, as here. (ii) A positive value of lineRedShift means the line is red-shifted wrt to the frequency specified for the image. The effect is the same as if the line and image frequencies were the same, but the bulk recession velocity were higher. lineRedShift should thus be added to the recession velocity, which is equivalent to subtracting it from deltav, as here. */

            /* Calculate an approximate average line-shape function at deltav within the Voronoi cell. */
            if(!par->pregrid)
              calcLineAmpSample(x,dx,ds,gp[posn].mol[molI].binv,projVels,nSteps,oneOnNSteps,deltav,&vfac);
            else
              vfac = gaussline(deltav-veloproject(dx,gp[posn].vel),gp[posn].mol[molI].binv);

            /* Increment jnu and alpha for this Voronoi cell by the amounts appropriate to the spectral line. */
            sourceFunc_line_raytrace(md[molI],vfac,gAux[posn].mol[molI],lineI,&jnu,&alpha);
          }
        }

        dtau=alpha*ds;
//???          if(dtau < -30) dtau = -30; // as in photon()?
        calcSourceFn(dtau, par, &remnantSnu, &expDTau);
        remnantSnu *= jnu*md[0].norminv*ds;
#ifdef FASTEXP
        brightnessIncrement = FastExp(ray.tau[ichan])*remnantSnu;
#else
        brightnessIncrement =    exp(-ray.tau[ichan])*remnantSnu;
#endif
        ray.intensity[ichan] += brightnessIncrement;
        ray.tau[ichan]+=dtau;
      }
    }

    /* Move the working point to the edge of the next Voronoi cell. */
    for(di=0;di<DIM;di++) x[di]+=ds*dx[di];
    col+=ds;
    posn=nposn;
  } while(col < 2.0*fabs(zp));

  /* Add or subtract cmb. */
#ifdef FASTEXP
  for(ichan=0;ichan<img[im].nchan;ichan++){
    ray.intensity[ichan]+=FastExp(ray.tau[ichan])*md[0].local_cmb[tmptrans];
  }
#else
  for(ichan=0;ichan<img[im].nchan;ichan++){
    ray.intensity[ichan]+=exp(-ray.tau[ichan])*md[0].local_cmb[tmptrans];
  }
#endif
}

/*....................................................................*/
void traceray_smooth(rayData ray, inputPars *par, const int tmptrans, image *img\
  , const int im, struct grid *gp, struct gAuxType *gAux, molData *md, const int nlinetot\
  , int *allLineMolIs, int *allLineLineIs, struct cell *dc\
  , const unsigned long numCells, const double epsilon, gridInterp gips[3]\
  , const int numSegments, const double oneOnNumSegments, const int nSteps, const double oneOnNSteps){
  /*
For a given image pixel position, this function evaluates the intensity of the total light emitted/absorbed along that line of sight through the (possibly rotated) model. The calculation is performed for several frequencies, one per channel of the output image.

Note that the algorithm employed here to solve the RTE is similar to that employed in the function photon() which calculates the average radiant flux impinging on a grid cell: namely the notional photon is started at the side of the model near the observer and 'propagated' in the receding direction until it 'reaches' the far side. This is rather non-physical in conception but it makes the calculation easier.

This version of traceray implements a new algorithm in which the population values are interpolated linearly from those at the vertices of the Delaunay cell which the working point falls within.
  */

  const int numFaces = DIM+1, nVertPerFace=3;
  int ichan,stokesId,di,status,lenChainPtrs,entryI,exitI,vi,vvi,ci;
  int si, contMolI, contLineI, polMolI, polLineI, iline, molI, lineI;
  double xp,yp,zp,x[DIM],dir[DIM],projVelRay,vel[DIM];
  double xCmpntsRay[nVertPerFace], ds, snu_pol[3], dtau, contJnu, contAlpha;
  double jnu, alpha, lineRedShift, vThisChan, deltav, vfac, remnantSnu, expDTau;
  double brightnessIncrement;
  intersectType entryIntcptFirstCell, *cellExitIntcpts=NULL;
  unsigned long *chainOfCellIds=NULL, dci;
  unsigned long gis[2][nVertPerFace];

  for(ichan=0;ichan<img[im].nchan;ichan++){
    ray.tau[ichan]=0.0;
    ray.intensity[ichan]=0.0;
  }

  xp=ray.x;
  yp=ray.y;

  /* The model is circular in projection. We only follow the ray if it will intersect the model.
  */
  if((xp*xp+yp*yp)>par->radiusSqu)
    return;

  zp=-sqrt(par->radiusSqu-(xp*xp+yp*yp)); /* There are two points of intersection between the line of sight and the spherical model surface; this is the Z coordinate (in the unrotated frame) of the one nearer to the observer. */

  /* Rotate the line of sight as desired. */
  for(di=0;di<DIM;di++){
    x[di]=xp*img[im].rotMat[di][0] + yp*img[im].rotMat[di][1] + zp*img[im].rotMat[di][2];
    dir[di]= img[im].rotMat[di][2]; /* This points away from the observer. */
  }

  contMolI = 0; /****** Always?? */

  if(img[im].doline && img[im].trans > -1)
    contLineI = img[im].trans;
  else if(img[im].doline && img[im].trans == -1)
    contLineI = tmptrans;
  else
    contLineI = 0;

  /* Find the chain of cells the ray passes through.
  */
  status = followRayThroughDelCells(x, dir, gp, dc, numCells, epsilon\
    , &entryIntcptFirstCell, &chainOfCellIds, &cellExitIntcpts, &lenChainPtrs);//, 0);

  if(status!=0){
    free(chainOfCellIds);
    free(cellExitIntcpts);
    return;
  }

  entryI = 0;
  exitI  = 1;
  dci = chainOfCellIds[0];

  /* Obtain the indices of the grid points on the vertices of the entry face.
  */
  vvi = 0;
  for(vi=0;vi<numFaces;vi++){
    if(vi!=entryIntcptFirstCell.fi){
      gis[entryI][vvi++] = dc[dci].vertx[vi]->id;
    }
  }

  /* Calculate, for each of the 3 vertices of the entry face, the displacement components in the direction of 'dir'. *** NOTE *** that if all the rays are parallel, we could precalculate these for all the vertices.
  */
  for(vi=0;vi<nVertPerFace;vi++)
    xCmpntsRay[vi] = veloproject(dir, gp[gis[entryI][vi]].x);

  doBaryInterp(entryIntcptFirstCell, gp, gAux, xCmpntsRay, gis[entryI]\
    , md, par->nSpecies, &gips[entryI]);

  for(ci=0;ci<lenChainPtrs;ci++){
    /* For each cell we have 2 data structures which give information about respectively the entry and exit points of the ray, including the barycentric coordinates of the intersection point between the ray and the appropriate face of the cell. (If we follow rays in 3D space then the cells will be tetrahedra and the faces triangles.) If we know the value of a quantity Q for each of the vertices, then the linear interpolation of the Q values for any face is (for a 3D space) bary[0]*Q[0] + bary[1]*Q[1] + bary[2]*Q[2], where the indices are taken to run over the vertices of that face. Thus we can calculate the interpolated values Q_entry and Q_exit. Further linear interpolation along the path between entry and exit is straightforward.
    */

    dci = chainOfCellIds[ci];

    /* Obtain the indices of the grid points on the vertices of the exit face. */
    vvi = 0;
    for(vi=0;vi<numFaces;vi++){
      if(vi!=cellExitIntcpts[ci].fi){
        gis[exitI][vvi++] = dc[dci].vertx[vi]->id;
      }
    }

    /* Calculate, for each of the 3 vertices of the exit face, the displacement components in the direction of 'dir'. *** NOTE *** that if all the rays are parallel, we could precalculate these for all the vertices.
    */
    for(vi=0;vi<nVertPerFace;vi++)
      xCmpntsRay[vi] = veloproject(dir, gp[gis[exitI][vi]].x);

    doBaryInterp(cellExitIntcpts[ci], gp, gAux, xCmpntsRay, gis[exitI]\
      , md, par->nSpecies, &gips[exitI]);

    /* At this point we have interpolated all the values of interest to both the entry and exit points of the cell. Now we break the path between entry and exit into several segments and calculate all these values at the midpoint of each segment.

At the moment I will fix the number of segments, but it might possibly be faster to rather have a fixed segment length (in barycentric coordinates) and vary the number depending on how many of these lengths fit in the path between entry and exit.
    */
    ds = (gips[exitI].xCmpntRay - gips[entryI].xCmpntRay)*oneOnNumSegments;

    for(si=0;si<numSegments;si++){
      doSegmentInterp(gips, entryI, md, par->nSpecies, oneOnNumSegments, si);

      if(par->polarization){
        polMolI = 0; /****** Always?? */
        polLineI = 0; /****** Always?? */
        for(stokesId=0;stokesId<img[im].nchan;stokesId++){ /**** could also precalc continuum part here? */
          sourceFunc_pol(ds, gips[2].B, md[polMolI], gips[2].mol[polMolI], polLineI, img[im].theta, snu_pol, &dtau);
#ifdef FASTEXP
          brightnessIncrement = FastExp(ray.tau[stokesId])*(1.-FastExp(dtau))*snu_pol[stokesId];
#else
          brightnessIncrement =    exp(-ray.tau[stokesId])*(1.-   exp(-dtau))*snu_pol[stokesId];
#endif
          ray.intensity[stokesId] += brightnessIncrement;
          ray.tau[stokesId]+=dtau;
        }
      } else {
        /* It appears to be necessary to sample the velocity function in the following way rather than interpolating it from the vertices of the Delaunay cell in the same way as with all the other quantities of interest. Velocity varies too much across the cells, and in a nonlinear way, for linear interpolation to yield a totally satisfactory result.
        */
        velocity(gips[2].x[0], gips[2].x[1], gips[2].x[2], vel);
        projVelRay = veloproject(dir, vel);

        /* Calculate first the continuum stuff because it is the same for all channels:
        */
        contJnu = 0.0;
        contAlpha = 0.0;
        sourceFunc_cont_raytrace(gips[2].mol[contMolI], contLineI, &contJnu, &contAlpha);

        for(ichan=0;ichan<img[im].nchan;ichan++){
          jnu = contJnu;
          alpha = contAlpha;
          vThisChan=(ichan-(img[im].nchan-1)*0.5)*img[im].velres; /* Consistent with the WCS definition in writefits(). */

          for(iline=0;iline<nlinetot;iline++){
            molI = allLineMolIs[iline];
            lineI = allLineLineIs[iline];
            if(img[im].doline\
            && md[molI].freq[lineI] > img[im].freq-img[im].bandwidth*0.5\
            && md[molI].freq[lineI] < img[im].freq+img[im].bandwidth*0.5){
              /* Calculate the red shift of the transition wrt to the frequency specified for the image.
              */
              if(img[im].trans > -1){
                lineRedShift=(md[molI].freq[img[im].trans]-md[molI].freq[lineI])/md[molI].freq[img[im].trans]*CLIGHT;
              } else {
                lineRedShift=(img[im].freq-md[molI].freq[lineI])/img[im].freq*CLIGHT;
              }

              deltav = vThisChan - img[im].source_vel - lineRedShift;
              /* Line centre occurs when deltav = the recession velocity of the radiating material. Explanation of the signs of the 2nd and 3rd terms on the RHS: (i) A bulk source velocity (which is defined as >0 for the receding direction) should be added to the material velocity field; this is equivalent to subtracting it from deltav, as here. (ii) A positive value of lineRedShift means the line is red-shifted wrt to the frequency specified for the image. The effect is the same as if the line and image frequencies were the same, but the bulk recession velocity were higher. lineRedShift should thus be added to the recession velocity, which is equivalent to subtracting it from deltav, as here. */

              calcLineAmpInterp(projVelRay, gips[2].mol[molI].binv, deltav, &vfac);

              /* Increment jnu and alpha for this Voronoi cell by the amounts appropriate to the spectral line.
              */
              sourceFunc_line_raytrace(md[molI], vfac, gips[2].mol[molI], lineI, &jnu, &alpha);
            }
          } /* end loop over all lines. */

          dtau = alpha*ds;
//???          if(dtau < -30) dtau = -30; // as in photon()?
          calcSourceFn(dtau, par, &remnantSnu, &expDTau);
          remnantSnu *= jnu*md[0].norminv*ds;
#ifdef FASTEXP
          brightnessIncrement = FastExp(ray.tau[ichan])*remnantSnu;
#else
          brightnessIncrement =    exp(-ray.tau[ichan])*remnantSnu;
#endif
          ray.intensity[ichan] += brightnessIncrement;
          ray.tau[ichan] += dtau;
        } /* End loop over channels. */
      } /* End if(par->polarization). */
    } /* End loop over segments within cell. */

    entryI = exitI;
    exitI = 1 - exitI;
  } /* End loop over cells in the chain traversed by the ray. */

  /* Add or subtract cmb. */
#ifdef FASTEXP
  for(ichan=0;ichan<img[im].nchan;ichan++){
    ray.intensity[ichan]+=FastExp(ray.tau[ichan])*md[0].local_cmb[tmptrans];
  }
#else
  for(ichan=0;ichan<img[im].nchan;ichan++){
    ray.intensity[ichan]+=exp(-ray.tau[ichan])*md[0].local_cmb[tmptrans];
  }
#endif

  free(chainOfCellIds);
  free(cellExitIntcpts);
}

/*....................................................................*/
void
raytrace(int im, inputPars *par, struct grid *gp, molData *md, image *img){
  /*
This function constructs an image cube by following sets of rays (at least 1 per image pixel) through the model, solving the radiative transfer equations as appropriate for each ray. The ray locations within each pixel are chosen randomly within the pixel, but the number of rays per pixel is set equal to the number of projected model grid points falling within that pixel, down to a minimum equal to par->alias.
  */

  const gsl_rng_type *ranNumGenType=gsl_rng_ranlxs2;
  const double epsilon = 1.0e-6; /* Needs thinking about. Double precision is much smaller than this. */
  const int numFaces=1+DIM, numInterpPoints=3, numSegments=5;
  const double oneOnNFaces=1.0/(double)numFaces, oneOnNumSegments=1.0/(double)numSegments;
  const int nStepsThruCell=10;
  const double oneOnNSteps=1.0/(double)nStepsThruCell;

  int i,di,vi,gi,molI,ei,li,nlinetot,iline,tmptrans,ichan,xi,yi;
  double size,imgCentreXPixels,imgCentreYPixels,sum,minfreq,absDeltaFreq,oneOnNumActiveRaysMinus1,cutoff,progress,x[2];
  unsigned int totalNumImagePixels,ppi,numActiveRays,nRaysDone;
  struct cell *dc=NULL;
  unsigned long numCells, dci;
  struct gAuxType *gAux=NULL; /* This will hold some precalculated values for the grid points. */
  int *allLineMolIs,*allLineLineIs;
  static double lastProgress = 0.0;

  gsl_rng *randGen = gsl_rng_alloc(ranNumGenType);	/* Random number generator */
#ifdef TEST
  gsl_rng_set(randGen,178490);
#else
  gsl_rng_set(randGen,time(0));
#endif

  gsl_rng **threadRans;
  threadRans = malloc(sizeof(gsl_rng *)*par->nThreads);

  for (i=0;i<par->nThreads;i++){
    threadRans[i] = gsl_rng_alloc(ranNumGenType);
    gsl_rng_set(threadRans[i],(int)(gsl_rng_uniform(randGen)*1e6));
  }

  size = img[im].distance*img[im].imgres;
  totalNumImagePixels = img[im].pxls*img[im].pxls;
  imgCentreXPixels = img[im].pxls/2.0;
  imgCentreYPixels = img[im].pxls/2.0;

  if(par->traceRayAlgorithm==1){
    delaunay(DIM, gp, (unsigned long)par->ncell, 1, &dc, &numCells);

    /* We need to process the list of cells a bit further - calculate their centres, and reset the id values to be the same as the index of the cell in the list. (This last because we are going to construct other lists to indicate which cells have been visited etc.)
    */
    for(dci=0;dci<numCells;dci++){
      for(di=0;di<DIM;di++){
        sum = 0.0;
        for(vi=0;vi<numFaces;vi++){
          sum += dc[dci].vertx[vi]->x[di];
        }
        dc[dci].centre[di] = sum*oneOnNFaces;
      }

      dc[dci].id = dci;
    }

  }else if(par->traceRayAlgorithm!=0){
    if(!silent) bail_out("Unrecognized value of par.traceRayAlgorithm");
    exit(1);
  }

  /* Precalculate binv*nmol*pops for all grid points.
  */
  gAux = malloc(sizeof(*gAux)*par->ncell);
  for(gi=0;gi<par->ncell;gi++){
    gAux[gi].mol = malloc(sizeof(*(gAux[gi].mol))*par->nSpecies);
    for(molI=0;molI<par->nSpecies;molI++){
      gAux[gi].mol[molI].specNumDens = malloc(sizeof(*(gAux[gi].mol[molI].specNumDens))*md[molI].nlev);
      gAux[gi].mol[molI].dust        = malloc(sizeof(*(gAux[gi].mol[molI].dust))       *md[molI].nline);
      gAux[gi].mol[molI].knu         = malloc(sizeof(*(gAux[gi].mol[molI].knu))        *md[molI].nline);

      for(ei=0;ei<md[molI].nlev;ei++)
        gAux[gi].mol[molI].specNumDens[ei] = gp[gi].mol[molI].binv\
          *gp[gi].mol[molI].nmol*gp[gi].mol[molI].pops[ei];

      /* This next is repetition. I do it in order to be able to use the same sourcefunc.c functions for the interpolated grid values as for the 'standard' algorithm. With a sensible arrangement of memory for the grid values, this would be unnecessary.
      */
      for(li=0;li<md[molI].nline;li++){
        gAux[gi].mol[molI].dust[li] = gp[gi].mol[molI].dust[li];
        gAux[gi].mol[molI].knu[li]  = gp[gi].mol[molI].knu[li];
      }
    }
  }

  /* Determine whether there are blended lines or not. */
  lineCount(par->nSpecies, md, &allLineMolIs, &allLineLineIs, &nlinetot);
  if(img[im].doline==0) nlinetot=1;

  /* Fix the image parameters. */
  if(img[im].freq < 0) img[im].freq = md[0].freq[img[im].trans];
  if(img[im].nchan == 0 && img[im].bandwidth>0){
    img[im].nchan=(int) (img[im].bandwidth/(img[im].velres/CLIGHT*img[im].freq));
  } else if (img[im].velres<0 && img[im].bandwidth>0){
    img[im].velres = img[im].bandwidth*CLIGHT/img[im].freq/img[im].nchan;
  } else img[im].bandwidth = img[im].nchan*img[im].velres/CLIGHT * img[im].freq;

  if(img[im].trans<0){
    iline=0;
    minfreq=fabs(img[im].freq-md[0].freq[iline]);
    tmptrans=iline;
    for(iline=1;iline<md[0].nline;iline++){
      absDeltaFreq=fabs(img[im].freq-md[0].freq[iline]);
      if(absDeltaFreq<minfreq){
        minfreq=absDeltaFreq;
        tmptrans=iline;
      }
    }
  } else tmptrans=img[im].trans;

  for(ppi=0;ppi<totalNumImagePixels;ppi++){
    for(ichan=0;ichan<img[im].nchan;ichan++){
      img[im].pixel[ppi].intense[ichan]=0.0;
      img[im].pixel[ppi].tau[ichan]=0.0;
    }
  }

  for(ppi=0;ppi<totalNumImagePixels;ppi++){
    img[im].pixel[ppi].numRays=0;
  }

  /* Calculate the number of rays wanted per image pixel from the density of the projected model grid points.
  */
  for(gi=0;gi<par->pIntensity;gi++){
    /* Apply the inverse (i.e. transpose) rotation matrix. (We use the inverse matrix here because here we want to rotate grid coordinates to the observer frame, whereas inside traceray() we rotate observer coordinates to the grid frame.)
    */
    for(i=0;i<2;i++){
      x[i]=0.0;
      for(di=0;di<DIM;di++)
        x[i] += gp[gi].x[di]*img[im].rotMat[di][i];
    }

    /* Calculate which pixel the projected position (x[0],x[1]) falls within. */
    xi = floor(x[0]/size + imgCentreXPixels);
    yi = floor(x[1]/size + imgCentreYPixels);
    ppi = (unsigned int)yi*img[im].pxls + (unsigned int)xi;
    if(ppi>=0 && ppi<totalNumImagePixels)
      img[im].pixel[ppi].numRays++;
  }

  /* Set a minimum number of rays per image pixel, and count the total number of rays.
  */
  numActiveRays = 0;
  for(ppi=0;ppi<totalNumImagePixels;ppi++)
    if(img[im].pixel[ppi].numRays < par->antialias)
      img[im].pixel[ppi].numRays = par->antialias;

    numActiveRays += img[im].pixel[ppi].numRays;
  oneOnNumActiveRaysMinus1 = 1.0/(double)(numActiveRays - 1);

  cutoff = par->minScale*1.0e-7;

  nRaysDone=0;
  omp_set_dynamic(0);
  #pragma omp parallel private(ppi,molI,xi,yi) num_threads(par->nThreads)
  {
    /* Declaration of thread-private pointers. */
    int ai,ii,threadI = omp_get_thread_num();
    rayData ray;
    gridInterp gips[numInterpPoints];
    double oneOnNRaysThisPixel;

    ray.intensity = malloc(sizeof(double)*img[im].nchan);
    ray.tau       = malloc(sizeof(double)*img[im].nchan);

    if(par->traceRayAlgorithm==1){
      /* Allocate memory for the interpolation points:
      */
      for(ii=0;ii<numInterpPoints;ii++){
        gips[ii].mol = malloc(sizeof(*(gips[ii].mol))*par->nSpecies);
        for(molI=0;molI<par->nSpecies;molI++){
          gips[ii].mol[molI].specNumDens = malloc(sizeof(*(gips[ii].mol[molI].specNumDens))*md[molI].nlev);
          gips[ii].mol[molI].dust        = malloc(sizeof(*(gips[ii].mol[molI].dust))       *md[molI].nline);
          gips[ii].mol[molI].knu         = malloc(sizeof(*(gips[ii].mol[molI].knu))        *md[molI].nline);
        }
      }
    }

    #pragma omp for schedule(dynamic)
    /* Main loop through pixel grid. */
    for(ppi=0;ppi<totalNumImagePixels;ppi++){
      xi = (int)(ppi%(unsigned int)img[im].pxls);
      yi = floor(ppi/(double)img[im].pxls);

      oneOnNRaysThisPixel = 1.0/(double)img[im].pixel[ppi].numRays;

      #pragma omp atomic
      ++nRaysDone;

      for(ai=0;ai<img[im].pixel[ppi].numRays;ai++){
        ray.x = -size*(gsl_rng_uniform(threadRans[threadI]) + xi - imgCentreXPixels);
        ray.y =  size*(gsl_rng_uniform(threadRans[threadI]) + yi - imgCentreYPixels);

        if(par->traceRayAlgorithm==0){
          traceray(ray, par, tmptrans, img, im, gp, gAux, md, nlinetot\
            , allLineMolIs, allLineLineIs, cutoff, nStepsThruCell, oneOnNSteps);
        }else if(par->traceRayAlgorithm==1)
          traceray_smooth(ray, par, tmptrans, img, im, gp, gAux, md, nlinetot\
            , allLineMolIs, allLineLineIs, dc, numCells, epsilon, gips, numSegments\
            , oneOnNumSegments, nStepsThruCell, oneOnNSteps);

        #pragma omp critical
        {
          for(ichan=0;ichan<img[im].nchan;ichan++){
            img[im].pixel[ppi].intense[ichan] += ray.intensity[ichan]*oneOnNRaysThisPixel;
            img[im].pixel[ppi].tau[    ichan] += ray.tau[      ichan]*oneOnNRaysThisPixel;
          }
        }
      }
      if (threadI == 0){ /* i.e., is master thread */
        if(!silent) {
          progress = ((double)nRaysDone)*oneOnNumActiveRaysMinus1;
          if(progress-lastProgress>0.002){
            lastProgress = progress;
            progressbar(progress, 13);
          }
        }
      }
    }

    if(par->traceRayAlgorithm==1){
      for(ii=0;ii<numInterpPoints;ii++)
        freePop2(par->nSpecies, gips[ii].mol);
    }
    free(ray.tau);
    free(ray.intensity);
  } /* End of parallel block. */

  img[im].trans=tmptrans;

  freeGAux((unsigned long)par->ncell, par->nSpecies, gAux);
  free(allLineMolIs);
  free(allLineLineIs);
  if(par->traceRayAlgorithm==1)
    free(dc);
  for (i=0;i<par->nThreads;i++){
    gsl_rng_free(threadRans[i]);
  }
  free(threadRans);
  gsl_rng_free(randGen);
}

