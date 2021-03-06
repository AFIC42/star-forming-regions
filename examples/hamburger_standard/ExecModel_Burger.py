"""
Basic docstring explaining example
"""
from __future__ import print_function
#------------------
#Import the package
#------------------
from sf3dmodels import *
#-----------------
#Extra libraries
#-----------------
from matplotlib import colors
import numpy as np
import os
import time

t0 = time.time()

#------------------
#General Parameters
#------------------
MStar = 0.86 * U.MSun 
MRate = 5.e-6 * U.MSun_yr 
RStar = U.RSun * ( MStar/U.MSun )**0.8 
LStar = U.LSun * ( MStar/U.MSun )**4 
TStar = U.TSun * ( (LStar/U.LSun) / (RStar/U.RSun)**2 )**0.25 
Rd = 264. * U.AU

print ('RStar:', RStar/U.RSun, ', LStar:', LStar/U.LSun, ', TStar:', TStar)

#---------------
#GRID Definition
#---------------
#Cubic grid, each edge ranges [-500, 500] AU.

sizex = sizey = sizez = 500 * U.AU
Nx = Ny = Nz = 200 #Number of divisions for each axis
GRID = Model.grid([sizex, sizey, sizez], [Nx, Ny, Nz])
NPoints = GRID.NPoints #Number of nodes in the grid

#-------------
#DENSITY
#-------------

#--------
#ENVELOPE
#--------
Rho0 = Res.Rho0(MRate, Rd, MStar)
Arho = None
Renv = 2.5 * Rd
densEnv = Model.density_Env_Disc(RStar, Rd, Rho0, Arho, GRID, discFlag = False, envFlag = True, 
                                 renv_max = Renv)
#-------
#DISC
#-------
H0sf = 0.03 #Disc scale height factor (H0 = H0sf * RStar)
Arho = 5.25
Rdisc = 1.5 * Rd
densDisc = Model.density_Hamburgers(RStar, H0sf, Rd, Rho0, Arho, GRID, discFlag = True, 
                                    rdisc_max = Rdisc)
#---------------------
#The COMPOSITE DENSITY
#---------------------
density = Model.Struct( **{ 'total': densEnv.total + densDisc.total,
                            'disc': densDisc.total, 
                            'env': densEnv.total,
                            'H': densDisc.H,
                            'Rt': densDisc.Rt,
                            'discFlag': True,
                            'envFlag': True,
                            'r_disc': densDisc.r_disc, 
                            'r_env': densEnv.r_env,
                            'streamline': densEnv.streamline} )

#-----------
#TEMPERATURE
#-----------
T10Env = 250. #Envelope temperature at 10 AU
Tmin = 10. #Minimum possible temperature. Every node with T<Tmin will inherit Tmin. 
BT = 60. #Adjustable factor for disc temperature. Extra, or less, disc heating.
temperature = Model.temperature_Hamburgers(TStar, RStar, MStar, MRate, Rd, T10Env, BT, 
                                           density, GRID, Tmin_disc = Tmin, inverted = False)

#--------
#VELOCITY
#--------
vel = Model.velocity(RStar, MStar, Rd, density, GRID)

#-------------------------------
#ABUNDANCE and GAS-to-DUST RATIO
#-------------------------------
ab0 = 5e-8 #CH3CN abundance vs H2
abundance = Model.abundance(ab0, NPoints)

gtd0 = 100. #Gas to dust ratio (H2 vs Dust)
gtdratio = Model.gastodust(gtd0, NPoints)

#-----------------------------
#WRITING DATA with LIME format
#-----------------------------
Model.DataTab_LIME(density.total, temperature.total, vel, abundance, gtdratio, GRID) 

#-----------------------------
#PRINTING resultant PROPERTIES
#-----------------------------
Model.PrintProperties(density, temperature, GRID)

#-------
#TIMING
#-------
print ('Ellapsed time: %.3fs' % (time.time() - t0))
print ('-------------------------------------------------\n-------------------------------------------------\n')

#----------------------------------------
#3D PLOTTING (weighting with temperature)
#----------------------------------------
tag = 'Burger'
dens_plot = density.total / 1e6

vmin, vmax = np.array([5e11, 5e15]) / 1e6
norm = colors.LogNorm(vmin=vmin, vmax=vmax)

weight = 10*T10Env
Plot_model.scatter3D(GRID, temperature.total, weight, NRand = 4000, colordim = dens_plot , axisunit = U.AU, cmap = 'hot', 
                     norm = norm, colorlabel = r'$\rho$ $[cm^{-3}]$', output = '3Dpoints%s.png'%tag, show = False)

#----------------------------------------
#2D PLOTTING (Density and Temperature)
#----------------------------------------

vmin, vmax = np.array([1e12, 1e17]) / 1e6
norm = colors.LogNorm(vmin=vmin, vmax=vmax)

Plot_model.plane2D(GRID, dens_plot, axisunit = U.AU, cmap = 'ocean_r', plane = {'z': 0*U.AU},
                   norm = norm, colorlabel = r'$[\rm cm^{-3}]$', output = 'DensMidplane_%s.png'%tag, show = False)

vmin, vmax = np.array([1e11, 5e15]) / 1e6
norm = colors.LogNorm(vmin=vmin, vmax=vmax)

Plot_model.plane2D(GRID, dens_plot, axisunit = U.AU, cmap = 'ocean_r', plane = {'y': 0*U.AU},
                   norm = norm, colorlabel = r'$[\rm cm^{-3}]$', output = 'DensVertical_%s.png'%tag, show = False)

vmin, vmax = np.array([5e1, 3e3])
norm = colors.LogNorm(vmin=vmin, vmax=vmax)

Plot_model.plane2D(GRID, temperature.total, axisunit = U.AU, cmap = 'ocean_r', plane = {'z': 0*U.AU},
                   norm = norm, colorlabel = r'[Kelvin]', output = 'TempMidplane_%s.png'%tag, show = False)

vmin, vmax = np.array([5e1, 2e3])
norm = colors.LogNorm(vmin=vmin, vmax=vmax)

Plot_model.plane2D(GRID, temperature.total, axisunit = U.AU, cmap = 'ocean_r', plane = {'y': 0*U.AU},
                   norm = norm, colorlabel = r'[Kelvin]', output = 'TempVertical_%s.png'%tag, show = False)


vmin, vmax = np.array([3e7, 5e12])
norm = colors.LogNorm(vmin=vmin, vmax=vmax)

Plot_model.plane2D(GRID, temperature.total * dens_plot, axisunit = U.AU, cmap = 'ocean_r', plane = {'y': 0*U.AU},
                   norm = norm, colorlabel = r'[$\rho$ T]', output = 'Emissivity_%s.png'%tag, show = False)
