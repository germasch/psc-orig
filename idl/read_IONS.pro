; IONS

fi=FINDFILE('I*.data.gz')
nfi=size(fi,/N_ELEMENTS)
;loadct,5

for ifi=0,nfi-1 do begin                
   print,fi(ifi)

; READ EVERY N-TH PARTCLE

   step=1
   @PART_EVOL
   nistep=1*step*nistep

   ndim=size(x,/DIMENSIONS) 
   x1=0
   x2=ndim(0)-2
   ndim=size(y,/DIMENSIONS) 
   y1=0
   y2=ndim(0)-2
   ndim=size(z,/DIMENSIONS) 
   z1=0
   z2=ndim(0)-2


   if (nqcount gt 0) then begin


      print,'pxmin: ',min(pxp),' time: ',t
      print,'pxmax: ',max(pxp),' time: ',t   
      print,'pymin: ',min(pyp),' time: ',t
      print,'pymax: ',max(pyp),' time: ',t   
      print,'pzmin: ',min(pzp),' time: ',t
      print,'pzmax: ',max(pzp),' time: ',t   


; MOMENTA OF IONS IN m_ic
      pxmin=-1.0
      pxmax=1.0
      pxmid=(pxmax+pxmin)/2.0
      pymin=-1.0
      pymax=1.0
      pymid=(pymax+pymin)/2.0
      pzmin=-1.0
      pzmax=1.0
      pzmid=(pzmax+pzmin)/2.0

; SPECTRUM OF IONS IN MeV
      enmin=-0.0001
      enmax=0.0001
      enmid=(enmax+enmin)/2.0

; STEP SIZE OF SLICES
      stepx=x2-x1+2
      stepy=y2-y1+2
      stepz=z2-z1+2


      ydim=10000.0
      xdim=ydim*(i1x-i1n+1)/(i2x-i2n+1)
;      @parts_xy_ps
;      @parts_charge_xy_ps
;      @parts_mass_xy_ps
;      @parts_mass_charge_xy_ps


      ydim=10000.0
      xdim=ydim*(i1x-i1n+1)/(i3x-i3n+1)
;      @parts_xz_ps
;      @parts_charge_xz_ps
;      @parts_mass_xz_ps
;      @parts_mass_charge_xz_ps


      ydim=10000.0
      xdim=ydim*(i2x-i2n+1)/(i3x-i3n+1)
;      @parts_yz_ps
;      @parts_charge_yz_ps
;      @parts_mass_yz_ps
      @parts_mass_charge_yz_ps


      xdim=10000.0
      ydim=10000.0
;      @parts_xpxpypz_ps
;      @parts_mass_charge_xpxpypz_ps


      xdim=10000.0
      ydim=10000.0
;      @parts_ypxpypz_ps
      @parts_mass_charge_ypxpypz_ps


      xdim=10000.0
      ydim=10000.0
;      @parts_zpxpypz_ps
;      @parts_mass_charge_zpxpypz_ps


      xdim=10000.0
      ydim=10000.0
      @parts_enz_ps
;      @parts_enz_vs_enz_ps
      @parts_pxpypz_ps
;      @parts_charge_pxpypz_ps
;      @parts_mass_pxpypz_ps
      @parts_mass_charge_pxpypz_ps

   endif
endfor

set_plot,'X'
end
