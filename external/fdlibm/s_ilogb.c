
/* @(#)s_ilogb.c 1.3 95/01/18 */


#include "fdlibm.h"

#ifdef __STDC__
	int ieee_ilogb(double x)
#else
	int ieee_ilogb(x)
	double x;
#endif
{
	int hx,lx,ix;

	hx  = (__HI(x))&0x7fffffff;	/* high word of x */
	if(hx<0x00100000) {
	    lx = __LO(x);
	    if((hx|lx)==0) 
		return 0x80000001;	/* ieee_ilogb(0) = 0x80000001 */
	    else			/* subnormal x */
		if(hx==0) {
		    for (ix = -1043; lx>0; lx<<=1) ix -=1;
		} else {
		    for (ix = -1022,hx<<=11; hx>0; hx<<=1) ix -=1;
		}
	    return ix;
	}
	else if (hx<0x7ff00000) return (hx>>20)-1023;
	else return 0x7fffffff;
}
