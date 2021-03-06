rimx=6.0
rimy=6.0

XSIZE=xdim/1000.0+rimx
YSIZE=ydim/1000.0+rimy

x0=500.0*rimx
y0=500.0*rimy

xlen=size(x,/DIMENSIONS)
ylen=size(y,/DIMENSIONS)
zlen=size(z,/DIMENSIONS)


mass=fltarr(10)
mass(0)=mnp(0)
mnum=0L
for i=0L,nqcount-1 do begin
   exist=0.0
   for j=0L,mnum do begin
      if (mass(j) eq mnp(i)) then begin
         exist=1.0
      endif
   endfor
   if (exist eq 0.0) then begin
      mnum=mnum+1
      mass(mnum)=mnp(i)
   endif
endfor


charge=fltarr(10)
charge(0)=qnp(0)
qnum=0L
for i=0L,nqcount-1 do begin
   exist=0.0
   for j=0L,qnum do begin
      if (charge(j) eq qnp(i)) then begin
         exist=1.0
      endif
   endfor
   if (exist eq 0.0) then begin
      qnum=qnum+1
      charge(qnum)=qnp(i)
   endif
endfor


if (xlen(0)-1 gt 1) then begin
if (ylen(0)-1 gt 1) then begin


for ml=0L,mnum do begin
for ql=0L,qnum do begin
for pzl=1,pznum do begin


   pzmin=(1.0*pzl)/mult
   pzmax=(1.0*pzl+1.0)/mult


   pnum=0L
   for i=0L,nqcount-1 do begin
      if (mnp(i) eq mass(ml)) then begin
      if (qnp(i) eq charge(ql)) then begin
      if (pzmin le pzp(i)) and (pzp(i) lt pzmax) then begin
         pnum=pnum+1
      endif
      endif
      endif
   endfor

   if (pnum gt 0) then begin

      xpl=fltarr(pnum)
      ypl=fltarr(pnum)
      zpl=fltarr(pnum)
      pxpl=fltarr(pnum)
      pypl=fltarr(pnum)
      pzpl=fltarr(pnum)
      qnpl=fltarr(pnum)
      mnpl=fltarr(pnum)
      lnpl=fltarr(pnum)

      xpt=fltarr(pnum)
      ypt=fltarr(pnum)

      pnum=0L
      for i=0L,nqcount-1 do begin
         if (mnp(i) eq mass(ml)) then begin
         if (qnp(i) eq charge(ql)) then begin
         if (pzmin le pzp(i)) and (pzp(i) lt pzmax) then begin
            xpl(pnum)=1.0e6*xp(i)
            ypl(pnum)=1.0e6*yp(i)
            zpl(pnum)=1.0e6*zp(i)
            pxpl(pnum)=pxp(i)/pzp(i)
            pypl(pnum)=pyp(i)/pzp(i)
            pzpl(pnum)=pzp(i)
            qnpl(pnum)=qnp(i)
            mnpl(pnum)=mnp(i)
            lnpl(pnum)=lnp(i)

            xpt(pnum)=1.0e2*(xp(i)+pxp(i)*(zt-zp(i))/pzp(i))
            ypt(pnum)=1.0e2*(yp(i)+pyp(i)*(zt-zp(i))/pzp(i))

            pnum=pnum+1
         endif
         endif
         endif
      endfor


      xs=0.0
      xu=2.0
      ys=0.0
      yu=2.0


      xpt=xpt+0.5*(xu-xs)
      ypt=ypt+0.5*(yu-ys)


      fo=strmid(nmfc,0,strlen(nmfc)-13)+'_xy-far'$
      +'_pz'+strtrim(string(pzmin),2)+'-'+strtrim(string(pzmax),2)$
      +'_m'+strtrim(string(mass(ml)),2)$
      +'_q'+strtrim(string(charge(ql)),2)$
      +'_t'+strmid(nmfc,strlen(nmfc)-13,5)$
      +'.eps'

      print,fo

      SET_PLOT,'PS'
      DEVICE,/color,/encapsulated,file=fo,XSIZE=XSIZE,YSIZE=YSIZE
      erase, 255      

      plot, xpt, ypt, xs=1, ys=1, charsize=1.6, psym=3, $
      POSITION=[x0,y0,x0+xdim,y0+ydim],/DEVICE, $ 
      xtitle='!8x/!8cm',$
      xticks=2,$
      xr=[xs,xu], $
      ytitle='!8y/!8cm',$
      yticks=2,$
      yr=[ys,yu], $
      color=0,background=255 

            number=n0*cori*dx*dy*dz*ld*ld*ld*pnum
            ttt='!8t='+strtrim(string(t*1.0e15),2)+'fs'
            posz='!8p!Dz!N='+strtrim(string(pzmin),2)+'-'+strtrim(string(pzmax),2)+'!8mc'
            number='!8N='+strtrim(string(number),2)
            xyouts,300,y0+ydim+2500.0,ttt,align=0.0,charsize=1.0,color=0,/DEVICE
            xyouts,300,y0+ydim+2200.0,posz,align=0.0,charsize=1.0,color=0,/DEVICE
            xyouts,300,y0+ydim+1900.0,number,align=0.0,charsize=1.0,color=0,/DEVICE

      DEVICE, /CLOSE
      SET_PLOT,'X'


      nx=x2-x1+1
      ny=y2-y1+1
      xs=0.0
      xu=2.0
      ys=0.0
      yu=2.0
      dxt=(xu-xs)/nx
      dyt=(yu-ys)/ny
      nf=fltarr(nx+2,ny+2)

      p=0L
      for l=0L,pnum-1 do begin
 
         xq=xpt(l)
         yq=ypt(l)
 
         if (xs lt xq) and (xq lt xu) then begin
         if (ys lt yq) and (yq lt yu) then begin
 
         k1=floor(xq/dxt)
         k2=floor(yq/dyt)
 
         xq=xq/dxt-k1
         yq=yq/dyt-k2
 
         h1=(1.0-xq)*(1.0-yq)
         h2=xq*(1.0-yq)
         h3=(1.0-xq)*yq
         h4=xq*yq
 
         nf(k1,k2)=nf(k1,k2)+h1
         nf(k1+1,k2)=nf(k1+1,k2)+h2
         nf(k1,k2+1)=nf(k1,k2+1)+h3
         nf(k1+1,k2+1)=nf(k1+1,k2+1)+h4

         endif
         endif

      endfor


      fo=strmid(nmfc,0,strlen(nmfc)-13)+'_xy-farscale'$
      +'_pz'+strtrim(string(pzmin),2)+'-'+strtrim(string(pzmax),2)$
      +'_m'+strtrim(string(mass(ml)),2)$
      +'_q'+strtrim(string(charge(ql)),2)$
      +'_t'+strmid(nmfc,strlen(nmfc)-13,5)$
      +'.eps'

      print,fo

      SET_PLOT,'PS'
      DEVICE,/color,/encapsulated,file=fo,XSIZE=XSIZE,YSIZE=YSIZE
      erase, 255      


      sfxy=smooth(nf,5)
      rmin=min(sfxy)
      rmax=max(sfxy)
 
      CONTOUR,sfxy(0:nx,0:ny), $
      xs=1,ys=1,charsize=1.6, $
      POSITION=[x0,y0,x0+xdim,y0+ydim], $
      xtitle='!8x/!8cm', $
      xticks=2,$
      xr=[xs,xu], $
      ytitle='!8y/!8cm', $
      yticks=2,$
      yr=[ys,yu], $
      background=255,color=0, $
      /DEVICE,/NODATA

      ixy = BYTSCL(sfxy(0:nx,0:ny), MIN=rmin, MAX=rmax)
      TV, ixy, !X.WINDOW(0),!Y.WINDOW(0),$
      XSIZE=!X.WINDOW(1) - !X.WINDOW(0),$
      YSIZE=!Y.WINDOW(1) - !Y.WINDOW(0), /NORM
 
      if (rmax-rmin gt 0.0) then begin
         x11=(x0+xdim+500.0)/(xdim+2.0*x0)
         y11=y0/(ydim+2.0*y0)
         x22=(x0+xdim+800.0)/(xdim+2.0*x0)
         y22=(y0+ydim)/(ydim+2.0*y0)
         colorbar, division=2, NCOLORS=!D.Table_size, /right, /vertical, $
         color=0, charsize=1.5, min=rmin, max=rmax, $
         /pscolor, POSITION=[x11,y11,x22,y22]
      endif

            number=n0*cori*dx*dy*dz*ld*ld*ld*pnum
            ttt='!8t='+strtrim(string(t*1.0e15),2)+'fs'
            posz='!8p!Dz!N='+strtrim(string(pzmin),2)+'-'+strtrim(string(pzmax),2)+'!8mc'
            number='!8N='+strtrim(string(number),2)
            xyouts,300,y0+ydim+2500.0,ttt,align=0.0,charsize=1.0,color=0,/DEVICE
            xyouts,300,y0+ydim+2200.0,posz,align=0.0,charsize=1.0,color=0,/DEVICE
            xyouts,300,y0+ydim+1900.0,number,align=0.0,charsize=1.0,color=0,/DEVICE

      DEVICE, /CLOSE
      SET_PLOT,'X'


      fo=strmid(nmfc,0,strlen(nmfc)-13)+'_xy-near'$
      +'_pz'+strtrim(string(pzmin),2)+'-'+strtrim(string(pzmax),2)$
      +'_m'+strtrim(string(mass(ml)),2)$
      +'_q'+strtrim(string(charge(ql)),2)$
      +'_t'+strmid(nmfc,strlen(nmfc)-13,5)$
      +'.eps'

      print,fo

      SET_PLOT,'PS'
      DEVICE,/color,/encapsulated,file=fo,XSIZE=XSIZE,YSIZE=YSIZE
      erase, 255      


      plot, xpl, ypl, xs=1, ys=1, charsize=1.6, psym=3, $
      POSITION=[x0,y0,x0+xdim,y0+ydim],/DEVICE, $ 
      xtitle='!8x/!7l!8m',$
      xticks=2,$
;      xtickn=[strtrim(string(1.e6*x(x1)),2), $
;              strtrim(string(0.5e6*(x(x2)+x(x1))),2), $
;              strtrim(string(1.e6*x(x2)),2)], $
      xr=1.0e6*[x(x1),x(x2)], $
      ytitle='!8y/!7l!8m',$
      yticks=2,$
;      ytickn=[strtrim(string(1.e6*y(y1)),2), $
;              strtrim(string(0.5e6*(y(y2)+y(y1))),2), $
;              strtrim(string(1.e6*y(y2)),2)], $
      yr=1.0e6*[y(y1),y(y2)], $
      color=0,background=255 

            number=n0*cori*dx*dy*dz*ld*ld*ld*pnum
            ttt='!8t='+strtrim(string(t*1.0e15),2)+'fs'
            posz='!8p!Dz!N='+strtrim(string(pzmin),2)+'-'+strtrim(string(pzmax),2)+'!8mc'
            number='!8N='+strtrim(string(number),2)
            xyouts,300,y0+ydim+2500.0,ttt,align=0.0,charsize=1.0,color=0,/DEVICE
            xyouts,300,y0+ydim+2200.0,posz,align=0.0,charsize=1.0,color=0,/DEVICE
            xyouts,300,y0+ydim+1900.0,number,align=0.0,charsize=1.0,color=0,/DEVICE

      DEVICE, /CLOSE
      SET_PLOT,'X'

   endif

endfor
endfor
endfor

endif
endif
