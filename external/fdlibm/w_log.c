
/* @(#)w_log.c 1.3 95/01/18 */


#include "fdlibm.h"


#ifdef __STDC__
	double ieee_log(double x)		/* wrapper log */
#else
	double ieee_log(x)			/* wrapper log */
	double x;
#endif
{
#ifdef _IEEE_LIBM
	return __ieee754_log(x);
#else
	double z;
	z = __ieee754_log(x);
	if(_LIB_VERSION == _IEEE_ || ieee_isnan(x) || x > 0.0) return z;
	if(x==0.0)
	    return __kernel_standard(x,x,16); /* ieee_log(0) */
	else 
	    return __kernel_standard(x,x,17); /* ieee_log(x<0) */
#endif
}
